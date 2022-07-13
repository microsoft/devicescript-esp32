#include "jdesp.h"
#include "interfaces/jd_usb.h"

#define LOG(msg, ...) DMESG("USB: " msg, ##__VA_ARGS__)
#define LOGV(msg, ...) ((void)0)
#undef ERROR
#define ERROR(msg, ...) DMESG("USB-ERROR: " msg, ##__VA_ARGS__)

#if defined(CONFIG_IDF_TARGET_ESP32S2)

#include "tinyusb.h"
#include "tusb_cdc_acm.h"

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
    JD_WAKE_MAIN();
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

#include "hal/usb_serial_jtag_ll.h"
#include "soc/periph_defs.h"

static uint8_t tx_buf[64];
static uint8_t tx_ptr;
static uint8_t usb_disconnected;
static uint32_t usb_wait_start;

int jd_usb_tx_free_space(void) {
    return sizeof(tx_buf) - tx_ptr;
}

int jd_usb_tx(const void *data, unsigned len) {
    int r = 0;
    target_disable_irq();
    if (tx_ptr + len <= sizeof(tx_buf)) {
        memcpy(tx_buf + tx_ptr, data, len);
        tx_ptr += len;
    } else {
        r = -1;
    }
    target_enable_irq();
    return r;
}

int jd_usb_tx_flush(void) {
    int r = 0;
    target_disable_irq();
    if (tx_ptr == 0) {
        r = -1;
    } else if (!usb_serial_jtag_ll_txfifo_writable()) {
        if (usb_disconnected) {
            tx_ptr = 0;
        } else {
            r = -2;
            uint32_t n = tim_get_micros();
            if (!usb_wait_start)
                usb_wait_start = n;
            else if ((n - usb_wait_start) > (64 << 10)) {
                usb_disconnected = 1;
                LOG("disconnected!");
            }
        }
    } else {
        if (usb_disconnected) {
            usb_disconnected = 0;
            LOG("reconnected");
        }
        usb_wait_start = 0;
        usb_serial_jtag_ll_write_txfifo(tx_buf, tx_ptr);
        usb_serial_jtag_ll_txfifo_flush();
        usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
        usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
        tx_ptr = 0;
    }
    target_enable_irq();
    return r;
}

int jd_usb_rx(void *data, unsigned len) {
    JD_ASSERT(len >= 64);
    int r = usb_serial_jtag_ll_read_rxfifo(data, len);
    usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT);
    usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT);
    return r;
}

static void usb_serial_jtag_isr_handler(void *arg) {
    uint32_t st = usb_serial_jtag_ll_get_intsts_mask();

    if (st & USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY) {
        usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
        usb_serial_jtag_ll_disable_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
        JD_WAKE_MAIN();
    }

    if (st & USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT) {
        usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT);
        usb_serial_jtag_ll_disable_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT);
        jd_usb_process_rx();
    }
}

void usb_init() {
    LOG("init");
    usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY |
                                       USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT);
    usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY |
                                     USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT);
    intr_handle_t intr_handle;
    CHK(esp_intr_alloc(ETS_USB_SERIAL_JTAG_INTR_SOURCE, 0, usb_serial_jtag_isr_handler, NULL,
                       &intr_handle));
    LOG("init done");
}

#endif