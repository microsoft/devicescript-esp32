#include "jdesp.h"
#include "interfaces/jd_usb.h"
#include "esp_mac.h"

#define LOG(msg, ...) DMESG("USB: " msg, ##__VA_ARGS__)
#define LOGV(msg, ...) ((void)0)
#undef ERROR
#define ERROR(msg, ...) DMESG("USB-ERROR: " msg, ##__VA_ARGS__)

#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)

#include "tinyusb.h"
#include "tusb_cdc_acm.h"

static const char *descriptor_str[USB_STRING_DESCRIPTOR_ARRAY_SIZE] = {
    // array of pointer to string descriptors
    (char[]){0x09, 0x04},                    // 0: is supported language is English (0x0409)
    CONFIG_TINYUSB_DESC_MANUFACTURER_STRING, // 1: Manufacturer
    "DeviceScript",                          // 2: Product
    "",                                      // 3: Serials -> replaced

#if CONFIG_TINYUSB_CDC_ENABLED
    CONFIG_TINYUSB_DESC_CDC_STRING, // 4: CDC Interface
#else
    "",
#endif

#if CONFIG_TINYUSB_MSC_ENABLED
    CONFIG_TINYUSB_DESC_MSC_STRING, // 5: MSC Interface
#else
    "",
#endif

#if CONFIG_TINYUSB_HID_ENABLED
    CONFIG_TINYUSB_DESC_HID_STRING // 6: HIDs
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

void usb_pre_init(void) {}

void usb_init(void) {
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

#elif defined(CONFIG_IDF_TARGET_ESP32C3)

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
void usb_pre_init(void) {
    phy_bbpll_en_usb(true);
}

void usb_init(void) {
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

#include "driver/uart.h"
#include "esp_private/periph_ctrl.h"
#include "hal/uart_ll.h"
#include "hal/gpio_ll.h"

// https://www.mischianti.org/2021/02/17/doit-esp32-dev-kit-v1-high-resolution-pinout-and-specs/
// https://randomnerdtutorials.com/esp32-pinout-reference-gpios/

#define TX_PIN 1
#define RX_PIN 3

static uart_dev_t *uart_hw;

static JD_FAST void fill_buffer(void) {
    int space = UART_FIFO_LEN - uart_hw->status.txfifo_cnt;
    if (space < 64)
        return;

    uint8_t buf[64];
    int len = jd_usb_pull(buf);
    if (len) {
        uart_ll_write_txfifo(uart_hw, buf, len);
        uart_hw->int_clr.txfifo_empty = 1;
        uart_hw->conf1.txfifo_empty_thrhd = 32;
        uart_hw->int_ena.txfifo_empty = 1;
    } else {
        uart_hw->int_clr.txfifo_empty = 1;
        uart_hw->int_ena.txfifo_empty = 0;
    }
}

static JD_FAST void read_fifo(void) {
    unsigned n;
    uint8_t buf[64];
    while (0 != (n = uart_hw->status.rxfifo_cnt)) {
        if (n > 64)
            n = 64;
        uart_ll_read_rxfifo(uart_hw, buf, n);
        jd_usb_push(buf, n);
    }
}

static JD_FAST void uart_isr(void *dummy) {
    uint32_t uart_intr_status = uart_hw->int_st.val;

    read_fifo();
    fill_buffer();

    uart_hw->int_clr.val = uart_intr_status; // clear all
}

void jd_usb_pull_ready(void) {
    target_disable_irq();
    fill_buffer();
    target_enable_irq();
}

void usb_pre_init(void) {}
void usb_init(void) {
    int uart_idx = 1;
    uart_hw = UART_LL_GET_HW(uart_idx);

    periph_module_enable(uart_periph_signal[uart_idx].module);
    const uart_config_t uart_config = {.baud_rate = 1500000,
                                       .data_bits = UART_DATA_8_BITS,
                                       .parity = UART_PARITY_DISABLE,
                                       .stop_bits = UART_STOP_BITS_1,
                                       .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                                       .source_clk = UART_SCLK_DEFAULT};
    CHK(uart_param_config(uart_idx, &uart_config));
    intr_handle_t intr_handle;
    CHK(esp_intr_alloc(uart_periph_signal[uart_idx].irq, 0, uart_isr, NULL, &intr_handle));

    CHK(uart_set_pin(uart_idx, TX_PIN, RX_PIN, -1, -1));

    uart_intr_config_t uart_intr = {
        .intr_enable_mask = UART_INTR_RXFIFO_TOUT | UART_INTR_TXFIFO_EMPTY | UART_INTR_RXFIFO_FULL,
        .rxfifo_full_thresh = 64,
        .rx_timeout_thresh = 30, // us
        .txfifo_empty_intr_thresh = 32};
    CHK(uart_intr_config(uart_idx, &uart_intr));
}

void jd_usb_process(void) {
    read_fifo();
    // try to fill the output buffer just in case
    jd_usb_pull_ready();
}

#else
#error "unknown target"
#endif
