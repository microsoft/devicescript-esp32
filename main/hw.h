#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "dmesg.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_system.h"
#include "esp_log.h"

#include "driver/gpio.h"

#if defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32C3)
#define WORKER_CPU PRO_CPU_NUM
#else
#define WORKER_CPU APP_CPU_NUM
#endif
