#include "jdesp.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_private/periph_ctrl.h"
#include "esp_timer.h"
#include "driver/uart.h"
#include "hal/uart_ll.h"
#include "hal/gpio_ll.h"
#include "rom/gpio.h"

typedef struct jacdac_ctx {
    uint8_t pin_num;
    uint8_t uart_num;
    volatile bool seen_low;
    bool rx_ended;
    bool in_tx;
    volatile uint16_t tx_len;
    volatile uint16_t rx_len;
    uint16_t data_left;
    uint8_t *volatile fifo_buf;
    cb_t timer_cb;
    uart_dev_t *uart_hw;

    esp_timer_handle_t timer;
    intr_handle_t intr_handle;

    volatile bool cb_rx;
    volatile bool cb_tx;
    volatile bool cb_fall;
    esp_timer_handle_t timer0;

    esp_timer_handle_t timer_worker;
    worker_t tim_worker;
} jacdac_ctx_t;

static jacdac_ctx_t context;

#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#define tx_brk_done_int_clr tx_brk_done
#define tx_brk_done_int_ena tx_brk_done
#define txfifo_empty_int_clr txfifo_empty
#define txfifo_empty_int_ena txfifo_empty
#define brk_det_int_raw brk_det
#endif

// #define LOG(msg, ...) DMESG("JD: " msg, ##__VA_ARGS__)
#define LOG JD_NOLOG

// #define PIN_LOG_0 GPIO_NUM_3
// #define PIN_LOG_1 GPIO_NUM_4

#define UART_EMPTY_THRESH_DEFAULT (10)
#define UART_FULL_THRESH_DEFAULT (120)
#define UART_TOUT_THRESH_DEFAULT (10)

static IRAM_ATTR void uart_isr(void *);

static void init_log_pins(void) {
#ifdef PIN_LOG_0
    gpio_set_direction(PIN_LOG_0, GPIO_MODE_OUTPUT);
    gpio_set_direction(PIN_LOG_1, GPIO_MODE_OUTPUT);
#endif
}

static void log_pin_pulse(int pinid, int numpulses) {
#ifdef PIN_LOG_0
    uint32_t mask = pinid == 0 ? 1 << PIN_LOG_0 : 1 << PIN_LOG_1;
    while (numpulses--) {
        GPIO.out_w1ts = mask;
        GPIO.out_w1tc = mask;
    }
#endif
}

void timer_log(int line, int v) {}
void log_pin_set(int line, int v) {}

static void jd_timer(void *dummy) {
    target_disable_irq();
    cb_t f = context.timer_cb;
    if (f) {
        context.timer_cb = NULL;
        target_enable_irq();
        f();
    } else {
        target_enable_irq();
    }
}

static void jd_timer0(void *dummy) {
    LOG("t0 rx=%d tx=%d fall=%d", context.cb_rx, context.cb_tx, context.cb_fall);
    if (context.cb_rx) {
        context.cb_rx = 0;
        jd_rx_completed(0);
    }
    if (context.cb_tx) {
        context.cb_tx = 0;
        jd_tx_completed(0);
    }
    if (context.cb_fall) {
        context.cb_fall = 0;
        jd_line_falling();
    }
}

static void jd_tim_worker(void *dummy) {
    worker_do_work(context.tim_worker);
}

int tim_worker_run(TaskFunction_t fn, void *arg) {
    int r = worker_run(context.tim_worker, fn, arg);
    if (r == 0) {
        esp_timer_stop(context.timer_worker);
        esp_timer_start_once(context.timer_worker, 0);
    }
    return r;
}

void tim_init(void) {
    init_log_pins();

    esp_timer_create_args_t args;
    args.callback = (esp_timer_cb_t)jd_timer;
    args.arg = NULL;
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "JD timeout";
    esp_timer_create(&args, &context.timer);

    args.callback = (esp_timer_cb_t)jd_timer0;
    args.name = "JD callback";
    esp_timer_create(&args, &context.timer0);

    args.callback = (esp_timer_cb_t)jd_tim_worker;
    args.name = "tim_worker";
    esp_timer_create(&args, &context.timer_worker);

    context.tim_worker = worker_alloc();
}

