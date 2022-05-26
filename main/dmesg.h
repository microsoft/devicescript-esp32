#ifndef CODAL_DMESG_H
#define CODAL_DMESG_H

#include <stdint.h>
#include <stdarg.h>

#define NOLOG ((void)0)

#if DEVICE_DMESG_BUFFER_SIZE > 0

#if DEVICE_DMESG_BUFFER_SIZE < 256
#error "Too small DMESG buffer"
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct CodalLogStore
{
    uint32_t ptr;
    char buffer[DEVICE_DMESG_BUFFER_SIZE];
};
extern struct CodalLogStore codalLogStore;

/**
  * Log formatted message to an internal buffer.
  *
  * Supported format strings:
  *    %c - single character
  *    %d - decimal number
  *    %x - hexadecimal number (with 0x)
  *    %p - hexadecimal number padded with zeros (and with 0x)
  *    %X - hexadecimal number padded with zeros (and with 0x)
  *    %s - '\0'-terminated string
  *    %% - literal %
  * Typically used via the DMESG() macro.
  *
  * @param format Format string
  *
  * @code
  * uint32_t k;
  * void *ptr;
  * ...
  * DMESG("USB: Error #%d at %X", k, ptr);
  * @endcode
  */
__attribute__((format(printf, 1, 2)))
void codal_dmesg(const char *format, ...);
__attribute__((format(printf, 1, 2)))
void codal_dmesgf(const char *format, ...);

void codal_dmesg_set_flush_fn(void (*fn)(void));
void codal_dmesg_flush(void);

void codal_vdmesg(const char *format, va_list ap);

#define DMESG  codal_dmesg
#define DMESGF  codal_dmesgf

#ifdef __cplusplus
}
#endif

#else

#define DMESG(...) ((void)0)

#endif

#endif
