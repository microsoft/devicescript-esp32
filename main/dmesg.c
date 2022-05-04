#include "jdesp.h"

#if DEVICE_DMESG_BUFFER_SIZE > 0

struct CodalLogStore codalLogStore;

static void logwriten(const char *msg, int l) {
    target_disable_irq();
    if (codalLogStore.ptr + l >= sizeof(codalLogStore.buffer)) {
#if 1
        codalLogStore.buffer[0] = '.';
        codalLogStore.buffer[1] = '.';
        codalLogStore.buffer[2] = '.';
        codalLogStore.ptr = 3;
#else
        // this messes with timings too much
        const int jump = sizeof(codalLogStore.buffer) / 4;
        codalLogStore.ptr -= jump;
        memmove(codalLogStore.buffer, codalLogStore.buffer + jump, codalLogStore.ptr);
        // zero-out the rest so it looks OK in the debugger
        memset(codalLogStore.buffer + codalLogStore.ptr, 0,
               sizeof(codalLogStore.buffer) - codalLogStore.ptr);
#endif
    }
    if (l + codalLogStore.ptr >= sizeof(codalLogStore.buffer))
        return; // shouldn't happen
    memcpy(codalLogStore.buffer + codalLogStore.ptr, msg, l);
    codalLogStore.ptr += l;
    codalLogStore.buffer[codalLogStore.ptr] = 0;
    target_enable_irq();
}

void codal_dmesg(const char *format, ...) {
    va_list arg;
    va_start(arg, format);
    codal_vdmesg(format, arg);
    va_end(arg);
}

void codal_dmesgf(const char *format, ...) {
    va_list arg;
    va_start(arg, format);
    codal_vdmesg(format, arg);
    va_end(arg);
    codal_dmesg_flush();
}

void codal_vdmesg(const char *format, va_list ap) {
    char tmp[80];
    jd_vsprintf(tmp, sizeof(tmp) - 1, format, ap);
    int len = strlen(tmp);
    tmp[len] = '\n';
    tmp[len + 1] = 0;
    logwriten(tmp, len + 1);
}

#endif

#if 1

extern int int_level;
void panic_print_char(const char c);
void panic_print_str(const char *str);
void panic_print_dec(int d);
void user_panic_handler() {
    panic_print_str("\r\nDMESG:\r\n");
    for (unsigned i = 0; i < codalLogStore.ptr; ++i) {
        char c = codalLogStore.buffer[i];
        if (c == '\n')
            panic_print_char('\r');
        panic_print_char(c);
    }
    panic_print_str("END DMESG\r\nInt: ");
    panic_print_dec(int_level);
    panic_print_str("\r\n");
}

#endif