#pragma once
#include "jdlow.h"
#include "jdtcp.h"
#include "jd_client.h"
#include "storage/jd_storage.h"
#include "devicescript.h"
#include "services/jd_services.h"
#include "services/interfaces/jd_pins.h"
#include "services/interfaces/jd_adc.h"
#include "services/interfaces/jd_pwm.h"
#include "services/interfaces/jd_flash.h"
#include "interfaces/jd_usb.h"
#include "network/jd_network.h"

#define CHK ESP_ERROR_CHECK

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
void uart_log_init(void);
void uart_log_write(const void *data0, unsigned size);
void uart_log_dmesg(void);

extern worker_t main_worker;

char *extract_property(const char *property_bag, int plen, const char *key);
char *jd_hmac_b64(const char *key, const char **parts);

void reboot_to_uf2(void);
void flush_dmesg(void);

void init_sdcard(void);

void jd_tcpsock_process(void);
void jd_tcpsock_init(void);

void get_i2c_pins(uint8_t *sda, uint8_t *scl);
