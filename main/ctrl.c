#include "jdesp.h"

const char app_dev_class_name[] = "JD-ESP v1.0";
#define DEV_CLASS 0x324f362a

const char app_fw_version[] = "0.0.0";

uint32_t app_get_device_class(void) {
    return DEV_CLASS;
}
