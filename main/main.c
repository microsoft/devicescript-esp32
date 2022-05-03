#include "jdesp.h"

/*
    export const PIN_JDPWR_OVERLOAD_LED = PIN_LED_R | DAL.CFG_PIN_CONFIG_ACTIVE_LO
    export const PIN_JDPWR_ENABLE = PIN_P2 | DAL.CFG_PIN_CONFIG_ACTIVE_LO // ILIM_ENABLE
    export const PIN_JDPWR_FAULT = PIN_P13 // ILIM_FAULT
*/

static uint64_t led_off_time;

void setup_output(int pin) {
    gpio_set_direction(pin, GPIO_MODE_OUTPUT);
    PIN_FUNC_SELECT(GPIO_PIN_MUX_REG[pin], PIN_FUNC_GPIO);
}

static void setup_pins(void) {
    setup_output(PIN_LED_R);
    setup_output(PIN_LED_G);
    setup_output(PIN_LED_B);
}

void led_set(int state) {
    gpio_set_level(PIN_LED_B, !state);
}

void led_blink(int us) {
    led_off_time = tim_get_micros() + us;
    led_set(1);
}

int jd_pin_num(void) {
    return 17;
}

static void flush_dmesg(void) {
    char *dmesgCopy = malloc(sizeof(codalLogStore));

    uint32_t len;

    target_disable_irq();
    len = codalLogStore.ptr;
    memcpy(dmesgCopy, codalLogStore.buffer, len);
    codalLogStore.ptr = 0;
    codalLogStore.buffer[0] = 0;
    target_enable_irq();

    if (len) {
        if (dmesgCopy[len - 1] == '\n')
            len--;
        dmesgCopy[len] = 0;
        if (strchr(dmesgCopy, '\n'))
            ESP_LOGI("JD", "DMESG:\n%s", dmesgCopy);
        else
            ESP_LOGI("JD", "DMESG: %s", dmesgCopy);
    }
    free(dmesgCopy);
}

uint32_t now;

static void jdloop(void *_dummy) {
    while (1) {
        int qdelay = 1;

        if (led_off_time) {
            int timeLeft = led_off_time - tim_get_micros();
            if (timeLeft <= 0) {
                led_off_time = 0;
                led_set(0);
            } else if (timeLeft < 10000) {
                qdelay = 0;
            }
        }

        jd_process_everything();

        if (qdelay && !jd_rx_has_frame())
            target_wait_us(10000);

        flush_dmesg();
    }
}

void app_init_services(void) {
    // wifi_init();
    // jdtcp_init();
}

void app_main() {
    ESP_LOGI("JD", "starting...");

    DMESG("app main");

    setup_pins();

    uart_init();
    tim_init();
    jd_init();

    xTaskCreatePinnedToCore(jdloop, "jdloop", 2 * 1024, NULL, 3, NULL, WORKER_CPU);
}

uint64_t jd_device_id(void) {
    static uint64_t addr;
    if (!addr) {
        uint8_t mac[6];
        esp_efuse_mac_get_default(mac);
        addr = ((uint64_t)0xff << 56) | ((uint64_t)mac[5] << 48) | ((uint64_t)mac[4] << 40) |
               ((uint64_t)mac[3] << 32) | ((uint64_t)mac[2] << 24) | ((uint64_t)mac[1] << 16) |
               ((uint64_t)mac[0] << 8) | ((uint64_t)0xfe << 0);
    }
    return addr;
}

void jd_alloc_stack_check(void) {}

void jd_alloc_init(void) {}

void *jd_alloc(uint32_t size) {
    return calloc(size, 1);
}

void jd_free(void *ptr) {
    free(ptr);
}

void *jd_alloc_emergency_area(uint32_t size) {
    return calloc(size, 1);
}

void target_reset() {
    esp_restart();
}

IRAM_ATTR void target_wait_us(uint32_t us) {
    int64_t later = esp_timer_get_time() + us;
    while (esp_timer_get_time() < later) {
        ;
    }
}

static portMUX_TYPE global_int_mux = portMUX_INITIALIZER_UNLOCKED;
int int_level;

IRAM_ATTR void target_disable_irq() {
    vPortEnterCritical(&global_int_mux);
    int_level++;
}

IRAM_ATTR void target_enable_irq() {
    int_level--;
    vPortExitCritical(&global_int_mux);
}

int target_in_irq(void) {
    // TODO?
    return 0;
}

void hw_panic(void) {
    target_disable_irq();
    ESP_LOGI("JD", "HW PANIC!\n");
    flush_dmesg();
    abort();
}