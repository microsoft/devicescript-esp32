#include "jdesp.h"
#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "uf2hid.h"

#define LOG(msg, ...) DMESG("USB: " msg, ##__VA_ARGS__)
#define LOGV(msg, ...) ((void)0)
#undef ERROR
#define ERROR(msg, ...) DMESG("USB-ERROR: " msg, ##__VA_ARGS__)

// 260 bytes needed for biggest JD packets (with overheads)
#define HF2_BUF_SIZE 260

typedef struct {
    uint16_t size;
    uint8_t serial;
    union {
        uint8_t buf[HF2_BUF_SIZE];
        uint32_t buf32[HF2_BUF_SIZE / 4];
        uint16_t buf16[HF2_BUF_SIZE / 2];
        HF2_Command cmd;
        HF2_Response resp;
    };
} HF2_Buffer;

static bool hf2_connected, hf2_jd_enabled;
static HF2_Buffer hf2_pkt;

static const char *descriptor_str[USB_STRING_DESCRIPTOR_ARRAY_SIZE] = {
    // array of pointer to string descriptors
    (char[]){0x09, 0x04},                // 0: is supported language is English (0x0409)
    CONFIG_USB_DESC_MANUFACTURER_STRING, // 1: Manufacturer
    "Jacscript",                         // 2: Product
    "",                                  // 3: Serials -> replaced

#if CONFIG_USB_CDC_ENABLED
    CONFIG_USB_DESC_CDC_STRING, // 4: CDC Interface
#else
    "",
#endif

#if CONFIG_USB_MSC_ENABLED
    CONFIG_USB_DESC_MSC_STRING, // 5: MSC Interface
#else
    "",
#endif

#if CONFIG_USB_HID_ENABLED
    CONFIG_USB_DESC_HID_STRING // 6: HIDs
#else
    "",
#endif
};

static const char *uf2_info() {
    return "ESP32-S2";
}

void reboot_to_uf2(void) {
    esp_restart(); // TODO
}

typedef struct {
    unsigned size;
    uint8_t flag;
    uint8_t data[0];
} BufferEntry;

static void send_buffer_core(void *ent_) {
    BufferEntry *ent = (BufferEntry *)ent_;

    uint32_t buf[64 / 4]; // aligned
    unsigned size = ent->size;
    uint8_t *data = ent->data;

    while (hf2_connected && size > 0) {
        memset(buf + 1, 0, 60);
        int s = 63;
        if (size <= 63) {
            s = size;
            buf[0] = ent->flag;
        } else {
            buf[0] = ent->flag == HF2_FLAG_CMDPKT_LAST ? HF2_FLAG_CMDPKT_BODY : ent->flag;
        }
        buf[0] |= s;
        uint8_t *dst = (uint8_t *)buf;
        dst++;
        memcpy(dst, data, s);
        data = data + s;
        size -= s;

        int i;
        for (i = 0; i < 20; ++i) {
            int r = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, (uint8_t *)buf, sizeof(buf));
            if (r == sizeof(buf))
                break;
            vTaskDelay(1);
        }
        if (i == 20)
            DMESG("CDC write fail");

        // tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0); - prints warnings
    }

    free(ent);
}

static void hf2_send_buffer(uint8_t flag, const void *data, unsigned size, uint32_t prepend) {
    if (!hf2_connected)
        return;

    if (prepend + 1)
        size += 4;

    BufferEntry *ent = (BufferEntry *)malloc(sizeof(BufferEntry) + size);
    ent->size = size;
    ent->flag = flag;
    uint8_t *dst = ent->data;

    if (prepend + 1) {
        memcpy(dst, &prepend, 4);
        dst += 4;
        size -= 4;
    }

    memcpy(dst, data, size);

    if (worker_run(fg_worker, send_buffer_core, ent) != 0)
        DMESG("HF2 queue full");
}

int hf2_send_event(uint32_t evId, const void *data, int size) {
    hf2_send_buffer(HF2_FLAG_CMDPKT_LAST, data, size, evId);
    return 0;
}

int hf2_send_serial(const void *data, int size, int isError) {
    if (!hf2_connected)
        return 0;

    hf2_send_buffer(isError ? HF2_FLAG_SERIAL_ERR : HF2_FLAG_SERIAL_OUT, data, size, -1);

    return 0;
}

void hf2_send_frame(const void *ptr) {
    const jd_frame_t *frame = ptr;
    if (hf2_jd_enabled)
        hf2_send_event(HF2_EV_JDS_PACKET, frame, JD_FRAME_SIZE(frame));
}

static int hf2_send_response(int size) {
    hf2_send_buffer(HF2_FLAG_CMDPKT_LAST, hf2_pkt.buf, 4 + size, -1);
    return 0;
}

static int hf2_send_response_with_data(const void *data, int size) {
    if (size <= (int)sizeof(hf2_pkt.buf) - 4) {
        memcpy(hf2_pkt.resp.data8, data, size);
        return hf2_send_response(size);
    } else {
        hf2_send_buffer(HF2_FLAG_CMDPKT_LAST, data, size, hf2_pkt.resp.eventId);
        return 0;
    }
}

