#include "dmesg.h"

#if DEVICE_DMESG_BUFFER_SIZE > 0

struct CodalLogStore codalLogStore;

/**
 * Performs an in buffer reverse of a given char array.
 *
 * @param s the string to reverse.
 *
 * @return DEVICE_OK, or DEVICE_INVALID_PARAMETER.
 */
int string_reverse(char *s) {
    // sanity check...
    if (s == NULL)
        return -1;

    char *j;
    int c;

    j = s + strlen(s) - 1;

    while (s < j) {
        c = *s;
        *s++ = *j;
        *j-- = c;
    }

    return 0;
}

/**
 * Converts a given integer into a string representation.
 *
 * @param n The number to convert.
 *
 * @param s A pointer to the buffer where the resulting string will be stored.
 *
 * @return DEVICE_OK, or DEVICE_INVALID_PARAMETER.
 */
int myitoa(int n, char *s) {
    int i = 0;
    int positive = (n >= 0);

    if (s == NULL)
        return -1;

    // Record the sign of the number,
    // Ensure our working value is positive.
    unsigned k = positive ? n : -n;

    // Calculate each character, starting with the LSB.
    do {
        s[i++] = (k % 10) + '0';
    } while ((k /= 10) > 0);

    // Add a negative sign as needed
    if (!positive)
        s[i++] = '-';

    // Terminate the string.
    s[i] = '\0';

    // Flip the order.
    string_reverse(s);

    return 0;
}



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

static void writeNum(char *buf, uint32_t n, bool full) {
    int i = 0;
    int sh = 28;
    while (sh >= 0) {
        int d = (n >> sh) & 0xf;
        if (full || d || sh == 0 || i) {
            buf[i++] = d > 9 ? 'A' + d - 10 : '0' + d;
        }
        sh -= 4;
    }
    buf[i] = 0;
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
    codal_vsprintf(tmp, sizeof(tmp) - 1, format, ap);
    int len = strlen(tmp);
    tmp[len] = '\n';
    tmp[len + 1] = 0;
    logwriten(tmp, len + 1);
}

#define WRITEN(p, sz_)                                                                             \
    do {                                                                                           \
        sz = sz_;                                                                                  \
        ptr += sz;                                                                                 \
        if (ptr < dstsize) {                                                                       \
            memcpy(dst + ptr - sz, p, sz);                                                         \
            dst[ptr] = 0;                                                                          \
        }                                                                                          \
    } while (0)

int codal_vsprintf(char *dst, unsigned dstsize, const char *format, va_list ap) {
    const char *end = format;
    unsigned ptr = 0, sz;
    char buf[16];

    for (;;) {
        char c = *end++;
        if (c == 0 || c == '%') {
            if (format != end)
                WRITEN(format, end - format - 1);
            if (c == 0)
                break;

            uint32_t val = va_arg(ap, uint32_t);
            c = *end++;
            buf[1] = 0;
            switch (c) {
            case 'c':
                buf[0] = val;
                break;
            case 'd':
                myitoa(val, buf);
                break;
            case 'x':
            case 'p':
            case 'X':
                buf[0] = '0';
                buf[1] = 'x';
                writeNum(buf + 2, val, c != 'x');
                break;
            case 's':
                WRITEN((char *)(void *)val, strlen((char *)(void *)val));
                buf[0] = 0;
                break;
            case '%':
                buf[0] = c;
                break;
            default:
                buf[0] = '?';
                break;
            }
            format = end;
            WRITEN(buf, strlen(buf));
        }
    }

    return ptr;
}

#endif
