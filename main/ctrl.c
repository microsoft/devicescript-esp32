#include "jdesp.h"

static uint8_t id_counter;
static uint32_t nextblink;

static void identify(void) {
    if (!id_counter)
        return;
    if (!jd_should_sample(&nextblink, 150000))
        return;

    id_counter--;
    led_blink(50000);
}

void ctrl_process() {
    identify();
}

static void send_value(jd_packet_t *pkt, uint32_t v) {
    jd_send(JD_SERVICE_NUMBER_CTRL, pkt->service_command, &v, sizeof(v));
}

const char app_dev_class_name[] = "JD-ESP v1.0";
#define DEV_CLASS 0x324f362a

void app_queue_annouce(void);

void ctrl_handle_packet(jd_packet_t *pkt) {
    switch (pkt->service_command) {
    case JD_CMD_ADVERTISEMENT_DATA:
        app_queue_annouce();
        break;

    case JD_CMD_CTRL_IDENTIFY:
        id_counter = 7;
        nextblink = now;
        identify();
        break;

    case JD_CMD_CTRL_RESET:
        esp_restart();
        break;

    case (JD_CMD_GET_REG | JD_REG_CTRL_DEVICE_DESCRIPTION):
        jd_send(JD_SERVICE_NUMBER_CTRL, pkt->service_command, app_dev_class_name,
                 strlen(app_dev_class_name));
        break;

    case (JD_CMD_GET_REG | JD_REG_CTRL_DEVICE_CLASS):
        send_value(pkt, DEV_CLASS);
        break;

    case (JD_CMD_GET_REG | JD_REG_CTRL_BL_DEVICE_CLASS):
        send_value(pkt, DEV_CLASS);
        break;
    }
}
