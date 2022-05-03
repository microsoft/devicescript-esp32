#include "jdesp.h"

static xQueueHandle frame_queue;

void jd_rx_init(void) {
    frame_queue = xQueueCreate(10, sizeof(jd_frame_t *));
}

int jd_rx_frame_received(jd_frame_t *frame) {
#ifdef JD_SERVICES_PROCESS_FRAME_PRE
    JD_SERVICES_PROCESS_FRAME_PRE(frame);
#endif
    if (!frame)
        return 0;

    jd_frame_t *copy = malloc(JD_FRAME_SIZE(frame));
    memcpy(copy, frame, JD_FRAME_SIZE(frame));
    if (!xQueueSendToBackFromISR(frame_queue, &copy, 0)) {
        free(copy);
        return -1;
    }
    return 0;
}

bool jd_rx_has_frame(void) {
    return !xQueueIsQueueEmptyFromISR(frame_queue);
}

jd_frame_t *jd_rx_get_frame(void) {
    jd_frame_t *fr;
    if (xQueueReceive(frame_queue, &fr, 0)) {
        return fr;
    } else {
        return NULL;
    }
}

void jd_rx_release_frame(jd_frame_t *frame) {
    free(frame);
}
