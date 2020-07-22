#include "jdesp.h"
#include "driver/uart.h"

//#define LOG(msg, ...) DMESG("U:" msg, ##__VA_ARGS__)
#define LOG(...) ((void)0)

#define PIN GPIO_NUM_22
#define MUX_REG IO_MUX_GPIO22_REG
#define UART_NUM UART_NUM_2
#define UART UART2
#define URXD_IN_IDX U2RXD_IN_IDX
#define UTXD_OUT_IDX U2TXD_OUT_IDX

#define UART_EMPTY_THRESH_DEFAULT (10)
#define UART_FULL_THRESH_DEFAULT (120)
#define UART_TOUT_THRESH_DEFAULT (10)

static xQueueHandle gpio_evt_queue = NULL;
static uint8_t *fifo_buf;
static uint16_t tx_len, rx_len, data_left;
static bool seen_low, rx_ended;

#define EV_LOW_PULSE 1
#define EV_RX_END 2
#define EV_TX_END 3

#define CHK(e)                                                                                     \
    if ((e) != ESP_OK)                                                                             \
    jd_panic()

static IRAM_ATTR esp_err_t xgpio_set_level(gpio_num_t gpio_num, uint32_t level) {
    if (level) {
        GPIO.out_w1ts = (1 << gpio_num);
    } else {
        GPIO.out_w1tc = (1 << gpio_num);
    }
    return ESP_OK;
}

static IRAM_ATTR void pin_rx(void) {
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[PIN], PIN_FUNC_GPIO);
    REG_SET_BIT(GPIO_PIN_MUX_REG[PIN], FUN_PU);
    PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[PIN]);
    GPIO.enable_w1tc = (0x1 << PIN);
    REG_WRITE(GPIO_FUNC0_OUT_SEL_CFG_REG + (PIN * 4), SIG_GPIO_OUT_IDX);
    gpio_matrix_in(PIN, URXD_IN_IDX, 0);
}

static IRAM_ATTR void pin_tx(void) {
    gpio_matrix_in(GPIO_FUNC_IN_HIGH, URXD_IN_IDX, 0); // UART
    GPIO.pin[PIN].int_type = GPIO_PIN_INTR_DISABLE;
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[PIN], PIN_FUNC_GPIO);
    xgpio_set_level(PIN, 1);
    gpio_matrix_out(PIN, UTXD_OUT_IDX, 0, 0);
}

static void send_ev(int ev) {
    xQueueSendFromISR(gpio_evt_queue, &ev, NULL);
}

static void uart_dispatcher(void *arg) {
    uint32_t event;
    for (;;) {
        if (xQueueReceive(gpio_evt_queue, &event, portMAX_DELAY)) {
            LOG("ev: %d", event);
            switch (event) {
            case EV_LOW_PULSE:
                if (!fifo_buf)
                    jd_line_falling();
                break;
            case EV_RX_END:
                log_pin_pulse(0, 2);
                jd_rx_completed(data_left);
                break;
            case EV_TX_END:
                jd_tx_completed(0);
                break;
            default:
                jd_panic();
                break;
            }
        }
    }
}

int uart_wait_high(void) {
    // we already started RX at this point
    return 0;
}

void uart_disable(void) {
    UART.int_clr.val = 0xffffffff;
    UART.int_ena.val = UART_BRK_DET_INT_ENA;
    seen_low = 0;
    rx_len = tx_len = 0;
    fifo_buf = NULL;
    rx_ended = 0;
    pin_rx();
}

static IRAM_ATTR void fill_fifo(void) {
    if (!tx_len)
        return;

    int space = UART_FIFO_LEN - UART.status.txfifo_cnt;
    if (tx_len < space)
        space = tx_len;

    for (int i = 0; i < space; i++)
        UART.fifo.rw_byte = fifo_buf[i];

    fifo_buf += space;
    tx_len -= space;

    if (tx_len == 0) {
        LOG("txbrk");
        UART.idle_conf.tx_brk_num = 14; // 14us
        UART.conf0.txd_brk = 1;
        UART.int_clr.tx_brk_done = 1;
        UART.int_ena.tx_brk_done = 1;
    }

    UART.int_clr.txfifo_empty = 1;
    UART.conf1.txfifo_empty_thrhd = UART_EMPTY_THRESH_DEFAULT;
    UART.int_ena.txfifo_empty = 1;
}

static IRAM_ATTR void read_fifo(int force) {
    uart_dev_t *uart_reg = &UART;
    int rx_fifo_len = uart_reg->status.rxfifo_cnt;

    if (!force && fifo_buf == NULL && rx_fifo_len < UART_FULL_THRESH_DEFAULT - 1)
        return; // read not started yet and we're not overflowing

    if (rx_fifo_len) {
        LOG("rxfifo %d", rx_fifo_len);
        int n = rx_fifo_len;
        if (n > rx_len)
            n = rx_len;

        rx_len -= n;
        rx_fifo_len -= n;

        while (n-- > 0)
            *fifo_buf++ = uart_reg->fifo.rw_byte;
        while (rx_fifo_len-- > 0)
            (void)uart_reg->fifo.rw_byte;
    }
}

