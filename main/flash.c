#include "jdesp.h"
#include "nvs_flash.h"

static nvs_handle_t flash_handle;

static void nvs_init(void) {
    if (!flash_handle) {
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        JD_CHK(ret);
        JD_CHK(nvs_open("jdsett", NVS_READWRITE, &flash_handle));
    }
}

int jd_settings_get_bin(const char *key, void *dst, unsigned space) {
    size_t len;

    nvs_init();

    int err = nvs_get_blob(flash_handle, key, dst, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        return -1;

    if (err == ESP_ERR_NVS_INVALID_LENGTH || err == 0)
        return len;

    JD_PANIC();
}

int jd_settings_set_bin(const char *key, const void *val, unsigned size) {
    nvs_init();
    int r = nvs_set_blob(flash_handle, key, val, size);
    if (r != 0)
        return r;
    return nvs_commit(flash_handle);
}
