#pragma once
#include "jdlow.h"
#include "jdtcp.h"

void log_pin_pulse(int line, int times);

typedef struct ostream_desc {
    // don't access members directly
    uint64_t device_identifier;
    uint16_t counter;
    uint16_t crc_value;
    struct ostream_desc *next;
    TaskHandle_t *crc_task;
    SemaphoreHandle_t sem;
    jd_frame_t *frame;
} ostream_desc_t;
int ostream_open(ostream_desc_t *str, jd_packet_t *pkt);
void ostream_write(ostream_desc_t *str, const void *data, unsigned len);
void ostream_write_meta(ostream_desc_t *str, const void *data, unsigned len);
void ostream_close(ostream_desc_t *str);
int ostream_flush(ostream_desc_t *str);
void ostream_process_ack(jd_packet_t *pkt);

typedef struct istream_desc istream_desc_t;
typedef void (*istream_handler_t)(istream_desc_t *istr, jd_packet_t *pkt);
struct istream_desc {
    istream_handler_t handler;
    istream_handler_t meta_handler;
    struct istream_desc *next;
    SemaphoreHandle_t sem;
    uint16_t counter;
};
int istream_open(istream_desc_t *str, istream_handler_t handler, istream_handler_t meta_handler);
void istream_close(istream_desc_t *str);
void istream_handle_pkt(jd_packet_t *pkt);

uint64_t device_id(void);

void ctrl_process();
void ctrl_handle_packet(jd_packet_t *pkt);

void wifi_process(void);
void wifi_init(void);
void wifi_handle_pkt(jd_packet_t *pkt);
void jdtcp_process(void);
void jdtcp_init(void);
void jdtcp_handle_pkt(jd_packet_t *pkt);

#define CHECK(cond)                                                                                \
    if (!(cond))                                                                                   \
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
void worker_run(worker_t w, TaskFunction_t fn, void *arg);
void worker_set_idle(worker_t w, TaskFunction_t fn, void *arg);