static int hf2_handle_packet(int sz) {
    if (hf2_pkt.serial) {
        // TODO raise some event?
        return 0;
    }

    LOGV("HF2 sz=%d CMD=%x", sz, hf2_pkt.buf32[0]);

    // one has to be careful dealing with these, as they share memory
    HF2_Command *cmd = &hf2_pkt.cmd;
    HF2_Response *resp = &hf2_pkt.resp;

    uint32_t cmdId = cmd->command_id;
    resp->tag = cmd->tag;
    resp->status16 = HF2_STATUS_OK;

    //#define checkDataSize(str, add) assert(sz == 8 + (int)sizeof(cmd->str) + (int)(add))

    // lastExchange = current_time_ms();
    // gotSomePacket = true;

    switch (cmdId) {
    case HF2_CMD_INFO:
        return hf2_send_response_with_data(uf2_info(), strlen(uf2_info()));

    case HF2_CMD_BININFO:
        resp->bininfo.mode = HF2_MODE_USERSPACE;
        resp->bininfo.flash_page_size = 0;
        resp->bininfo.flash_num_pages = 0;
        resp->bininfo.max_message_size = sizeof(hf2_pkt.buf);
        resp->bininfo.uf2_family = 0xbfdd4eee;
        return hf2_send_response(sizeof(resp->bininfo));

    case HF2_CMD_RESET_INTO_APP:
        target_reset();
        break;

    case HF2_CMD_RESET_INTO_BOOTLOADER:
        reboot_to_uf2();
        break;

    case HF2_CMD_DMESG:
        // TODO
        break;

    case HF2_CMD_JDS_CONFIG:
        if (cmd->data8[0]) {
            hf2_jd_enabled = 1;
        } else {
            hf2_jd_enabled = 0;
        }
        return hf2_send_response(0);

    case HF2_CMD_JDS_SEND:
        jd_send_frame((jd_frame_t *)cmd->data8);
        return hf2_send_response(0);

    default:
        // command not understood
        resp->status16 = HF2_STATUS_INVALID_CMD;
        break;
    }

    return hf2_send_response(0);
}

static void hf2_recv(uint8_t buf[64]) {
    uint8_t tag = buf[0];
    if (hf2_pkt.size && (tag & HF2_FLAG_SERIAL_OUT)) {
        ERROR("serial in middle of cmd");
        return;
    }

    int size = tag & HF2_SIZE_MASK;
    if (hf2_pkt.size + size > (int)sizeof(hf2_pkt.buf)) {
        ERROR("hf2_pkt too large");
        return;
    }

    memcpy(hf2_pkt.buf + hf2_pkt.size, buf + 1, size);
    hf2_pkt.size += size;
    tag &= HF2_FLAG_MASK;
    if (tag != HF2_FLAG_CMDPKT_BODY) {
        if (tag == HF2_FLAG_CMDPKT_LAST)
            hf2_pkt.serial = 0;
        else if (tag == HF2_FLAG_SERIAL_OUT)
            hf2_pkt.serial = 1;
        else
            hf2_pkt.serial = 2;
        int sz = hf2_pkt.size;
        hf2_pkt.size = 0;
        hf2_handle_packet(sz);
    }
}

static void on_cdc_rx(int itf0, cdcacm_event_t *event) {
    /* initialization */
    uint8_t buf[CONFIG_USB_CDC_RX_BUFSIZE];
    size_t rx_size = 0;
    tinyusb_cdcacm_itf_t itf = (tinyusb_cdcacm_itf_t)itf0;

    /* read */
    esp_err_t ret = tinyusb_cdcacm_read(itf, buf, CONFIG_USB_CDC_RX_BUFSIZE, &rx_size);
    if (ret == ESP_OK) {
        LOGV("%d (%d)", rx_size, buf[0]);
        hf2_recv(buf);
    } else {
        ERROR("Read error");
    }
}

static void on_cdc_line_state_changed(int itf, cdcacm_event_t *event) {
    hf2_connected = event->line_state_changed_data.dtr && event->line_state_changed_data.rts;
    LOG("connected: %d", hf2_connected);
}

void hf2_init() {
    LOG("init");
    tinyusb_config_t tusb_cfg;
    memset(&tusb_cfg, 0, sizeof(tusb_cfg));

    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    static char macHex[15];
    macHex[0] = 'J';
    macHex[1] = 'D';
    for (int i = 0; i < 6; ++i) {
        snprintf(macHex + (2 + i * 2), 3, "%02X", mac[i]);
    }
    DMESG("USB serial: %s", macHex);
    descriptor_str[3] = macHex;
    tusb_cfg.string_descriptor = (char **)descriptor_str;

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));

    tinyusb_config_cdcacm_t amc_cfg;
    memset(&amc_cfg, 0, sizeof(amc_cfg));
    amc_cfg.usb_dev = TINYUSB_USBDEV_0;
    amc_cfg.cdc_port = TINYUSB_CDC_ACM_0;
    amc_cfg.rx_unread_buf_sz = 64;
    amc_cfg.callback_rx = &on_cdc_rx;
    amc_cfg.callback_line_state_changed = &on_cdc_line_state_changed;
    ESP_ERROR_CHECK(tusb_cdc_acm_init(&amc_cfg));

    LOG("init done");
}
