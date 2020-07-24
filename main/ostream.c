#include "jdesp.h"

static portMUX_TYPE streamMux = portMUX_INITIALIZER_UNLOCKED;

#define LOCK() portENTER_CRITICAL(&streamMux)
#define UNLOCK() portEXIT_CRITICAL(&streamMux)

static ostream_desc_t *streams;

int _jd_send_frame(jd_frame_t *f, int wait);

static void ostream_free(ostream_desc_t *str) {
    if (!str->sem)
        return;
    if (streams == str) {
        streams = str->next;
    } else {
        for (ostream_desc_t *ss = streams; ss; ss = ss->next)
            if (ss->next == str) {
                ss->next = str->next;
                break;
            }
    }
    if (str->frame) {
        free(str->frame);
        str->frame = NULL;
    }
    vSemaphoreDelete(str->sem);
    str->sem = NULL;
    str->counter = 0;
    str->device_identifier = 0;
}

int ostream_open(ostream_desc_t *str, jd_packet_t *pkt) {
    if (pkt->service_size < sizeof(jd_stream_cmd_t))
        return -1;
    LOCK();
    ostream_free(str);
    str->sem = xSemaphoreCreateMutex();
    str->crc_task = NULL;
    str->crc_value = 0;
    jd_stream_cmd_t *sc = (jd_stream_cmd_t *)pkt->data;
    str->device_identifier = sc->device_identifier;
    str->counter = sc->port_num << JD_STREAM_PORT_SHIFT;
    str->frame = NULL;
    str->next = streams;
    streams = str;
    UNLOCK();
    return 0;
}

void ostream_process_ack(jd_packet_t *pkt) {
    if (pkt->service_number != JD_SERVICE_NUMBER_CRC_ACK)
        return;
    LOCK();
    for (ostream_desc_t *s = streams; s; s = s->next) {
        if (s->crc_task && pkt->service_command == s->crc_value &&
            s->device_identifier == pkt->device_identifier) {
            xTaskNotifyGive(s->crc_task);
        }
    }
    UNLOCK();
}

static int do_flush(ostream_desc_t *str) {
    jd_frame_t *f = str->frame;
    str->frame = NULL;

    int gotAck = 1;
    int delay = 1;
    if (f) {
        jd_compute_crc(f);
        str->crc_value = f->crc;
        str->crc_task = xTaskGetCurrentTaskHandle();

        while (delay < 1500 / portTICK_PERIOD_MS) {
            jd_frame_t *fcp = malloc(sizeof(*f));
            memcpy(fcp, f, sizeof(*f));
            _jd_send_frame(fcp, 5); // this will get free()d when sent
            gotAck = ulTaskNotifyTake(pdTRUE, delay);
            if (gotAck)
                break;
            delay *= 2;
        }

        free(f);

        str->crc_task = NULL;
    }

    return gotAck ? 0 : -1;
}

int ostream_flush(ostream_desc_t *str) {
    if (!str->sem)
        return -2;

    xSemaphoreTake(str->sem, portMAX_DELAY);
    int r = do_flush(str);
    xSemaphoreGive(str->sem);

    // don't try to send close mark when flush closed, as this can lead to infinite recursion
    if (r < 0)
        ostream_free(str);

    return r;
}

static void ostream_write_ex(ostream_desc_t *str, const void *data, unsigned len, int flags) {
    if (!str->sem)
        return;

    xSemaphoreTake(str->sem, portMAX_DELAY);

    void *trg;
    for (;;) {
        if (str->frame == NULL) {
            str->frame = calloc(sizeof(jd_frame_t), 1);
            str->frame->device_identifier = str->device_identifier;
            str->frame->flags = JD_FRAME_FLAG_COMMAND | JD_FRAME_FLAG_ACK_REQUESTED;
        }
        trg = jd_push_in_frame(str->frame, JD_SERVICE_NUMBER_STREAM, str->counter | flags, len);
        if (trg) {
            memcpy(trg, data, len);
            break;
        }
        do_flush(str); // and try again
    }

    str->counter =
        ((str->counter + 1) & JD_STREAM_COUNTER_MASK) | (str->counter & ~JD_STREAM_COUNTER_MASK);

    xSemaphoreGive(str->sem);
}

void ostream_write_meta(ostream_desc_t *str, const void *data, unsigned len) {
    ostream_write_ex(str, data, len, JD_STREAM_METADATA_MASK);
}

void ostream_write(ostream_desc_t *str, const void *data, unsigned len) {
    ostream_write_ex(str, data, len, 0);
}

void ostream_close(ostream_desc_t *str) {
    if (!str->sem)
        return;

    ostream_write_ex(str, NULL, 0, JD_STREAM_CLOSE_MASK);
    ostream_flush(str);
    ostream_free(str);
}
