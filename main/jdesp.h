#pragma once
#include "jdlow.h"
#include "jdtcp.h"
#include "jd_client.h"
#include "jacscript/jacscript.h"

typedef struct opipe_desc {
    // don't access members directly
    uint64_t device_identifier;
    uint16_t counter;
    uint16_t crc_value;
    struct opipe_desc *next;
    TaskHandle_t *crc_task;
    SemaphoreHandle_t sem;
    jd_frame_t *frame;
} opipe_desc_t;
int opipe_open(opipe_desc_t *str, jd_packet_t *pkt);
void opipe_write(opipe_desc_t *str, const void *data, unsigned len);
void opipe_write_meta(opipe_desc_t *str, const void *data, unsigned len);
void opipe_close(opipe_desc_t *str);
int opipe_flush(opipe_desc_t *str);
void opipe_process_ack(jd_packet_t *pkt);

typedef struct ipipe_desc ipipe_desc_t;
typedef void (*ipipe_handler_t)(ipipe_desc_t *istr, jd_packet_t *pkt);
struct ipipe_desc {
    ipipe_handler_t handler;
    ipipe_handler_t meta_handler;
    struct ipipe_desc *next;
    SemaphoreHandle_t sem;
    uint16_t counter;
};
int ipipe_open(ipipe_desc_t *str, ipipe_handler_t handler, ipipe_handler_t meta_handler);
void ipipe_close(ipipe_desc_t *str);
void ipipe_handle_pkt(jd_packet_t *pkt);

void wifi_init(void);
void jdtcp_init(void);

#define CHK(call)                                                                                  \
    if ((call) != 0)                                                                               \
    jd_panic()

void led_blink(int us);
void led_set(int state);

typedef struct _ssl_conn_t ssl_conn_t;

ssl_conn_t *ssl_alloc(void);
int ssl_connect(ssl_conn_t *conn, const char *hostname, int port);
bool ssl_is_connected(ssl_conn_t *conn);
int ssl_write(ssl_conn_t *conn, const void *data, uint32_t len);
int ssl_get_bytes_avail(ssl_conn_t *conn);
int ssl_read(ssl_conn_t *conn, void *data, uint32_t len);
void ssl_close(ssl_conn_t *conn);

typedef struct worker *worker_t;
worker_t worker_alloc(const char *id, uint32_t stack_size);
int worker_run(worker_t w, TaskFunction_t fn, void *arg);
void worker_set_idle(worker_t w, TaskFunction_t fn, void *arg);
int worker_run_wait(worker_t w, TaskFunction_t fn, void *arg);

bool jd_rx_has_frame(void);
void init_jacscript_manager(void);
void hf2_init(void);

extern worker_t fg_worker;
