#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define DEVICE_DMESG_BUFFER_SIZE 4096

#include "dmesg.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_system.h"
#include "esp_log.h"

#include "driver/gpio.h"

#define JD_WR_OVERHEAD 28
