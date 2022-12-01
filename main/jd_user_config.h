#ifndef JD_USER_CONFIG_H
#define JD_USER_CONFIG_H

#define DEVICE_DMESG_BUFFER_SIZE 4096

#include "dmesg.h"

#define JD_LOG DMESG
#define JD_WR_OVERHEAD 28

#ifndef NO_JACSCRIPT
#define JD_CLIENT 1
#endif

#define JD_MS_TIMER 1
#define JD_FREE_SUPPORTED 1
#define JD_ADVANCED_STRING 1
#define JD_LSTORE 1

#ifndef __XTENSA__

// ESP32-C3

void led_set_rgb(uint8_t r, uint8_t g, uint8_t b);
#define LED_SET_RGB led_set_rgb
#define PIN_WS2812B 2

#define PIN_JACDAC 3 // A1

#define PIN_SD_MISO 8
#define PIN_SD_MOSI 7
#define PIN_SD_SCK 10
#define PIN_SD_CS 4 // A0

#define PIN_SDA 5
#define PIN_SCL 6

#define PIN_UART_TX 21
#define PIN_UART_RX 20

#if PIN_SD_CS == 9
// GPIO9 is boot pin, connected to button; avoid the MCU driving the pin high, while button pulls it low
#define JD_SD_CS_PULL_UP 1
#endif

#else

// ESP32-S2

#define LED_R_MULT 250
#define LED_G_MULT 60
#define LED_B_MULT 150

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


void jdesp_wake_main(void);
#define JD_WAKE_MAIN() jdesp_wake_main()

#define JD_SIMPLE_ALLOC 0
#define JD_NET_BRIDGE 1
#define JD_FLASH_IN_SETTINGS 1

#define JD_GC_KB 8

#define JD_SEND_FRAME_SIZE 1024
#define JD_RX_QUEUE_SIZE 1024

#endif
