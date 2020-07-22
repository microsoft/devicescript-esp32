#include "jdesp.h"
#include "driver/periph_ctrl.h"
#include "driver/timer.h"

typedef struct {
    uint32_t dummy;
} timer_event_t;

static xQueueHandle timer_queue;
static volatile cb_t timer_cb;

static void IRAM_ATTR timer_group0_isr(void *para) {
    timer_event_t evt;
    evt.dummy = 1;
    TIMERG0.int_clr_timers.t0 = 1;
    xQueueSendFromISR(timer_queue, &evt, NULL);
}

static void timer_dispatcher(void *arg) {
    while (1) {
        timer_event_t evt;
        xQueueReceive(timer_queue, &evt, portMAX_DELAY);
        cb_t f = timer_cb;
        timer_cb = NULL;
        if (f)
            f();
    }
}

IRAM_ATTR uint64_t tim_get_micros(void) {
    uint64_t r;

    target_disable_irq();
    TIMERG0.hw_timer[TIMER_0].update = 1;
    r = ((uint64_t)TIMERG0.hw_timer[TIMER_0].cnt_high << 32) |
        (TIMERG0.hw_timer[TIMER_0].cnt_low);
    target_enable_irq();

    return r;
}

void tim_init(void) {
    timer_queue = xQueueCreate(3, sizeof(timer_event_t));

    timer_idx_t timer_idx = TIMER_0;
    timer_config_t config;
    config.divider = TIMER_BASE_CLK / 1000000;
    config.counter_dir = TIMER_COUNT_UP;
    config.counter_en = TIMER_PAUSE;
    config.alarm_en = TIMER_ALARM_DIS;
    config.intr_type = TIMER_INTR_LEVEL;
    config.auto_reload = 0;
    timer_init(TIMER_GROUP_0, timer_idx, &config);

    timer_set_counter_value(TIMER_GROUP_0, timer_idx, 0);

    timer_enable_intr(TIMER_GROUP_0, timer_idx);
    timer_isr_register(TIMER_GROUP_0, timer_idx, timer_group0_isr, (void *)timer_idx,
                       ESP_INTR_FLAG_IRAM, NULL);

    timer_start(TIMER_GROUP_0, timer_idx);

    xTaskCreatePinnedToCore(timer_dispatcher, "timer_dispatcher", 2048, NULL, 5, NULL, APP_CPU_NUM);
}

void tim_set_timer(int delta, cb_t cb) {
    if (delta < 10)
        delta = 10;

    timer_idx_t timer_num = TIMER_0;

    unsigned irqStatus = portENTER_CRITICAL_NESTED();
    TIMERG0.hw_timer[timer_num].update = 1;
    uint64_t alarm_value = ((uint64_t)TIMERG0.hw_timer[timer_num].cnt_high << 32) |
                           (TIMERG0.hw_timer[timer_num].cnt_low);
    alarm_value += delta;
    TIMERG0.hw_timer[timer_num].alarm_high = (uint32_t)(alarm_value >> 32);
    TIMERG0.hw_timer[timer_num].alarm_low = (uint32_t)alarm_value;
    TIMERG0.hw_timer[timer_num].config.alarm_en = 1;
    timer_cb = cb;
    portEXIT_CRITICAL_NESTED(irqStatus);
}

void jd_panic(void) {
    portENTER_CRITICAL_NESTED();
    ets_printf(LOG_FORMAT(E, "panic"), esp_log_timestamp(), "JD");
    ets_printf(LOG_FORMAT(W, "DMESG:\n%s\n"), esp_log_timestamp(), "JD", codalLogStore.buffer);
    DMESG("PANIC");
    abort();
}

static portMUX_TYPE irq = portMUX_INITIALIZER_UNLOCKED;

IRAM_ATTR void target_disable_irq(void) {
    portENTER_CRITICAL(&irq);
}

IRAM_ATTR void target_enable_irq(void) {
    portEXIT_CRITICAL(&irq);
}

IRAM_ATTR void target_wait_us(uint32_t n) {
    uint64_t end = tim_get_micros() + n;
    while (tim_get_micros() < end)
        ;
}
