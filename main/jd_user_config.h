#ifndef JD_USER_CONFIG_H
#define JD_USER_CONFIG_H

#define DEVICE_DMESG_BUFFER_SIZE 4096

#include "dmesg.h"

#define JD_LOG DMESG
#define JD_WR_OVERHEAD 28

#define JD_CLIENT 0

#define LED_R_MULT 250
#define LED_G_MULT 76
#define LED_B_MULT 221

#define PIN_LED_B 6
#define PIN_LED_G 7
#define PIN_LED_R 8

#define PIN_JACDAC 17

#define PIN_PWR_OVERLOAD NO_PIN
#define PIN_PWR_EN 2 // active lo
#define PIN_PWR_FAULT 13

#define JD_RAW_FRAME 1

#endif
