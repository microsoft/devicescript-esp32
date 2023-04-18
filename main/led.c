#include "jdesp.h"

#include "driver/ledc.h"
#include "hal/ledc_ll.h"
#include "hal/gpio_ll.h"
#include "esp_rom_gpio.h"

#define LEDC_TIMER_DIV_NUM_MAX (0x3FFFF)

typedef struct timer_info {
    uint8_t tim_num;
    uint8_t pin;
    uint8_t bits;
    uint32_t div;
    uint32_t period;
} timer_info_t;

static timer_info_t timers[LEDC_TIMER_MAX];

uint8_t cpu_mhz = LEDC_APB_CLK_HZ / 1000000;

static void apply_config(timer_info_t *t) {
    ledc_timer_t tim = t->tim_num;

    ledc_ll_set_clock_divider(&LEDC, LEDC_LOW_SPEED_MODE, tim, t->div);
    ledc_ll_set_clock_source(&LEDC, LEDC_LOW_SPEED_MODE, tim, LEDC_APB_CLK);
    ledc_ll_set_duty_resolution(&LEDC, LEDC_LOW_SPEED_MODE, tim, t->bits);
    ledc_ll_ls_timer_update(&LEDC, LEDC_LOW_SPEED_MODE, tim);

    ledc_ll_timer_rst(&LEDC, LEDC_LOW_SPEED_MODE, tim);
    ledc_ll_ls_timer_update(&LEDC, LEDC_LOW_SPEED_MODE, tim);
}

static void init(void) {
    if (timers[1].tim_num == 1)
        return;
    for (int i = 0; i < LEDC_TIMER_MAX; ++i) {
        timers[i].tim_num = i;
        timers[i].pin = 0xff;
    }
    periph_module_enable(PERIPH_LEDC_MODULE);
    ledc_ll_set_slow_clk_sel(&LEDC, LEDC_SLOW_CLK_APB);
}

uint8_t jd_pwm_init(uint8_t pin, uint32_t period, uint32_t duty, uint8_t prescaler) {
    init();

    uint32_t period_cycles = period * prescaler;
    timer_info_t *t = NULL;

    for (int bits = LEDC_TIMER_BIT_MAX - 1; bits >= 4; bits--) {
        uint32_t div = (period_cycles << 8) >> bits;
        if (div < 256)
            continue;
        if (div > LEDC_TIMER_DIV_NUM_MAX) {
            DMESG("! PWM too fast");
            hw_panic();
        }

        for (unsigned i = 0; i < LEDC_TIMER_MAX; ++i) {
            t = &timers[i];
            if (t->pin == pin)
                break;
            t = NULL;
        }

        if (t == NULL)
            for (unsigned i = 0; i < LEDC_TIMER_MAX; ++i) {
                t = &timers[i];
                if (t->pin == 0xff)
                    break;
                t = NULL;
            }

        if (t == NULL) {
            DMESG("! out of LEDC timers");
            hw_panic();
        }

        t->pin = pin;
        t->div = div;
        t->bits = bits;
        t->period = period;
        apply_config(t);

        break;
    }

    int ch = t->tim_num; // we bind timers to channels 1-1
    int tim = t->tim_num;
    int pwm_id = t->tim_num + 1;

    jd_pwm_set_duty(pwm_id, duty);

    ledc_ll_bind_channel_timer(&LEDC, LEDC_LOW_SPEED_MODE, ch, tim);
    ledc_ll_ls_channel_update(&LEDC, LEDC_LOW_SPEED_MODE, ch);

    jd_pwm_enable(pwm_id, 1);

    return pwm_id;
}

