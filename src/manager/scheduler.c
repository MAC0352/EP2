#define _POSIX_C_SOURCE 200809L
#include "manager/manager.h"
#include "protocol.h"
#include "util.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static pthread_t g_threads[MGR_MAX_AGENTS];
static int       g_thread_count = 0;

typedef struct { manager_t *m; agent_t *a; } worker_arg_t;

static int connect_with_timeout(const char *host, int port, int timeout_ms) {
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);

    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) return -1;

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) { freeaddrinfo(res); return -1; }

    struct timeval tv;
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        freeaddrinfo(res); close(fd); return -1;
    }
    freeaddrinfo(res);
    return fd;
}

static int write_all(int fd, const char *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, buf + off, n - off);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        off += (size_t)w;
    }
    return 0;
}

static int read_line(int fd, char *buf, size_t bufsz) {
    size_t off = 0;
    while (off + 1 < bufsz) {
        char c;
        ssize_t r = read(fd, &c, 1);
        if (r == 0) break;
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        buf[off++] = c;
        if (c == '\n') break;
    }
    buf[off] = '\0';
    return (int)off;
}

int scheduler_poll_once(manager_t *m, agent_t *a) {
    uint64_t t0 = now_ms();
    int fd = connect_with_timeout(a->host, a->port, m->timeout_ms);
    if (fd < 0) {
        LOGW("[%s] connect %s:%d falhou: %s", a->id, a->host, a->port, strerror(errno));
        return -1;
    }

    int all_ok = 1;
    for (int i = 0; i < m->n_oids; i++) {
        pdu_t req, resp;
        pdu_make_get(&req, (uint32_t)(t0 + i), m->oids[i]);
        char buf[PROTO_MAX_PDU];
        int n = pdu_encode(&req, buf, sizeof(buf));
        if (n < 0) { all_ok = 0; break; }
        if (write_all(fd, buf, (size_t)n) != 0) { all_ok = 0; break; }

        char line[PROTO_MAX_PDU];
        int rn = read_line(fd, line, sizeof(line));
        if (rn <= 0) { all_ok = 0; break; }
        if (pdu_decode(line, &resp) != 0) { all_ok = 0; break; }

        if (resp.type == PDU_RESPONSE) {
            pthread_mutex_lock(&a->lock);
            str_copy(a->last_value[i], MGR_VAL_LEN, resp.value);
            pthread_mutex_unlock(&a->lock);
            storage_append(a->id, m->oids[i], resp.value);
        } else {
            pthread_mutex_lock(&a->lock);
            str_copy(a->last_value[i], MGR_VAL_LEN, "ERR");
            pthread_mutex_unlock(&a->lock);
        }
    }
    close(fd);

    uint64_t rtt = now_ms() - t0;
    if (all_ok) failure_record_ok(a, rtt);
    return all_ok ? 0 : -1;
}

static void *worker_main(void *arg) {
    worker_arg_t *w = (worker_arg_t *)arg;
    manager_t *m = w->m; agent_t *a = w->a;
    free(w);

    LOGI("[%s] worker iniciado (%s:%d)", a->id, a->host, a->port);
    while (!m->stop) {
        if (scheduler_poll_once(m, a) != 0) {
            failure_record_fail(a, m->failure_threshold);
        }
        for (int s = 0; s < m->interval_sec && !m->stop; s++) sleep(1);
    }
    LOGI("[%s] worker encerrado", a->id);
    return NULL;
}

int scheduler_start(manager_t *m) {
    g_thread_count = 0;
    for (int i = 0; i < m->n_agents; i++) {
        worker_arg_t *w = malloc(sizeof(*w));
        if (!w) return -1;
        w->m = m; w->a = &m->agents[i];
        if (pthread_create(&g_threads[g_thread_count], NULL, worker_main, w) != 0) {
            free(w); return -1;
        }
        g_thread_count++;
    }
    return 0;
}

void scheduler_stop(manager_t *m) { m->stop = 1; }

void scheduler_join(manager_t *m) {
    (void)m;
    for (int i = 0; i < g_thread_count; i++) pthread_join(g_threads[i], NULL);
    g_thread_count = 0;
}
