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

static volatile uint8_t fq_scheduled;

static void fill_queue(void *dummy) {
    fq_scheduled = 0;
    while (tud_cdc_n_write_available(TINYUSB_CDC_ACM_0) >= 64) {
        uint8_t buf[64];
        int sz = jd_usb_pull(buf);
        if (sz > 0) {
            tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, buf, sz);
            tud_cdc_n_write_flush(TINYUSB_CDC_ACM_0);
        } else {
            break;
        }
    }
}

void jd_usb_pull_ready(void) {
    if (!fq_scheduled) {
        fq_scheduled = 1;
        tim_worker_run(fill_queue, NULL);
    }
}

void tud_cdc_tx_complete_cb(uint8_t itf) {
    jd_usb_pull_ready();
}

static void on_cdc_rx(int itf0, cdcacm_event_t *event) {
    uint8_t buf[64];
    for (;;) {
        size_t rx_size = 0;
        esp_err_t ret = tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, buf, sizeof(buf), &rx_size);
        if (ret >= 0 && rx_size > 0) {
            jd_usb_push(buf, rx_size);
        } else {
            break;
        }
    }
    JD_WAKE_MAIN();
}

static void on_cdc_line_state_changed(int itf, cdcacm_event_t *event) {
    usb_connected = event->line_state_changed_data.dtr && event->line_state_changed_data.rts;
    LOG("connected: %d", usb_connected);
}

void usb_pre_init() {}

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

#elif defined(CONFIG_IDF_TARGET_ESP32S2)

#include "hal/usb_serial_jtag_ll.h"
#include "soc/periph_defs.h"

static void fill_buffer(void) {
    uint8_t buf[64];
    int len = jd_usb_pull(buf);
    if (len > 0) {
        usb_serial_jtag_ll_write_txfifo(buf, len);
        usb_serial_jtag_ll_txfifo_flush();
        usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
        usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
    } else {
        usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
        usb_serial_jtag_ll_disable_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY);
    }
}

void jd_usb_pull_ready(void) {
    target_disable_irq();
    if (usb_serial_jtag_ll_txfifo_writable())
        fill_buffer();
    target_enable_irq();
}

static void usb_serial_jtag_isr_handler(void *arg) {
    uint32_t st = usb_serial_jtag_ll_get_intsts_mask();

    if (st & USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY) {
        if (usb_serial_jtag_ll_txfifo_writable())
            fill_buffer();
        else
            usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_SERIAL_IN_EMPTY); // ???
    }

    if (st & USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT) {
        uint8_t buf[64];
        int r = usb_serial_jtag_ll_read_rxfifo(buf, sizeof(buf));
        usb_serial_jtag_ll_clr_intsts_mask(USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT);
        usb_serial_jtag_ll_ena_intr_mask(USB_SERIAL_JTAG_INTR_SERIAL_OUT_RECV_PKT);
        if (r)
            jd_usb_push(buf, r);
    }
}

void phy_bbpll_en_usb(bool en);
void usb_pre_init() {
    phy_bbpll_en_usb(true);
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

#elif defined(CONFIG_IDF_TARGET_ESP32)

void jd_usb_pull_ready() {}
void usb_pre_init() {}
void usb_init() {}

#else
#error "unknown target"
#endif
