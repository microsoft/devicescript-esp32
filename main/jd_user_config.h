#ifndef JD_USER_CONFIG_H
#define JD_USER_CONFIG_H

#include <stdint.h>
#include "../build/config/sdkconfig.h"

#define JD_DMESG_BUFFER_SIZE 4096

#define JD_LOG DMESG
#define JD_WR_OVERHEAD 28

#ifndef NO_JACSCRIPT
#define JD_CLIENT 1
#endif

#define JD_MS_TIMER 1
#define JD_FREE_SUPPORTED 1
#define JD_ADVANCED_STRING 1
#define JD_LSTORE 1

#define JD_RAW_FRAME 1

#define JD_FLASH_PAGE_SIZE 1024

#define JD_USB_BRIDGE 1

// probably not so useful on brains...
#define JD_CONFIG_WATCHDOG 0

void jdesp_wake_main(void);
#define JD_WAKE_MAIN() jdesp_wake_main()

#define JD_SIMPLE_ALLOC 0
#define JD_FLASH_IN_SETTINGS 1

#define JD_GC_KB 32

#define JD_I2C_HELPERS 1
#define JD_WIFI 1
#define JD_NET_BRIDGE JD_WIFI

const void *dcfg_base_addr(void);
#define JD_DCFG_BASE_ADDR dcfg_base_addr()

#if CONFIG_IDF_TARGET_ESP32
#define JD_CONFIG_TEMPERATURE 0
#else
#define JD_CONFIG_TEMPERATURE 1
#endif

#define JD_FAST _JD_SECTION_ATTR_IMPL(".iram1", __COUNTER__)
#define _JD_COUNTER_STRINGIFY(COUNTER) #COUNTER
#define _JD_SECTION_ATTR_IMPL(SECTION, COUNTER)                                                    \
    __attribute__((section(SECTION "." _JD_COUNTER_STRINGIFY(COUNTER))))

#define JD_SPI 1


#endif
