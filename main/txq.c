#include "jdesp.h"

#define NUM_ENTRIES 5

static xQueueHandle send_queue;
static jd_frame_t *sendFrame;
static portMUX_TYPE sendMux = portMUX_INITIALIZER_UNLOCKED;

#define LOCK() portENTER_CRITICAL(&sendMux)
#define UNLOCK() portEXIT_CRITICAL(&sendMux)

int jd_tx_is_idle() {
    LOCK();
    int r = sendFrame == NULL && xQueueIsQueueEmptyFromISR(send_queue);
    UNLOCK();
    return r;
}

void jd_tx_init(void) {
    if (send_queue)
        return;
    send_queue = xQueueCreate(NUM_ENTRIES, sizeof(jd_frame_t *));
}

void jd_send(unsigned service_num, unsigned service_cmd, const void *data, unsigned service_size) {
    if (service_size > JD_SERIAL_PAYLOAD_SIZE)
        jd_panic();

    uint64_t dev_id = jd_device_id(); // before locking

    void *trg;
    for (;;) {
        LOCK();
        if (sendFrame == NULL) {
            sendFrame = calloc(sizeof(jd_frame_t), 1);
            sendFrame->device_identifier = dev_id;
        }
        trg = jd_push_in_frame(sendFrame, service_num, service_cmd, service_size);
        if (trg)
            memcpy(trg, data, service_size);
        UNLOCK();
        if (trg)
            return;
        jd_tx_flush(); // and try again
    }
}

void jd_send_event_ext(srv_t *srv, uint32_t eventid, uint32_t arg) {
    srv_common_t *state = (srv_common_t *)srv;
    if (eventid >> 16)
        jd_panic();
    uint32_t data[] = {eventid, arg};
    jd_send(state->service_number, JD_CMD_EVENT, data, 8);
}

jd_frame_t *jd_tx_get_frame(void) {
    LOCK();
    jd_frame_t *fr = NULL;
    if (!xQueueReceiveFromISR(send_queue, &fr, NULL))
        fr = NULL;
    UNLOCK();
    return fr;
}

void jd_tx_frame_sent(jd_frame_t *pkt) {
    free(pkt);
    LOCK();
    int empty = xQueueIsQueueEmptyFromISR(send_queue);
    UNLOCK();
    if (!empty)
        jd_packet_ready();
}

int _jd_tx_push_frame(jd_frame_t *f, int wait) {
    if (!f)
        return 0;

    int r = 0;

    if (!xQueueSendToBack(send_queue, &f, wait))
        r = -1;

    if (r) {
        DMESG("dropped TXQ");
        free(f);
    }

    jd_packet_ready(); // even if dropped the frame, there's still something for the JD layer to
                       // chew on

    return r;
}

void jd_tx_flush() {
    LOCK();
    jd_frame_t *f = sendFrame;
    sendFrame = NULL;
    UNLOCK();
    if (!f)
        return;
    jd_compute_crc(f);
    _jd_tx_push_frame(f, 5);
}
