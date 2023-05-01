#include "jdesp.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "mbedtls/platform.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/esp_debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/certs.h"
#include "esp_crt_bundle.h"

#define LOG(fmt, ...) DMESG("SOCK: " fmt, ##__VA_ARGS__)
#if 1
#define LOGV(...) ((void)0)
#else
#define LOGV LOG
#endif

#define CHK_ERR(call) CHK(0 != (call))

static xQueueHandle sock_cmds;
static xQueueHandle sock_events;
static uint8_t sockbuf[128];

typedef struct {
    mbedtls_ssl_context ssl;
    mbedtls_ctr_drbg_context ctr_drbg; // rng
    mbedtls_ssl_config conf;
    mbedtls_entropy_context entropy;
    mbedtls_net_context server_fd;
    bool is_tls;
    bool is_connected;

#if 0
    mbedtls_x509_crt cacert;
    mbedtls_x509_crt *cacert_ptr;
    mbedtls_x509_crt clientcert;
    mbedtls_pk_context clientkey;
#endif
} sock_state_t;

static sock_state_t _tls;

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

static bool needs_io(int ret) {
    return (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);
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

static int mbedtls_print_error_msg(const char *fn, int error) {
    LOG("%s returned -%x", fn, -error);
    mbedtls_strerror(error, (char *)sockbuf, sizeof(sockbuf));
    LOG("  %s", sockbuf);
    raise_error(fn);
    return -1;
}

static int sock_create_and_connect(const char *hostname, int port) {
    struct addrinfo hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *result;

    bool is_tls = false;

    if (port < 0) {
        port = -port;
        is_tls = true;
    }

    char portbuf[10];
    jd_sprintf(portbuf, sizeof(portbuf), "%d", port);

    int s = getaddrinfo(hostname, portbuf, &hints, &result);
    if (s) {
        LOG("getaddrinfo %s:%d: %d", hostname, port, s);
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
            LOG("connect %s:%d: %s", hostname, port, strerror(errno));
            push_error("can't connect");
        }

        close(sfd);
    }

    freeaddrinfo(result);

    if (sockfd < 0)
        return sockfd;

    LOG("connected to %s:%d", hostname, port);

    sock_fd = sockfd;

    if (!is_tls)
        return 0;

    sock_state_t *tls = &_tls;
    mbedtls_ssl_init(&tls->ssl);
    mbedtls_ctr_drbg_init(&tls->ctr_drbg);
    mbedtls_ssl_config_init(&tls->conf);
    mbedtls_entropy_init(&tls->entropy);
    tls->is_tls = true;
    tls->server_fd.fd = sockfd;

    int ret;

    if ((ret = mbedtls_ssl_set_hostname(&tls->ssl, hostname)) != 0)
        return mbedtls_print_error_msg("mbedtls_ssl_set_hostname", ret);

    if ((ret = mbedtls_ssl_config_defaults(&tls->conf, MBEDTLS_SSL_IS_CLIENT,
                                           MBEDTLS_SSL_TRANSPORT_STREAM,
                                           MBEDTLS_SSL_PRESET_DEFAULT)) != 0)
        return mbedtls_print_error_msg("mbedtls_ssl_config_defaults", ret);

    mbedtls_ssl_conf_authmode(&tls->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    esp_crt_bundle_attach(&tls->conf);

    if ((ret = mbedtls_ctr_drbg_seed(&tls->ctr_drbg, mbedtls_entropy_func, &tls->entropy, NULL,
                                     0)) != 0)
        return mbedtls_print_error_msg("mbedtls_ctr_drbg_seed", ret);

    mbedtls_ssl_conf_rng(&tls->conf, mbedtls_ctr_drbg_random, &tls->ctr_drbg);

    // 2-warn 3-debug 4-verbose
    // mbedtls_esp_enable_debug_log(&tls->conf, 3);

    if ((ret = mbedtls_ssl_setup(&tls->ssl, &tls->conf)) != 0)
        return mbedtls_print_error_msg("mbedtls_ssl_setup", ret);

    mbedtls_ssl_set_bio(&tls->ssl, &tls->server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);

    for (;;) {
        ret = mbedtls_ssl_handshake(&tls->ssl);
        if (ret == 0)
            break;

        if (!needs_io(ret))
            return mbedtls_print_error_msg("mbedtls_ssl_handshake", ret);

        vTaskDelay(10);
    }

    mbedtls_net_set_nonblock(&tls->server_fd);

    LOG("TLS handshake completed with %s:%d", hostname, port);

    return 0;
}

static void process_open(sock_cmd_t *cmd) {
    jd_tcpsock_close();
    int r = sock_create_and_connect(cmd->open.hostname, cmd->open.port);
    jd_free(cmd->open.hostname);
    if (r == 0) {
        _tls.is_connected = true;
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

static int sock_mbedtls_write(sock_state_t *tls, const uint8_t *data, size_t datalen) {
    JD_ASSERT(datalen < MBEDTLS_SSL_OUT_CONTENT_LEN);

    size_t written = 0;
    size_t write_len = datalen;
    while (written < datalen) {
        ssize_t ret = mbedtls_ssl_write(&tls->ssl, data + written, write_len);
        if (ret <= 0) {
            if (ret != 0 && !needs_io(ret)) {
                return mbedtls_print_error_msg("mbedtls_ssl_write", ret);
            } else {
                vTaskDelay(5);
            }
        }
        written += ret;
        write_len = datalen - written;
    }
    return written;
}

static void process_write(sock_cmd_t *cmd) {
    unsigned size = cmd->write.size;
    uint8_t *buf = cmd->write.data;

    sock_state_t *tls = &_tls;

    if (tls->is_tls) {
        LOGV("wrTLS %u b", size);
        sock_mbedtls_write(tls, buf, size);
    } else if (sock_fd) {
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

    sock_state_t *tls = &_tls;
    tls->is_connected = false;
    if (tls->is_tls) {
        // mbedtls_ssl_session_reset(&tls->ssl);
        mbedtls_entropy_free(&tls->entropy);
        mbedtls_ssl_config_free(&tls->conf);
        mbedtls_ctr_drbg_free(&tls->ctr_drbg);
        mbedtls_ssl_free(&tls->ssl);
        tls->is_tls = 0;
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
    esp_log_level_set("mbedtls", ESP_LOG_DEBUG);

    // The main task is at priority 1, so we're higher priority (run "more often").
    // Timer task runs at much higher priority (~20).
    unsigned stack_size = 4096;
    sock_cmds = xQueueCreate(50, sizeof(sock_cmd_t));
    sock_events = xQueueCreate(10, sizeof(sock_event_t));
    TaskHandle_t task;
    xTaskCreatePinnedToCore(worker_main, "tcpsock", stack_size, NULL, 2, &task, WORKER_CPU);
}

void jd_tcpsock_process(void) {
    sock_event_t evt;
    while (xQueueReceive(sock_events, &evt, 0)) {
        jd_tcpsock_on_event(evt.ev, evt.data, evt.size);
    }

    sock_state_t *tls = &_tls;

    if (!tls->is_connected)
        return;

    for (;;) {
        if (tls->is_tls) {
            int ret = mbedtls_ssl_read(&tls->ssl, sockbuf, sizeof(sockbuf));

            if (needs_io(ret))
                return;

            if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || ret == 0) {
                raise_error(NULL);
                return;
            }

            if (ret < 0) {
                mbedtls_print_error_msg("mbedtls_ssl_read", ret);
                return;
            }

            LOGV("rdTLS %d", ret);
            jd_tcpsock_on_event(JD_CONN_EV_MESSAGE, sockbuf, ret);
        } else {
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
}
