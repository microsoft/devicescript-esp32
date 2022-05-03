#include "jdesp.h"
#include "services/interfaces/jd_pins.h"
#include "services/interfaces/jd_pwm.h"

#include "driver/ledc.h"

static bool led_inited;
static uint8_t free_ch;

uint8_t pwm_init(uint8_t pin, uint32_t period, uint32_t duty, uint8_t prescaler) {
    if (period != 512)
        hw_panic();

    if (!led_inited) {
        led_inited = 1;
        ledc_fade_func_install(0);
        ledc_timer_config_t ledc_timer;
        memset(&ledc_timer, 0, sizeof(ledc_timer));
        ledc_timer.duty_resolution = LEDC_TIMER_9_BIT;
        ledc_timer.freq_hz = 50000;
        ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;
        ledc_timer.timer_num = LEDC_TIMER_1;
        ledc_timer.clk_cfg = LEDC_AUTO_CLK;
        CHK(ledc_timer_config(&ledc_timer));
    }

    ledc_channel_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.channel = ++free_ch;
    cfg.gpio_num = pin;
    cfg.timer_sel = LEDC_TIMER_1;
    cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    cfg.duty = duty;
    CHK(ledc_channel_config(&cfg));
    return cfg.channel;
}

void pwm_set_duty(uint8_t pwm_id, uint32_t duty) {
    CHK(ledc_set_duty_with_hpoint(LEDC_LOW_SPEED_MODE, pwm_id, duty, 0));
    CHK(ledc_update_duty(LEDC_LOW_SPEED_MODE, pwm_id));
}

void pwm_enable(uint8_t pwm_id, bool enabled) {
    // not implemented
}

void pin_set(int pin, int v) {
    if ((uint8_t)pin != NO_PIN)
        gpio_set_level(pin, v);
}

void pwr_enter_no_sleep(void) {}
void pwr_enter_tim(void) {}
void pwr_leave_tim(void) {}
