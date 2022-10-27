#include "jdesp.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#define LOG(fmt, ...) DMESG("SOCK: " fmt, ##__VA_ARGS__)
#define LOGV(...) ((void)0)
//#define LOGV LOG

#define CHK_ERR(call) CHK(0 != (call))

static int sock_create_and_connect(const char *hostname, const char *port_num) {
    struct addrinfo hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *result;

    int s = getaddrinfo(hostname, port_num, &hints, &result);
    if (s) {
        LOG("getaddrinfo %s:%s: %d", hostname, port_num, s);
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

        if (rp->ai_next == NULL)
            LOG("connect %s:%s: %s", hostname, port_num, strerror(errno));

        close(sfd);
    }

    freeaddrinfo(result);

    if (sockfd < 0)
        return sockfd;

    LOG("connected to %s:%s", hostname, port_num);
    return sockfd;
}

static int sock_fd;
static void raise_error(const char *msg) {
    if (sock_fd) {
        jd_tcpsock_close();
        if (msg)
            jd_tcpsock_on_event(JD_CONN_EV_ERROR, msg, strlen(msg));
        jd_tcpsock_on_event(JD_CONN_EV_CLOSE, NULL, 0);
    }
}

int jd_tcpsock_new(const char *hostname, int port) {
    jd_tcpsock_close();
    char *port_num = jd_sprintf_a("%d", port);
    int r = sock_create_and_connect(hostname, port_num);
    jd_free(port_num);
    if (r > 0) {
        sock_fd = r;
        jd_tcpsock_on_event(JD_CONN_EV_OPEN, NULL, 0);
        return 0;
    }
    return -1;
}

void jd_tcpsock_close(void) {
    if (sock_fd) {
        close(sock_fd);
        sock_fd = 0;
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

int jd_tcpsock_write(const void *buf, unsigned size) {
    if (!sock_fd)
        return -10;
    // DMESG("wr %s", (const char*)buf);
    if (forced_write(sock_fd, buf, size) == (int)size)
        return 0;
    raise_error("write error");
    return -1;
}

void jd_tcpsock_process(void) {
    static uint8_t sockbuf[128];

    if (!sock_fd)
        return;

    for (;;) {
        int r = recv(sock_fd, sockbuf, sizeof(sockbuf), MSG_DONTWAIT);
        if (r == 0) {
            raise_error(NULL);
            return;
        }
        if (r > 0) {
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
