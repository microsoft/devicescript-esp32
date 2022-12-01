#pragma once
#include "jdlow.h"
#include "jdtcp.h"
#include "jd_client.h"
#include "storage/jd_storage.h"
#include "devicescript/devicescript.h"
#include "services/jd_services.h"
#include "services/interfaces/jd_pins.h"
#include "services/interfaces/jd_pwm.h"
#include "services/interfaces/jd_flash.h"
#include "interfaces/jd_usb.h"
#include "network/jd_network.h"

void wifi_init(void);
bool wifi_is_connected(void);

void jdtcp_init(void);

void azureiothub_init(void);
extern const devscloud_api_t azureiothub_cloud;

#define CHK ESP_ERROR_CHECK

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
// needs worker_do_work() called from somewhere
worker_t worker_alloc(void);
// starts task that will call worker_do_work():
worker_t worker_start(const char *id, uint32_t stack_size);
int worker_run(worker_t w, TaskFunction_t fn, void *arg);
void worker_set_idle(worker_t w, TaskFunction_t fn, void *arg);
int worker_run_wait(worker_t w, TaskFunction_t fn, void *arg);
void worker_do_work(worker_t w);

int tim_worker_run(TaskFunction_t fn, void *arg);

bool jd_rx_has_frame(void);
void usb_init(void);
void usb_pre_init(void);

void log_free_mem(void);

extern worker_t main_worker;

char *extract_property(const char *property_bag, int plen, const char *key);
char *jd_hmac_b64(const char *key, const char **parts);

void reboot_to_uf2(void);
void flush_dmesg(void);

void init_sdcard(void);

void jd_tcpsock_process(void);
void jd_tcpsock_init(void);

void get_i2c_pins(uint8_t *sda, uint8_t *scl);
