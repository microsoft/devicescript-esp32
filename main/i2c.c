#include "jdesp.h"
#include "jd_drivers.h"
#include "driver/i2c.h"
#include "hal/i2c_hal.h"

static int i2c_inst_num;

static uint8_t i2c_ok;

int i2c_init_(void) {
    uint8_t sda, scl;

    if (i2c_ok)
        return 0;

    sda = dcfg_get_pin("i2c.pinSDA");
    scl = dcfg_get_pin("i2c.pinSCL");

    if (sda == scl) {
        DMESG("no I2C");
        return -1;
    }

    int khz = dcfg_get_i32("i2c.kHz", 100);

    i2c_inst_num = dcfg_get_i32("i2c.inst", 0);

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = khz * 1000,
        .clk_flags = 0,
    };

    JD_CHK(i2c_param_config(i2c_inst_num, &conf));
    JD_CHK(i2c_set_timeout(i2c_inst_num, I2C_LL_MAX_TIMEOUT - 1));
    JD_CHK(i2c_driver_install(i2c_inst_num, conf.mode, 0, 0, 0));

    i2c_ok = 1;

    DMESG("i2c OK: sda=%d scl=%d %dkHz", sda, scl, khz);

    return 0;
}

#define I2C_TRANS_BUF_MINIMUM_SIZE 200

int i2c_read_ex(uint8_t device_address, void *dst, unsigned len) {
    if (!i2c_ok)
        return -108;

    esp_err_t err = ESP_OK;
    uint8_t buffer[I2C_TRANS_BUF_MINIMUM_SIZE] = {0};

    i2c_cmd_handle_t handle = i2c_cmd_link_create_static(buffer, sizeof(buffer));
    JD_ASSERT(handle != NULL);

    err = i2c_master_start(handle);
    if (err != ESP_OK)
        goto end;

    err = i2c_master_write_byte(handle, device_address << 1 | I2C_MASTER_READ, true);
    if (err != ESP_OK)
        goto end;

    err = i2c_master_read(handle, dst, len, I2C_MASTER_LAST_NACK);
    if (err != ESP_OK)
        goto end;

    i2c_master_stop(handle);

    err = i2c_master_cmd_begin(i2c_inst_num, handle, 2);

end:
    i2c_cmd_link_delete_static(handle);
    return err;
}

int i2c_write_ex2(uint8_t device_address, const void *src, unsigned len, const void *src2,
                  unsigned len2, bool repeated) {
    if (!i2c_ok)
        return -108;

    esp_err_t err = ESP_OK;
    uint8_t buffer[I2C_TRANS_BUF_MINIMUM_SIZE] = {0};

    i2c_cmd_handle_t handle = i2c_cmd_link_create_static(buffer, sizeof(buffer));
    JD_ASSERT(handle != NULL);

    err = i2c_master_start(handle);
    if (err != ESP_OK)
        goto end;

    err = i2c_master_write_byte(handle, device_address << 1 | I2C_MASTER_WRITE, true);
    if (err != ESP_OK)
        goto end;

    err = i2c_master_write(handle, src, len, true);
    if (err != ESP_OK)
        goto end;

    if (len2 != 0) {
        err = i2c_master_write(handle, src2, len2, true);
        if (err != ESP_OK)
            goto end;
    }

    if (!repeated)
        i2c_master_stop(handle);

    err = i2c_master_cmd_begin(i2c_inst_num, handle, 2);

end:
    i2c_cmd_link_delete_static(handle);
    return err;
}