#define END_RX_FLAGS (UART_RXFIFO_TOUT_INT_ST | UART_BRK_DET_INT_ST | UART_FRM_ERR_INT_ST)

static IRAM_ATTR void start_bg_rx(void) {
    read_fifo(1); // flush any data
    seen_low = 1;
    UART.int_ena.val |= END_RX_FLAGS | UART_RXFIFO_FULL_INT_ENA;
    send_ev(EV_LOW_PULSE);
}

static IRAM_ATTR void uart_isr(void *dummy) {
    uart_dev_t *uart_reg = &UART;

    uint32_t uart_intr_status = uart_reg->int_st.val;
    uart_reg->int_clr.val = uart_intr_status; // clear all

    LOG("ISR %x", uart_intr_status);

    read_fifo(0);

    if (!seen_low && (uart_intr_status & UART_BRK_DET_INT_ST)) {
        log_pin_pulse(0, 1);
        start_bg_rx();
    } else if (uart_intr_status & UART_TX_BRK_DONE_INT_ST) {
        uart_reg->conf0.txd_brk = 0;
        uart_disable();
        send_ev(EV_TX_END);
    } else if (uart_intr_status & UART_TXFIFO_EMPTY_INT_ST) {
        uart_reg->int_ena.txfifo_empty = 0;
        fill_fifo();
    } else if (uart_intr_status & END_RX_FLAGS) {
        log_pin_pulse(0, 3);
        LOG("end, rx=%d", rx_len);
        data_left = rx_len;
        int had_buf = fifo_buf != NULL;
        uart_disable();
        if (had_buf)
            send_ev(EV_RX_END);
        else
            rx_ended = 1;
    }
}

static IRAM_ATTR NOINLINE_ATTR void probe_and_set(volatile uint32_t *oe, volatile uint32_t *inp,
                                                  uint32_t mask) {
    *oe = *inp & mask;
}

static void tx_race() {
    // don't reconnect the pin in the middle of the low-pulse
    int timeout = 50000;
    while (timeout-- > 0 && gpio_get_level(PIN) == 0)
        ;
    pin_rx();
    start_bg_rx();
}

IRAM_ATTR int uart_start_tx(const void *data, uint32_t numbytes) {
    if (tx_len)
        jd_panic();

    if (seen_low || UART.status.st_urx_out != 0)
        return -1;

    gpio_matrix_in(GPIO_FUNC_IN_HIGH, URXD_IN_IDX, 0); // UART
    PIN_FUNC_SELECT(MUX_REG, PIN_FUNC_GPIO);
    GPIO.out_w1tc = (1 << PIN);

    probe_and_set(&GPIO.enable_w1ts, &GPIO.in, 1 << PIN);

    if (!(GPIO.enable & (1 << PIN))) {
        // the line went down in the meantime
        tx_race();
        return -1;
    }

    target_wait_us(12); // low pulse is 14us with wait of 12 here
    xgpio_set_level(PIN, 1);

    target_wait_us(36); // 41us from end of low pulse to start bit with wait of 36 here

    pin_tx();

    fifo_buf = (uint8_t *)data;
    tx_len = numbytes;

    UART.int_clr.val = 0xffffffff;

    fill_fifo();

    return 0;
}

void uart_flush_rx(void) {
    target_disable_irq();
    read_fifo(1);
    target_enable_irq();
}

void uart_start_rx(void *data, uint32_t maxbytes) {
    if (rx_len || tx_len)
        jd_panic();

    log_pin_pulse(0, 1);

    fifo_buf = data;
    rx_len = maxbytes;
    LOG("ini rx=%d", maxbytes);

    uart_flush_rx();

    log_pin_pulse(0, 2);

    if (rx_ended) {
        rx_ended = 0;
        rx_len = 0;
        fifo_buf = NULL;
        send_ev(EV_RX_END);
    }
}

void uart_init(void) {
    const uart_config_t uart_config = //
        {.baud_rate = 1000000,
         .data_bits = UART_DATA_8_BITS,
         .parity = UART_PARITY_DISABLE,
         .stop_bits = UART_STOP_BITS_1,
         .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};
    CHK(uart_param_config(UART_NUM, &uart_config));
    CHK(uart_isr_register(UART_NUM, uart_isr, NULL, 0, NULL));

    uart_intr_config_t uart_intr = //
        {.intr_enable_mask = 0,
         .rxfifo_full_thresh = UART_FULL_THRESH_DEFAULT,
         .rx_timeout_thresh = 30, // 30us
         .txfifo_empty_intr_thresh = UART_EMPTY_THRESH_DEFAULT};
    CHK(uart_intr_config(UART_NUM, &uart_intr));

    gpio_evt_queue = xQueueCreate(5, sizeof(uint32_t));
    xTaskCreatePinnedToCore(uart_dispatcher, "uart_dispatcher", 2048, NULL, 6, NULL, APP_CPU_NUM);

    gpio_set_pull_mode(PIN, GPIO_PULLUP_ONLY);
    gpio_set_direction(PIN, GPIO_MODE_INPUT);

    uart_disable();
}
