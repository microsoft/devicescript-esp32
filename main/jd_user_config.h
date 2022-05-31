#ifndef JD_USER_CONFIG_H
#define JD_USER_CONFIG_H

#define DEVICE_DMESG_BUFFER_SIZE 4096

#include "dmesg.h"

#define JD_LOG DMESG
#define JD_WR_OVERHEAD 28

#define JD_CLIENT 1

#define LED_R_MULT 250
#define LED_G_MULT 60
#define LED_B_MULT 150

#define PIN_LED_B 6
#define PIN_LED_G 7
#define PIN_LED_R 8

#define PIN_JACDAC 17

#define PIN_PWR_OVERLOAD NO_PIN
#define PIN_PWR_EN 2 // active lo
#define PIN_PWR_FAULT 13

#define JD_RAW_FRAME 1

#define JD_FLASH_PAGE_SIZE 1024

void hf2_send_frame(const void *frame);
#define JD_USB_SEND hf2_send_frame

// probably not so useful on brains...
#define JD_CONFIG_WATCHDOG 0

#define JD_SEND_FRAME_SIZE 1024

#endif