void jd_pwm_set_duty(uint8_t pwm_id, uint32_t duty) {
    JD_ASSERT(pwm_id > 0);
    JD_ASSERT(pwm_id <= LEDC_TIMER_MAX);

    timer_info_t *t = &timers[pwm_id - 1];
    int ch = t->tim_num;

    uint32_t max = 1 << t->bits;
    duty = max * duty / t->period;
    if (duty >= max)
        duty = max - 1;

    ledc_ll_set_duty_int_part(&LEDC, LEDC_LOW_SPEED_MODE, ch, duty);
    ledc_ll_set_duty_direction(&LEDC, LEDC_LOW_SPEED_MODE, ch, 1);
    ledc_ll_set_duty_num(&LEDC, LEDC_LOW_SPEED_MODE, ch, 0);
    ledc_ll_set_duty_cycle(&LEDC, LEDC_LOW_SPEED_MODE, ch, 0);
    ledc_ll_set_duty_scale(&LEDC, LEDC_LOW_SPEED_MODE, ch, 0);
    ledc_ll_ls_channel_update(&LEDC, LEDC_LOW_SPEED_MODE, ch);

    ledc_ll_set_sig_out_en(&LEDC, LEDC_LOW_SPEED_MODE, ch, true);
    ledc_ll_set_duty_start(&LEDC, LEDC_LOW_SPEED_MODE, ch, true);
    ledc_ll_ls_channel_update(&LEDC, LEDC_LOW_SPEED_MODE, ch);
}

void jd_pwm_enable(uint8_t pwm_id, bool enabled) {
    JD_ASSERT(pwm_id > 0);
    JD_ASSERT(pwm_id <= LEDC_TIMER_MAX);

    timer_info_t *t = &timers[pwm_id - 1];
    int ch = t->tim_num;

    pin_setup_output(t->pin);
    if (enabled) {
        bool output_invert = false;
        esp_rom_gpio_connect_out_signal(
            t->pin, ledc_periph_signal[LEDC_LOW_SPEED_MODE].sig_out0_idx + ch, output_invert, 0);
    }
}

void pin_set(int pin, int v) {
    if ((uint8_t)pin != NO_PIN)
        gpio_set_level(pin, v);
}

void pin_setup_output(int pin) {
    if ((uint8_t)pin != NO_PIN) {
        gpio_ll_iomux_func_sel(GPIO_PIN_MUX_REG[pin], PIN_FUNC_GPIO);
        CHK(gpio_set_direction(pin, GPIO_MODE_OUTPUT));
        esp_rom_gpio_connect_out_signal(pin, SIG_GPIO_OUT_IDX, false, false);
    }
}

int pin_get(int pin) {
    if ((uint8_t)pin == NO_PIN)
        return -1;
    return gpio_get_level(pin);
}

void pin_setup_input(int pin, int pull) {
    if ((uint8_t)pin == NO_PIN)
        return;
    gpio_ll_iomux_func_sel(GPIO_PIN_MUX_REG[pin], PIN_FUNC_GPIO);
    pin_set_pull(pin, pull);
    CHK(gpio_set_direction(pin, GPIO_MODE_INPUT));
}

void pin_set_pull(int pin, int pull) {
    if ((uint8_t)pin == NO_PIN)
        return;

    if (pull < 0) {
        gpio_pulldown_en(pin);
        gpio_pullup_dis(pin);
    } else if (pull > 0) {
        gpio_pullup_en(pin);
        gpio_pulldown_dis(pin);
    } else {
        gpio_pullup_dis(pin);
        gpio_pulldown_dis(pin);
    }
}

void pin_setup_analog_input(int pin) {
    if ((uint8_t)pin == NO_PIN)
        return;
    gpio_ll_iomux_func_sel(GPIO_PIN_MUX_REG[pin], PIN_FUNC_GPIO);
    pin_set_pull(pin, PIN_PULL_NONE);
    CHK(gpio_set_direction(pin, GPIO_MODE_DISABLE));
}

void pwr_enter_no_sleep(void) {}
void pwr_enter_tim(void) {}
void pwr_leave_tim(void) {}

void pwr_enter_pll(void) {}
void pwr_leave_pll(void) {}

void power_pin_enable(int en) {}
