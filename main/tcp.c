#include "jdesp.h"

struct srv_state {
    SRV_COMMON;
};

static srv_t *jdtcp_state;

#define LOG(...) ESP_LOGI("TCP", __VA_ARGS__)

#define SERV_NUM jdtcp_state->service_number

#define SSL_OPEN 0x0001

typedef struct {
    void *userdata;
    jd_packet_t pkt;
} jd_packet_ext_t;

jd_packet_ext_t *mk_packet_ext(jd_packet_t *pkt, void *userdata) {
    uint32_t sz = pkt ? pkt->service_size : 0;
    jd_packet_ext_t *ext = calloc(1, sizeof(jd_packet_ext_t) + sz);
    ext->userdata = userdata;
    if (pkt)
        memcpy(&ext->pkt, pkt, sizeof(*pkt) + pkt->service_size);
    return ext;
}

static worker_t worker;

typedef struct connection {
    ipipe_desc_t inp; // keep as first member!
    opipe_desc_t outp;
    ssl_conn_t *ssl;
    struct connection *next;
} conn_t;

static conn_t *connlist;

struct tcp_info {
    uint32_t tag;
    int32_t error;
};

struct ssl_open_cmd {
    uint32_t tag;
    uint16_t port;
    char hostname[0];
};

static void conn_free(conn_t *conn) {
    LOG("free conn %p", conn);
    opipe_close(&conn->outp);
    if (conn->ssl)
        ssl_close(conn->ssl);
    if (connlist == conn) {
        connlist = conn->next;
    } else {
        for (conn_t *ss = connlist; ss; ss = ss->next)
            if (ss->next == conn) {
                ss->next = conn->next;
                break;
            }
    }
    free(conn);
}

static void signal_error(conn_t *conn, int err) {
    LOG("signal error %p - %d", conn, err);
    struct tcp_info info = {.tag = 0, err = err};
    opipe_write_meta(&conn->outp, &info, sizeof(info));
    conn_free(conn);
}

static void data_handler_inner(jd_packet_ext_t *ext) {
    conn_t *conn = (conn_t *)ext->userdata;
    jd_packet_t *pkt = &ext->pkt;
    // LOG("data sz=%d", pkt->service_size);
    if (conn->ssl && pkt->service_size) {
        int r = ssl_write(conn->ssl, pkt->data, pkt->service_size);
        if (r < 0)
            signal_error(conn, r);
    }
    free(ext);
}

static void data_handler(ipipe_desc_t *istr, jd_packet_t *pkt) {
    worker_run(worker, (TaskFunction_t)data_handler_inner, mk_packet_ext(pkt, istr));
}

static void cmd_ssl_open(conn_t *conn, struct ssl_open_cmd *cmd) {
    if (conn->ssl)
        ssl_close(conn->ssl);

    conn->ssl = ssl_alloc();
    int r = ssl_connect(conn->ssl, cmd->hostname, cmd->port);
    LOG("ssl conn r=%d", r);
    if (r)
        signal_error(conn, r);

    opipe_write(&conn->outp, NULL, 0); // poke output stream to indicate we managed to connect
    opipe_flush(&conn->outp);

    LOG("ssl open done");
}

static void meta_handler_inner(jd_packet_ext_t *ext) {
    conn_t *conn = (conn_t *)ext->userdata;
    jd_packet_t *pkt = &ext->pkt;
    if (pkt->service_number == 0) {
        conn_free(conn);
    } else if (pkt->service_size < 4) {
        signal_error(conn, 2); // cmd validation error
    } else {
        uint16_t cmd = *(uint16_t *)pkt->data;
        switch (cmd) {
        case SSL_OPEN:
            if (pkt->service_size >= sizeof(struct ssl_open_cmd) &&
                pkt->data[pkt->service_size - 1] == 0) {
                cmd_ssl_open(conn, (void *)pkt->data);
            }
            break;
        default:
            signal_error(conn, 1); // cmd not understood
            break;
        }
    }
    free(ext);
}

static void meta_handler(ipipe_desc_t *istr, jd_packet_t *pkt) {
    worker_run(worker, (TaskFunction_t)meta_handler_inner, mk_packet_ext(pkt, istr));
}

static void open_stream(jd_packet_t *pkt) {
    conn_t *conn = calloc(1, sizeof(conn_t));
    int r = opipe_open(&conn->outp, pkt);
    if (r < 0) {
        free(conn);
        return;
    }
    int port = ipipe_open(&conn->inp, data_handler, meta_handler);
    jd_send(SERV_NUM, pkt->service_command, &port, 2); // return input port
    conn->next = connlist;
    connlist = conn;
}

static void flush_ssl(void *arg) {
    for (conn_t *conn = connlist; conn; conn = conn->next) {
        if (conn->ssl && ssl_get_bytes_avail(conn->ssl)) {
            uint8_t buf[JD_SERIAL_PAYLOAD_SIZE];
            int sz = ssl_read(conn->ssl, buf, sizeof(buf));
            if (sz < 0) {
                signal_error(conn, sz);
            } else if (sz == 0) {
                conn_free(conn);
                break; // do not continue, as the list got messed up - will do next time
            } else {
                opipe_write(&conn->outp, buf, sz);
                opipe_flush(&conn->outp);
            }
        }
    }
}

void jdtcp_process(srv_t *state) {}

void jdtcp_handle_packet(srv_t *state, jd_packet_t *pkt) {
    switch (pkt->service_command) {
    case JDTCP_CMD_OPEN:
        open_stream(pkt);
        break;
    }
}

SRV_DEF(jdtcp, JD_SERVICE_CLASS_TCP);
void jdtcp_init(void) {
    SRV_ALLOC(jdtcp);
    jdtcp_state = state;
    // this handles SSL tasks which need lots of stack
    worker = worker_alloc("tcp", 10 * 1024);
    worker_set_idle(worker, flush_ssl, NULL);
}
