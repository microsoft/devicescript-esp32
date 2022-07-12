#ifndef JD_USER_CONFIG_H
#define JD_USER_CONFIG_H

#define DEVICE_DMESG_BUFFER_SIZE 4096

#include "dmesg.h"
#include "sdkconfig.h"

#define JD_LOG DMESG
#define JD_WR_OVERHEAD 28

#define JD_CLIENT 1

#define LED_R_MULT 250
#define LED_G_MULT 60
#define LED_B_MULT 150

#if defined(CONFIG_IDF_TARGET_ESP32C3)

#define PIN_LED_B 3
#define PIN_LED_G 1
#define PIN_LED_R 0

#define PIN_JACDAC 4

#define PIN_SD_MISO 8
#define PIN_SD_MOSI 7
#define PIN_SD_SCK 10
#define PIN_SD_CS 20

#else

#define PIN_LED_B 6
#define PIN_LED_G 7
#define PIN_LED_R 8

#define PIN_JACDAC 17

#define PIN_PWR_EN 2 // active lo
#define PIN_PWR_FAULT 13

// only pins 32+ are supported currently
#define PIN_SD_MISO 37
#define PIN_SD_MOSI 35
#define PIN_SD_SCK 36
#define PIN_SD_CS 38
#endif

#define JD_RAW_FRAME 1

#define JD_FLASH_PAGE_SIZE 1024

#define JD_USB_BRIDGE 1

// probably not so useful on brains...
#define JD_CONFIG_WATCHDOG 0

#define JD_SEND_FRAME_SIZE 1024

#endif
