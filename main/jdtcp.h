#pragma once

#include "jdprotocol.h"

#define JD_SERVICE_CLASS_WIFI 0x18aae1fa
#define JDWIFI_CMD_SCAN 0x80
#define JDWIFI_CMD_CONNECT 0x81
#define JDWIFI_CMD_DISCONNECT 0x82

#define JDWIFI_EV_GOT_IP 0x01
#define JDWIFI_EV_LOST_IP 0x02

#define JDWIFI_SCAN_FLAG_PASSWORD 0x0001
#define JDWIFI_SCAN_FLAG_WPS 0x0002
#define JDWIFI_SCAN_FLAG_SECONDARY_CHANNEL_ABOVE 0x0004
#define JDWIFI_SCAN_FLAG_SECONDARY_CHANNEL_BELOW 0x0008
#define JDWIFI_SCAN_FLAG_802_11B 0x0100
#define JDWIFI_SCAN_FLAG_802_11A 0x0200
#define JDWIFI_SCAN_FLAG_802_11G 0x0400
#define JDWIFI_SCAN_FLAG_802_11N 0x0800
#define JDWIFI_SCAN_FLAG_802_11AC 0x1000
#define JDWIFI_SCAN_FLAG_802_11AX 0x2000
#define JDWIFI_SCAN_FLAG_802_LONG_RANGE 0x8000

#define JDWIFI_SCAN_ENTRY_HEADER_SIZE 16
struct _jdwifi_scan_entry_t {
    uint32_t flags;
    uint32_t reserved;
    int8_t rssi;
    uint8_t channel;
    uint8_t bssid[6]; // MAC of AP
    uint8_t ssid[33]; // NUL-terminated
} JD_PACKED;
typedef struct _jdwifi_scan_entry_t jdwifi_scan_entry_t;

#define JD_SERVICE_CLASS_TCP 0x1b43b70b
#define JDTCP_CMD_OPEN 0x80

