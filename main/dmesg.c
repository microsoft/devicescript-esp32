#include "jdesp.h"

#if JD_DMESG_BUFFER_SIZE > 0

extern int int_level;
static uint8_t panic_mode_uart;
void panic_print_char(const char c);
void panic_print_str(const char *str);
void panic_print_dec(int d);

void __real_uart_hal_write_txfifo(void *hal, const uint8_t *buf, uint32_t data_size,
                                  uint32_t *write_size);
void __wrap_uart_hal_write_txfifo(void *hal, const uint8_t *buf, uint32_t data_size,
                                  uint32_t *write_size) {
    __real_uart_hal_write_txfifo(hal, buf, data_size, write_size);
    if (data_size == 1 && panic_mode_uart) {
        jd_usb_panic_print_char(*buf);
        jd_lstore_panic_print_char(*buf);
    }
}

// void __real_panic_restart(void);
void __wrap_panic_restart(void) {
    jd_lstore_panic_flush();
    jd_usb_panic_print_char('\n');
    target_reset(); // this make sure to reset USB connection state on ESP32-C3
    // __real_panic_restart();
}

void panic_dump_dmesg(void) {
    panic_print_str(LOG_COLOR(LOG_COLOR_RED) "\r\nDMESG:\r\n");
    for (unsigned i = 0; i < codalLogStore.ptr; ++i) {
        char c = codalLogStore.buffer[i];
        if (c == '\n')
            panic_print_char('\r');
        panic_print_char(c);
    }
    panic_print_str("END DMESG\r\nInt: ");
    panic_print_dec(int_level);
    panic_print_str("\r\n" LOG_RESET_COLOR);
}

void __real_esp_panic_handler(void *);
void __wrap_esp_panic_handler(void *info) {
    panic_mode_uart = 1;
    jd_usb_panic_start();
    panic_dump_dmesg();
    __real_esp_panic_handler(info);
}

#endif

#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
#define LOGGING_TX_PIN 43
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
#define LOGGING_TX_PIN 21
#endif

#if defined(LOGGING_TX_PIN)
#include "hal/uart_ll.h"

static uart_dev_t *log_uart;
void uart_log_write(const void *data0, unsigned size) {
    if (log_uart) {
        const uint8_t *data = data0;
        while (size > 0) {
            uint16_t fill_len = uart_ll_get_txfifo_len(log_uart);
            int n = fill_len;
            if (n > size)
                n = size;
            uart_ll_write_txfifo(log_uart, data, n);
            data += n;
            size -= n;
            vTaskDelay(1);
        }
    }
}

void uart_log_init(void) {
    int p = dcfg_get_pin("log.pinTX");
    if (p == NO_PIN) {
        DMESG("log.pinTX not set");
    } else {
        DMESG("log.pinTX at GPIO%d", p);
        if (p == LOGGING_TX_PIN) {
            log_uart = &UART0;
        } else {
            DMESG("! only GPIO%d supported for TX", LOGGING_TX_PIN);
        }
    }
}

void uart_log_dmesg(void) {
    static uint32_t dmesg_ptr;

    if (!log_uart)
        return;

    while (uart_ll_get_txfifo_len(log_uart) > 64) {
        uint8_t buf[64];
        int n = jd_dmesg_read(buf, sizeof(buf), &dmesg_ptr);
        if (n > 0) {
            jd_usb_flush_stdout();
            uart_log_write(buf, n);
        } else {
            break;
        }
    }
}

#else
void uart_log_init(void) {}
void uart_log_dmesg(void) {}
void uart_log_write(const void *data0, unsigned size) {}
#endif
