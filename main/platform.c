#include "jdesp.h"

#include "esp_timer.h"
#include "esp_event.h"
#include "esp_private/system_internal.h"

int jd_pin_num(void) {
    return PIN_JACDAC;
}

uint64_t hw_device_id(void) {
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
    void *r = calloc(size, 1);
    if (r == NULL) {
        DMESG("OOM!");
        ESP_LOGE("JD", "OOM %d bytes\n", size);
        JD_PANIC();
    }
    return r;
}

void jd_free(void *ptr) {
    free(ptr);
}

void *jd_alloc_emergency_area(uint32_t size) {
    return calloc(size, 1);
}

void target_reset() {
    ESP_LOGE("JD", "target_reset()\n");
    esp_restart_noos_dig();
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
    portENTER_CRITICAL_ISR(&global_int_mux);
    int_level++;
}

IRAM_ATTR void target_enable_irq() {
    int_level--;
    portEXIT_CRITICAL_ISR(&global_int_mux);
}

void hw_panic(void) {
    DMESG("HW PANIC!");
    abort();
}

void reboot_to_uf2(void) {
    ESP_LOGE("JD", "reset to UF2\n");

#if CONFIG_IDF_TARGET_ESP32S2
    // call esp_reset_reason() is required for idf.py to properly links esp_reset_reason_set_hint()
    (void)esp_reset_reason();
    esp_reset_reason_set_hint((esp_reset_reason_t)0x11F2);
#endif

    esp_restart_noos_dig();
}

void jd_crypto_get_random(uint8_t *buf, unsigned size) {
    esp_fill_random(buf, size);
}

extern const char app_fw_version[];
extern const char app_dev_class_name[];

const char *app_get_fw_version(void) {
    return app_fw_version;
}