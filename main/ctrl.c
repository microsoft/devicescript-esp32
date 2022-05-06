#include "jdesp.h"

const char app_dev_class_name[] = "ESP32 Jacscript 48 v0.2";
#define DEV_CLASS 0x3de1398b

const char app_fw_version[] = "0.0.0";

uint32_t app_get_device_class(void) {
    return DEV_CLASS;
}
