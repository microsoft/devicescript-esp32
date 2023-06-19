#include "jdesp.h"

#include "esp_timer.h"
#include "esp_event.h"
#include "esp_spi_flash.h"
#include "esp_private/system_internal.h"
#include "esp_sleep.h"
#include "esp_random.h"
#include "esp_mac.h"

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

void log_free_mem(void) {
    DMESG("free memory: %u bytes (max block: %u bytes)",
          (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

void *jd_alloc(uint32_t size) {
    void *r = calloc(size, 1);
    if (r == NULL) {
        DMESG("OOM! %u bytes", (unsigned)size);
        log_free_mem();
        ESP_LOGE("JD", "OOM %u bytes\n", (unsigned)size);
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

void target_reset(void) {
    // ESP_LOGE("JD", "target_reset()\n");
    // reset through deep sleep to make sure the C3 USB is disconnected
    esp_sleep_enable_timer_wakeup(20000);
    esp_deep_sleep_start();
    // just in case...
    esp_restart_noos_dig();
}

void target_standby(uint32_t duration_ms) {
    esp_sleep_enable_timer_wakeup(duration_ms * 1000LL);
    esp_deep_sleep_start();
}

IRAM_ATTR void target_wait_us(uint32_t us) {
    int64_t later = esp_timer_get_time() + us;
    while (esp_timer_get_time() < later) {
        ;
    }
}

static portMUX_TYPE global_int_mux = portMUX_INITIALIZER_UNLOCKED;
int int_level;

IRAM_ATTR void target_disable_irq(void) {
    portENTER_CRITICAL_ISR(&global_int_mux);
    int_level++;
}

IRAM_ATTR void target_enable_irq(void) {
    int_level--;
    portEXIT_CRITICAL_ISR(&global_int_mux);
}

void hw_panic(void) {
    DMESG("HW PANIC!");
    abort();
}

void reboot_to_uf2(void) {
    ESP_LOGE("JD", "reset to UF2\n");

#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
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

const void *dcfg_base_addr(void) {
    static const void *result = NULL;
    if (!result) {
        spi_flash_mmap_handle_t map;
        CHK(spi_flash_mmap(0, 0x10000, SPI_FLASH_MMAP_DATA, &result, &map));
        result = (const uint8_t *)result + 0x9000;
    }
    return result;
}