static void schedule_timer0(void) {
    esp_timer_stop(context.timer0);
    esp_timer_start_once(context.timer0, 0);
}

uint64_t tim_get_micros(void) {
    return esp_timer_get_time();
}

void tim_set_timer(int delta, cb_t callback) {
    // compensate for overheads
    delta -= JD_TIM_OVERHEAD;
    if (delta < 20)
        delta = 20;

    target_disable_irq();
    if (!!context.timer) {
        context.timer_cb = callback;
        esp_timer_stop(context.timer);
        if (callback) {
            esp_timer_start_once(context.timer, delta);
        }
    }
    target_enable_irq();
}

static IRAM_ATTR esp_err_t xgpio_set_level(gpio_num_t gpio_num, uint32_t level) {
    gpio_ll_set_level(&GPIO, gpio_num, level);
    return ESP_OK;
}

// #define RX_SIG uart_periph_signal[context.uart_num].rx_sig
// #define TX_SIG uart_periph_signal[context.uart_num].tx_sig

#define RX_SIG uart_periph_signal[context.uart_num].pins[SOC_UART_RX_PIN_IDX].signal
#define TX_SIG uart_periph_signal[context.uart_num].pins[SOC_UART_TX_PIN_IDX].signal

static IRAM_ATTR void pin_rx(void) {
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[context.pin_num], PIN_FUNC_GPIO);
    REG_SET_BIT(GPIO_PIN_MUX_REG[context.pin_num], FUN_PU);
    PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[context.pin_num]);
    gpio_ll_output_disable(&GPIO, context.pin_num);
    gpio_matrix_in(context.pin_num, RX_SIG, 0);
}

static IRAM_ATTR void pin_tx(void) {
    gpio_matrix_in(GPIO_FUNC_IN_HIGH, RX_SIG,
                   0); // context.uart_hw
    GPIO.pin[context.pin_num].int_type = GPIO_PIN_INTR_DISABLE;
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[context.pin_num], PIN_FUNC_GPIO);
    xgpio_set_level(context.pin_num, 1);
    gpio_matrix_out(context.pin_num, TX_SIG, 0, 0);
}

static IRAM_ATTR void fill_fifo(void) {
    if (!context.tx_len) {
        return;
    }

    int space = UART_FIFO_LEN - context.uart_hw->status.txfifo_cnt;
    if (context.tx_len < space) {
        space = context.tx_len;
    }

    uart_ll_write_txfifo(context.uart_hw, context.fifo_buf, space);

    context.fifo_buf += space;
    context.tx_len -= space;

    if (context.tx_len == 0) {
        LOG("txbrk");
        uart_ll_tx_break(context.uart_hw, 14);
        context.uart_hw->int_clr.tx_brk_done_int_clr = 1;
        context.uart_hw->int_ena.tx_brk_done_int_ena = 1;
    }

    context.uart_hw->int_clr.txfifo_empty_int_clr = 1;
    context.uart_hw->conf1.txfifo_empty_thrhd = UART_EMPTY_THRESH_DEFAULT;
    context.uart_hw->int_ena.txfifo_empty_int_ena = 1;
}

static IRAM_ATTR void read_fifo(int force) {
    uart_dev_t *uart_reg = context.uart_hw;
    int rx_fifo_len = uart_reg->status.rxfifo_cnt;

    if (!force && context.fifo_buf == NULL && rx_fifo_len < UART_FULL_THRESH_DEFAULT - 1) {
        return; // read not started yet and we're not overflowing
    }
    if (rx_fifo_len) {
        LOG("rxfifo %d", rx_fifo_len);
        int n = rx_fifo_len;
        int needs_rst = 0;
        if (n > context.rx_len) {
            n = context.rx_len;
            needs_rst = 1;
        }

        if (n) {
            context.rx_len -= n;
            rx_fifo_len -= n;
            uart_ll_read_rxfifo(uart_reg, context.fifo_buf, n);
            context.fifo_buf += n;
        }

        // and drop the rest of data
        if (needs_rst) {
            uart_ll_rxfifo_rst(uart_reg);
        }
    }
}

