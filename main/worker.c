#include "jdesp.h"

struct worker {
    TaskHandle_t task;
    xQueueHandle queue;
    TaskFunction_t fn;
    void *arg;
};

typedef struct qitem {
    TaskFunction_t fn;
    void *arg;
} qitem_t;

static void worker_main(void *arg) {
    worker_t w = arg;
    while (1) {
        qitem_t evt;
        if (xQueueReceive(w->queue, &evt, w->fn ? 20 : 1))
            evt.fn(evt.arg);
        if (w->fn)
            w->fn(w->arg);
    }
}

worker_t worker_alloc(const char *id, uint32_t stack_size) {
    worker_t w = calloc(1, sizeof(struct worker));
    w->queue = xQueueCreate(20, sizeof(qitem_t));
    xTaskCreatePinnedToCore(worker_main, id, stack_size, w, 2, &w->task, APP_CPU_NUM);
    return w;
}

void worker_set_idle(worker_t w, TaskFunction_t fn, void *arg) {
    w->fn = fn;
    w->arg = arg;
}

int worker_run(worker_t w, TaskFunction_t fn, void *arg) {
    qitem_t evt = {fn, arg};
    if (xQueueSendFromISR(w->queue, &evt, NULL) == pdPASS)
        return 0;
    return -1;
}
