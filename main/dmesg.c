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
        jd_usb_write_serial(buf, data_size);
        if (data_size == 1 && buf[0] == '\n')
            jd_usb_panic_flush();
        jd_lstore_panic_print_char(*buf);
    }
}

void __real_panic_restart(void);
void __wrap_panic_restart(void) {
    jd_lstore_panic_flush();
    jd_usb_panic_flush();
    __real_panic_restart();
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
    jd_usb_panic_enter();
    panic_dump_dmesg();
    jd_usb_panic_flush();
    __real_esp_panic_handler(info);
}

#endif