void uart_init_(void) {
    uint8_t pinnum = dcfg_get_pin("jacdac.pin");
    if (pinnum == NO_PIN) {
        DMESG("jacdac.pin not defined");
        return;
    }

    if (!(1 <= pinnum && pinnum < 32)) {
        DMESG("invalid jacdac.pin");
        JD_PANIC();
    }

    context.uart_num = UART_NUM_MAX - 1;
    DMESG("Jacdac on UART%d IO%d", context.uart_num, pinnum);
    context.uart_hw = UART_LL_GET_HW(context.uart_num);

    context.pin_num = pinnum;

    // uart_mark_used(context.uart_num, true);

    periph_module_enable(uart_periph_signal[context.uart_num].module);

    const uart_config_t uart_config = {.baud_rate = 1000000,
                                       .data_bits = UART_DATA_8_BITS,
                                       .parity = UART_PARITY_DISABLE,
                                       .stop_bits = UART_STOP_BITS_1,
                                       .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};
    CHK(uart_param_config(context.uart_num, &uart_config));
    CHK(esp_intr_alloc(uart_periph_signal[context.uart_num].irq, 0, (void (*)(void *))uart_isr,
                       &context, &context.intr_handle));

    uart_intr_config_t uart_intr = {.intr_enable_mask = 0,
                                    .rxfifo_full_thresh = UART_FULL_THRESH_DEFAULT,
                                    .rx_timeout_thresh = 30, // 30us
                                    .txfifo_empty_intr_thresh = UART_EMPTY_THRESH_DEFAULT};
    CHK(uart_intr_config(context.uart_num, &uart_intr));

    gpio_config_t cfg = {
        .pin_bit_mask = BIT64(context.pin_num),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = true,
        .pull_down_en = false,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    uart_disable();
}

#define END_RX_FLAGS (UART_RXFIFO_TOUT_INT_ST | UART_BRK_DET_INT_ST | UART_FRM_ERR_INT_ST)

static IRAM_ATTR void start_bg_rx(void) {
    JD_ASSERT(!context.in_tx);
    read_fifo(1); // flush any data
    context.seen_low = 1;
    context.uart_hw->int_ena.val |= END_RX_FLAGS | UART_RXFIFO_FULL_INT_ENA;
    if (!context.fifo_buf) {
        context.cb_fall = 1;
        schedule_timer0();
    }
}

static void uart_isr(void *dummy) {
    log_pin_pulse(0, 1);

    if (!context.intr_handle)
        return;

    uart_dev_t *uart_reg = context.uart_hw;

    uint32_t uart_intr_status = uart_reg->int_st.val;
    uart_reg->int_clr.val = uart_intr_status; // clear all

    // mask out disabled interrupts
    uart_intr_status &= uart_reg->int_ena.val;

    LOG("ISR %x %d", uart_intr_status, context.seen_low);

    read_fifo(0);

    if (!context.seen_low && (uart_intr_status & UART_BRK_DET_INT_ST)) {
        log_pin_pulse(0, 2);
        start_bg_rx();
    } else if (uart_intr_status & UART_TX_BRK_DONE_INT_ST) {
        uart_reg->conf0.txd_brk = 0;
        context.in_tx = 0;
        uart_disable();
        context.cb_tx = 1;
        schedule_timer0();
    } else if (uart_intr_status & UART_TXFIFO_EMPTY_INT_ST) {
        uart_reg->int_ena.txfifo_empty_int_ena = 0;
        fill_fifo();
    } else if (uart_intr_status & END_RX_FLAGS) {
        log_pin_pulse(0, 4);
        context.data_left = context.rx_len;
        int had_buf = context.fifo_buf != NULL;
        LOG("%d end, rx=%d %d", (int)esp_timer_get_time(), context.rx_len, had_buf);
        uart_disable();
        if (had_buf) {
            log_pin_pulse(0, 5);
            context.cb_rx = 1;
            schedule_timer0();
        } else {
            context.rx_ended = 1;
        }
    }
}

static IRAM_ATTR NOINLINE_ATTR void probe_and_set(volatile uint32_t *oe, volatile uint32_t *inp,
                                                  uint32_t mask) {
    *oe = *inp & mask;
}

static void tx_race(void) {
    // don't reconnect the pin in the middle of the low-pulse
    int timeout = 50000;
    while (timeout-- > 0 && gpio_get_level(context.pin_num) == 0) {
        ;
    }
    pin_rx();
    if (!context.seen_low)
        start_bg_rx(); // some rare race here
}

#if defined(CONFIG_IDF_TARGET_ESP32C3)
#define GPIO_VAL(x) (GPIO.x.val)
#else
#define GPIO_VAL(x) (GPIO.x)
#endif

int uart_start_tx(const void *data, uint32_t numbytes) {
    if (!context.uart_hw) {
        jd_tx_completed(0);
        return 0;
    }

    if (context.tx_len || context.in_tx) {
        JD_PANIC();
    }

    target_disable_irq();
    if (!context.intr_handle || context.seen_low || context.uart_hw->int_raw.brk_det_int_raw) {
        LOG("seen low %p %d %p", &context, context.seen_low, context.uart_hw->int_raw.brk_det);
        target_enable_irq();
        return -1;
    }

    gpio_matrix_in(GPIO_FUNC_IN_HIGH, RX_SIG, 0); // context.uart_hw
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[context.pin_num], PIN_FUNC_GPIO);
    xgpio_set_level(context.pin_num, 0);

    probe_and_set(&GPIO_VAL(enable_w1ts), &GPIO_VAL(in), 1 << context.pin_num);

    if (!(GPIO_VAL(enable) & (1 << context.pin_num))) {
        // the line went down in the meantime
        tx_race();
        target_enable_irq();
        return -1;
    }

    JD_ASSERT(!context.seen_low);
    context.in_tx = 1;

    target_wait_us(12); // low pulse is 14us with wait of 12 here
    xgpio_set_level(context.pin_num, 1);

    target_wait_us(50);

    pin_tx();

    context.fifo_buf = (uint8_t *)data;
    context.tx_len = numbytes;

    context.uart_hw->int_clr.val = 0xffffffff;

    fill_fifo();

    target_enable_irq();

    return 0;
}

