#include "jdesp.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#define LOG(fmt, ...) DMESG("SOCK: " fmt, ##__VA_ARGS__)
#if 1
#define LOGV(...) ((void)0)
#else
#define LOGV LOG
#endif

#define CHK_ERR(call) CHK(0 != (call))

static xQueueHandle sock_cmds;
static xQueueHandle sock_events;

typedef struct {
    unsigned ev;
    const void *data;
    unsigned size;
} sock_event_t;

typedef struct {
    unsigned cmd;
    union {
        struct {
            char *hostname;
            int port;
        } open;
        struct {
            uint8_t *data;
            unsigned size;
        } write;
    };
} sock_cmd_t;

static void push_event(unsigned event, const void *data, unsigned size) {
    sock_event_t evt = {
        .ev = event,
        .data = data,
        .size = size,
    };
    xQueueSend(sock_events, &evt, 20);
}

static void push_error(const char *msg) {
    push_event(JD_CONN_EV_ERROR, msg, strlen(msg));
}

static int sock_fd;
static void raise_error(const char *msg) {
    if (msg)
        LOG("err: %s", msg);
    else
        LOG("close");
    if (sock_fd) {
        jd_tcpsock_close();
        if (msg)
            push_error(msg);
        push_event(JD_CONN_EV_CLOSE, NULL, 0);
    }
}

static int sock_create_and_connect(const char *hostname, const char *port_num) {
    struct addrinfo hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *result;

    int s = getaddrinfo(hostname, port_num, &hints, &result);
    if (s) {
        LOG("getaddrinfo %s:%s: %d", hostname, port_num, s);
        push_error("can't resolve host");
        return -1;
    }

    int sockfd = -1;

    for (struct addrinfo *rp = result; rp != NULL; rp = rp->ai_next) {
        int sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd == -1)
            continue;

        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1) {
            sockfd = sfd;
            break;
        }

        if (rp->ai_next == NULL) {
            LOG("connect %s:%s: %s", hostname, port_num, strerror(errno));
            push_error("can't connect");
        }

        close(sfd);
    }

    freeaddrinfo(result);

    if (sockfd < 0)
        return sockfd;

    LOG("connected to %s:%s", hostname, port_num);
    return sockfd;
}

static void process_open(sock_cmd_t *cmd) {
    jd_tcpsock_close();
    char *port_num = jd_sprintf_a("%d", cmd->open.port);
    int r = sock_create_and_connect(cmd->open.hostname, port_num);
    jd_free(port_num);
    jd_free(cmd->open.hostname);
    if (r > 0) {
        sock_fd = r;
        push_event(JD_CONN_EV_OPEN, NULL, 0);
    }
}

static int forced_write(int fd, const void *buf, size_t nbytes) {
    int numread = 0;
    while ((int)nbytes > numread) {
        int r = write(fd, (const uint8_t *)buf + numread, nbytes - numread);
        if (r <= 0)
            return r;
        numread += r;
        if ((int)nbytes > numread)
            LOGV("short write: %d", r);
    }
    return numread;
}

static void process_write(sock_cmd_t *cmd) {
    unsigned size = cmd->write.size;
    uint8_t *buf = cmd->write.data;
    if (sock_fd) {
        LOGV("wr %u b", size);
        if (forced_write(sock_fd, buf, size) != (int)size)
            raise_error("write error");
    }
    jd_free(buf);
}

void jd_tcpsock_close(void) {
    if (sock_fd) {
        close(sock_fd);
        sock_fd = 0;
    }
}

int jd_tcpsock_new(const char *hostname, int port) {
    sock_cmd_t cmd = {
        .cmd = JD_CONN_EV_OPEN,
        .open = {.hostname = jd_strdup(hostname), .port = port},
    };
    if (xQueueSend(sock_cmds, &cmd, 20) == pdPASS) {
        return 0;
    } else {
        jd_free(cmd.open.hostname);
        return -1;
    }
}

int jd_tcpsock_write(const void *buf, unsigned size) {
    if (!sock_fd)
        return -10;

    uint8_t *copy = jd_alloc(size);
    memcpy(copy, buf, size);
    sock_cmd_t cmd = {.cmd = JD_CONN_EV_MESSAGE, .write = {.data = copy, .size = size}};
    if (xQueueSend(sock_cmds, &cmd, 0) == pdPASS) {
        return 0;
    } else {
        jd_free(copy);
        return -1;
    }
}

static void worker_main(void *arg) {
    while (1) {
        sock_cmd_t cmd;
        if (!xQueueReceive(sock_cmds, &cmd, 20))
            continue;
        switch (cmd.cmd) {
        case JD_CONN_EV_OPEN:
            process_open(&cmd);
            break;
        case JD_CONN_EV_MESSAGE:
            process_write(&cmd);
            break;
        default:
            JD_PANIC();
        }
    }
}

void jd_tcpsock_init(void) {
    // The main task is at priority 1, so we're higher priority (run "more often").
    // Timer task runs at much higher priority (~20).
    unsigned stack_size = 4096;
    sock_cmds = xQueueCreate(50, sizeof(sock_cmd_t));
    sock_events = xQueueCreate(10, sizeof(sock_event_t));
    TaskHandle_t task;
    xTaskCreatePinnedToCore(worker_main, "tcpsock", stack_size, NULL, 2, &task, WORKER_CPU);
}

void jd_tcpsock_process(void) {
    static uint8_t sockbuf[128];

    sock_event_t evt;
    while (xQueueReceive(sock_events, &evt, 0)) {
        jd_tcpsock_on_event(evt.ev, evt.data, evt.size);
    }

    if (!sock_fd)
        return;

    for (;;) {
        int r = recv(sock_fd, sockbuf, sizeof(sockbuf), MSG_DONTWAIT);
        if (r == 0) {
            raise_error(NULL);
            return;
        }
        if (r > 0) {
            LOGV("rd %d", r);
            jd_tcpsock_on_event(JD_CONN_EV_MESSAGE, sockbuf, r);
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            else {
                raise_error("recv error");
                return;
            }
        }
    }
}
