#include "jdesp.h"

// TODO timeouts?

static portMUX_TYPE streamMux = portMUX_INITIALIZER_UNLOCKED;

#define LOCK() portENTER_CRITICAL(&streamMux)
#define UNLOCK() portEXIT_CRITICAL(&streamMux)

static istream_desc_t *streams;

static void istream_free(istream_desc_t *str) {
    if (!str->sem)
        return;
    if (streams == str) {
        streams = str->next;
    } else {
        for (istream_desc_t *ss = streams; ss; ss = ss->next)
            if (ss->next == str) {
                ss->next = str->next;
                break;
            }
    }
    vSemaphoreDelete(str->sem);
    str->sem = NULL;
    str->counter = 0;
}

static bool is_free_port(int p) {
    if (!p)
        return false;
    for (istream_desc_t *ss = streams; ss; ss = ss->next) {
        if ((ss->counter >> JD_STREAM_PORT_SHIFT) == p)
            return false;
    }
    return true;
}

int istream_open(istream_desc_t *str, istream_handler_t handler, istream_handler_t meta_handler) {
    LOCK();
    istream_free(str);
    str->sem = xSemaphoreCreateMutex();
    str->handler = handler;
    str->meta_handler = meta_handler;
    for (;;) {
        int p = jd_random() & 511;
        if (is_free_port(p)) {
            str->counter = p << JD_STREAM_PORT_SHIFT;
            break;
        }
    }
    str->next = streams;
    streams = str;
    UNLOCK();
    return str->counter >> JD_STREAM_PORT_SHIFT;
}

void istream_handle_pkt(jd_packet_t *pkt) {
    if (pkt->service_number != JD_SERVICE_NUMBER_STREAM)
        return;

    uint16_t cmd = pkt->service_command;
    int port = cmd >> JD_STREAM_PORT_SHIFT;
    for (istream_desc_t *s = streams; s; s = s->next) {
        if ((s->counter >> JD_STREAM_PORT_SHIFT) == port) {
            if ((s->counter & JD_STREAM_COUNTER_MASK) == (cmd & JD_STREAM_COUNTER_MASK)) {
                s->counter = ((s->counter + 1) & JD_STREAM_COUNTER_MASK) |
                             (s->counter & ~JD_STREAM_COUNTER_MASK);
                if (cmd & JD_STREAM_METADATA_MASK) {
                    s->meta_handler(s, pkt);
                } else {
                    s->handler(s, pkt);
                }
                if (cmd & JD_STREAM_CLOSE_MASK) {
                    s->meta_handler(s, NULL); // indicate EOF
                    istream_free(s);
                }
            }
            break;
        }
    }
}

void istream_close(istream_desc_t *str) {
    if (!str->sem)
        return;
    istream_free(str);
}
