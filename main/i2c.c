#include "jdesp.h"
#include "jd_drivers.h"
#include "driver/i2c.h"
#include "hal/i2c_hal.h"

#define I2C_MASTER_NUM 0

static uint8_t i2c_ok;

int i2c_init(void) {
    uint8_t sda, scl;

    if (i2c_ok)
        return 0;

    get_i2c_pins(&sda, &scl);
    if (sda == scl) {
        DMESG("no I2C");
        return -1;
    }

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400 * 1000,
        .clk_flags = 0,
    };

    JD_CHK(i2c_param_config(I2C_MASTER_NUM, &conf));
    JD_CHK(i2c_set_timeout(I2C_MASTER_NUM, I2C_LL_MAX_TIMEOUT - 1));
    JD_CHK(i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0));

    i2c_ok = 1;

    return 0;
}

#define I2C_TRANS_BUF_MINIMUM_SIZE 200

int i2c_read_ex(uint8_t device_address, void *dst, unsigned len) {
    if (!i2c_ok)
        return -1;

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

    err = i2c_master_cmd_begin(I2C_MASTER_NUM, handle, 2);

end:
    i2c_cmd_link_delete_static(handle);
    return err;
}

int i2c_write_ex2(uint8_t device_address, const void *src, unsigned len, const void *src2,
                  unsigned len2, bool repeated) {
    if (!i2c_ok)
        return -1;

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

    err = i2c_master_cmd_begin(I2C_MASTER_NUM, handle, 2);

end:
    i2c_cmd_link_delete_static(handle);
    return err;
}

//
// platform-independent code starts
//

#define CHECK_RET(call)                                                                            \
    do {                                                                                           \
        int _r = call;                                                                             \
        if (_r < 0)                                                                                \
            return _r;                                                                             \
    } while (0)

int i2c_write_ex(uint8_t device_address, const void *src, unsigned len, bool repeated) {
    return i2c_write_ex2(device_address, src, len, NULL, 0, repeated);
}

// 8-bit reg addr
int i2c_write_reg_buf(uint8_t addr, uint8_t reg, const void *src, unsigned len) {
    return i2c_write_ex2(addr, &reg, 1, src, len, false);
}

int i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val) {
    return i2c_write_reg_buf(addr, reg, &val, 1);
}

int i2c_read_reg_buf(uint8_t addr, uint8_t reg, void *dst, unsigned len) {
    CHECK_RET(i2c_write_ex(addr, &reg, 1, true) < 0);
    return i2c_read_ex(addr, dst, len);
}

int i2c_read_reg(uint8_t addr, uint8_t reg) {
    uint8_t r = 0;
    CHECK_RET(i2c_read_reg_buf(addr, reg, &r, 1));
    return r;
}

// 16-bit reg addr
int i2c_write_reg16_buf(uint8_t addr, uint16_t reg, const void *src, unsigned len) {
    uint8_t regaddr[2] = {reg >> 8, reg & 0xff};
    return i2c_write_ex2(addr, regaddr, 2, src, len, false);
}

int i2c_write_reg16(uint8_t addr, uint16_t reg, uint8_t val) {
    return i2c_write_reg16_buf(addr, reg, &val, 1);
}

int i2c_read_reg16_buf(uint8_t addr, uint16_t reg, void *dst, unsigned len) {
    uint8_t a[] = {reg >> 8, reg & 0xff};
    CHECK_RET(i2c_write_ex(addr, a, 2, true));
    return i2c_read_ex(addr, dst, len);
}

int i2c_read_reg16(uint8_t addr, uint16_t reg) {
    uint8_t r = 0;
    CHECK_RET(i2c_read_reg16_buf(addr, reg, &r, 1));
    return r;
}