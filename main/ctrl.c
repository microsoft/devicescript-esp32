#include "jdesp.h"

#ifdef __XTENSA__
#ifndef NO_JACSCRIPT
const char app_dev_class_name[] = "ESP32-S2 Jacscript 48 v0.2";
#define DEV_CLASS 0x3de1398b
#else
const char app_dev_class_name[] = "ESP32-S2 Cloud Connector 48 v0.2";
#define DEV_CLASS 0x30a3c887
#endif
#else
const char app_dev_class_name[] = "ESP32-C3 Jacscript";
#define DEV_CLASS 0x33e239e5
#endif

uint32_t app_get_device_class(void) {
    return DEV_CLASS;
}
