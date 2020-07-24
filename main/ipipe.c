#include "jdesp.h"

// TODO timeouts?

static portMUX_TYPE streamMux = portMUX_INITIALIZER_UNLOCKED;

#define LOCK() portENTER_CRITICAL(&streamMux)
#define UNLOCK() portEXIT_CRITICAL(&streamMux)

static ipipe_desc_t *streams;

static void ipipe_free(ipipe_desc_t *str) {
    if (!str->sem)
        return;
    if (streams == str) {
        streams = str->next;
    } else {
        for (ipipe_desc_t *ss = streams; ss; ss = ss->next)
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
    for (ipipe_desc_t *ss = streams; ss; ss = ss->next) {
        if ((ss->counter >> JD_PIPE_PORT_SHIFT) == p)
            return false;
    }
    return true;
}

int ipipe_open(ipipe_desc_t *str, ipipe_handler_t handler, ipipe_handler_t meta_handler) {
    LOCK();
    ipipe_free(str);
    str->sem = xSemaphoreCreateMutex();
    str->handler = handler;
    str->meta_handler = meta_handler;
    for (;;) {
        int p = jd_random() & 511;
        if (is_free_port(p)) {
            str->counter = p << JD_PIPE_PORT_SHIFT;
            break;
        }
    }
    str->next = streams;
    streams = str;
    UNLOCK();
    return str->counter >> JD_PIPE_PORT_SHIFT;
}

void ipipe_handle_pkt(jd_packet_t *pkt) {
    if (pkt->service_number != JD_SERVICE_NUMBER_STREAM)
        return;

    uint16_t cmd = pkt->service_command;
    int port = cmd >> JD_PIPE_PORT_SHIFT;
    for (ipipe_desc_t *s = streams; s; s = s->next) {
        if ((s->counter >> JD_PIPE_PORT_SHIFT) == port) {
            if ((s->counter & JD_PIPE_COUNTER_MASK) == (cmd & JD_PIPE_COUNTER_MASK)) {
                s->counter = ((s->counter + 1) & JD_PIPE_COUNTER_MASK) |
                             (s->counter & ~JD_PIPE_COUNTER_MASK);
                if (cmd & JD_PIPE_METADATA_MASK) {
                    s->meta_handler(s, pkt);
                } else {
                    s->handler(s, pkt);
                }
                if (cmd & JD_PIPE_CLOSE_MASK) {
                    s->meta_handler(s, NULL); // indicate EOF
                    ipipe_free(s);
                }
            }
            break;
        }
    }
}

void ipipe_close(ipipe_desc_t *str) {
    if (!str->sem)
        return;
    ipipe_free(str);
}
