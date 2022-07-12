#include "jdesp.h"

#if defined(CONFIG_IDF_TARGET_ESP32S2)

#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "interfaces/jd_usb.h"

#define LOG(msg, ...) DMESG("USB: " msg, ##__VA_ARGS__)
#define LOGV(msg, ...) ((void)0)
#undef ERROR
#define ERROR(msg, ...) DMESG("USB-ERROR: " msg, ##__VA_ARGS__)

static const char *descriptor_str[USB_STRING_DESCRIPTOR_ARRAY_SIZE] = {
    // array of pointer to string descriptors
    (char[]){0x09, 0x04},                // 0: is supported language is English (0x0409)
    CONFIG_USB_DESC_MANUFACTURER_STRING, // 1: Manufacturer
    "Jacscript",                         // 2: Product
    "",                                  // 3: Serials -> replaced

#if CONFIG_USB_CDC_ENABLED
    CONFIG_USB_DESC_CDC_STRING, // 4: CDC Interface
#else
    "",
#endif

#if CONFIG_USB_MSC_ENABLED
    CONFIG_USB_DESC_MSC_STRING, // 5: MSC Interface
#else
    "",
#endif

#if CONFIG_USB_HID_ENABLED
    CONFIG_USB_DESC_HID_STRING // 6: HIDs
#else
    "",
#endif
};

static uint8_t usb_connected;

int jd_usb_tx_free_space(void) {
    return tud_cdc_n_write_available(TINYUSB_CDC_ACM_0);
}

// returns 0 on success
int jd_usb_tx(const void *data, unsigned len) {
    if (tud_cdc_n_write_available(TINYUSB_CDC_ACM_0) < len)
        return -2;
    int r = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (uint8_t *)data, len);
    return r == len ? 0 : -1;
}

int jd_usb_tx_flush(void) {
    return tud_cdc_n_write_flush(TINYUSB_CDC_ACM_0) > 0 ? 0 : -1;
}

int jd_usb_rx(void *data, unsigned len) {
    size_t rx_size = 0;
    esp_err_t ret = tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, data, len, &rx_size);
    if (ret >= 0)
        return rx_size;
    return ret;
}

static void on_cdc_rx(int itf0, cdcacm_event_t *event) {
    jd_usb_process_rx();
}

static void on_cdc_line_state_changed(int itf, cdcacm_event_t *event) {
    usb_connected = event->line_state_changed_data.dtr && event->line_state_changed_data.rts;
    LOG("connected: %d", usb_connected);
}

void usb_init() {
    LOG("init");
    tinyusb_config_t tusb_cfg;
    memset(&tusb_cfg, 0, sizeof(tusb_cfg));

    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    static char macHex[15];
    macHex[0] = 'J';
    macHex[1] = 'D';
    for (int i = 0; i < 6; ++i) {
        snprintf(macHex + (2 + i * 2), 3, "%02X", mac[i]);
    }
    DMESG("USB serial: %s", macHex);
    descriptor_str[3] = macHex;
    tusb_cfg.string_descriptor = (const char **)descriptor_str;

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    tinyusb_config_cdcacm_t amc_cfg;
    memset(&amc_cfg, 0, sizeof(amc_cfg));
    amc_cfg.usb_dev = TINYUSB_USBDEV_0;
    amc_cfg.cdc_port = TINYUSB_CDC_ACM_0;
    amc_cfg.rx_unread_buf_sz = 64;
    amc_cfg.callback_rx = &on_cdc_rx;
    amc_cfg.callback_line_state_changed = &on_cdc_line_state_changed;
    ESP_ERROR_CHECK(tusb_cdc_acm_init(&amc_cfg));

    LOG("init done");
}

#else

void hf2_init() {}
void hf2_send_frame(const void *ptr) {}

#endif