void uart_flush_rx(void) {
    target_disable_irq();
    read_fifo(1);
    target_enable_irq();
}

void uart_start_rx(void *data, uint32_t maxbytes) {
    if (context.rx_len || context.tx_len) {
        JD_PANIC();
    }

    log_pin_pulse(0, 3);

    target_disable_irq();
    context.fifo_buf = data;
    context.rx_len = maxbytes;
    target_enable_irq();

    LOG("ini rx=%d", maxbytes);

    uart_flush_rx();

    // log_pin_pulse(0, 2);

    if (context.rx_ended) {
        target_disable_irq();
        context.seen_low = 0;
        context.rx_ended = 0;
        context.rx_len = 0;
        context.fifo_buf = NULL;
        target_enable_irq();
        // log_pin_pulse(0, 2);
        jd_rx_completed(0);
    }
}

void uart_disable(void) {
    target_disable_irq();
    context.uart_hw->int_clr.val = context.uart_hw->int_st.val;
    context.uart_hw->int_ena.val = UART_BRK_DET_INT_ENA;
    context.seen_low = 0;
    context.cb_fall = 0;
    context.rx_len = context.tx_len = 0;
    context.fifo_buf = NULL;
    context.rx_ended = 0;
    read_fifo(1);
    pin_rx();
    target_enable_irq();
    log_pin_pulse(1, 1);
}

int uart_wait_high(void) {
    // we already started RX at this point
    return 0;
}
