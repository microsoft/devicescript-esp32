#include "jdesp.h"

static xQueueHandle frame_queue;

void jd_rx_init(void) {
    frame_queue = xQueueCreate(32, sizeof(jd_frame_t *));
}

static int jd_rx_frame_received_core(jd_frame_t *frame, int is_loop) {
#ifdef JD_SERVICES_PROCESS_FRAME_PRE
    JD_SERVICES_PROCESS_FRAME_PRE(frame);
#endif
    if (!frame)
        return 0;

    jd_frame_t *copy = malloc(JD_FRAME_SIZE(frame));
    if (!copy)
        return -1;

    memcpy(copy, frame, JD_FRAME_SIZE(frame));

    if (!is_loop)
        hf2_send_frame(copy);

    if (!xQueueSendToBackFromISR(frame_queue, &copy, 0)) {
        free(copy);
        return -1;
    }
    return 0;
}

int jd_rx_frame_received_loopback(jd_frame_t *frame) {
    return jd_rx_frame_received_core(frame, 1);
}

int jd_rx_frame_received(jd_frame_t *frame) {
    return jd_rx_frame_received_core(frame, 0);
}

bool jd_rx_has_frame(void) {
    return !xQueueIsQueueEmptyFromISR(frame_queue);
}

jd_frame_t *jd_rx_get_frame(void) {
    jd_frame_t *fr;
    if (xQueueReceiveFromISR(frame_queue, &fr, 0)) {
        // DMESG("OUT %p sz=%d",fr,((jd_packet_t*)fr)->service_size);
        return fr;
    } else {
        return NULL;
    }
}

void jd_rx_release_frame(jd_frame_t *frame) {
    // DMESG("FREE %p sz=%d",frame,((jd_packet_t*)frame)->service_size);
    free(frame);
}
