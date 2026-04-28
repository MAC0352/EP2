#define _POSIX_C_SOURCE 200809L
#include "agent/collectors.h"
#include "mib.h"
#include "protocol.h"
#include "util.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static volatile sig_atomic_t g_stop = 0;
static char g_node_id[64] = "agent";

static void on_signal(int sig) { (void)sig; g_stop = 1; }

static void usage(const char *prog) {
    fprintf(stderr,
            "Uso: %s <porta> [--id NODE_ID]\n"
            "  porta     porta TCP de escuta (1..65535)\n"
            "  --id ID   identificador opcional do nó (default: hostname-ish)\n",
            prog);
}

/* Read one line ('\n' terminated) from fd into buf. Returns bytes read
 * (without NUL), 0 on clean EOF, -1 on error / overflow. */
static int read_line(int fd, char *buf, size_t bufsz) {
    size_t off = 0;
    while (off + 1 < bufsz) {
        char c;
        ssize_t r = read(fd, &c, 1);
        if (r == 0) {
            if (off == 0) return 0;
            break;
        }
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        buf[off++] = c;
        if (c == '\n') break;
    }
    buf[off] = '\0';
    return (int)off;
}

static int write_all(int fd, const char *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, buf + off, n - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

static void handle_pdu(int fd, const pdu_t *req) {
    pdu_t resp;
    char value[PROTO_MAX_VALUE];

    if (req->type != PDU_GET) {
        pdu_make_error(&resp, req->msg_id, req->oid, ERR_BAD_REQUEST);
    } else {
        proto_err_t e = mib_get(req->oid, value, sizeof(value));
        if (e == ERR_OK)      pdu_make_response(&resp, req->msg_id, req->oid, value);
        else                  pdu_make_error(&resp, req->msg_id, req->oid, e);
    }

    char out[PROTO_MAX_PDU];
    int n = pdu_encode(&resp, out, sizeof(out));
    if (n < 0) { LOGE("falha ao codificar resposta"); return; }
    if (write_all(fd, out, (size_t)n) != 0) LOGW("write falhou: %s", strerror(errno));
}

typedef struct { int fd; struct sockaddr_in peer; } conn_arg_t;

static void *conn_thread(void *arg) {
    conn_arg_t *c = (conn_arg_t *)arg;
    char peer[INET_ADDRSTRLEN] = "?";
    inet_ntop(AF_INET, &c->peer.sin_addr, peer, sizeof(peer));
    LOGI("[%s] conexão de %s:%u", g_node_id, peer, (unsigned)ntohs(c->peer.sin_port));

    char line[PROTO_MAX_PDU];
    for (;;) {
        int n = read_line(c->fd, line, sizeof(line));
        if (n <= 0) break;
        if (line[n-1] != '\n') {
            LOGW("PDU sem terminador, descartado");
            pdu_t err; pdu_make_error(&err, 0, "", ERR_BAD_REQUEST);
            char out[PROTO_MAX_PDU];
            int nn = pdu_encode(&err, out, sizeof(out));
            if (nn > 0) (void)write_all(c->fd, out, (size_t)nn);
            continue;
        }
        pdu_t req;
        if (pdu_decode(line, &req) != 0) {
            LOGW("PDU malformado");
            pdu_t err; pdu_make_error(&err, 0, "", ERR_BAD_REQUEST);
            char out[PROTO_MAX_PDU];
            int nn = pdu_encode(&err, out, sizeof(out));
            if (nn > 0) (void)write_all(c->fd, out, (size_t)nn);
            continue;
        }
        LOGD("[%s] %s msg_id=%u oid=%s", g_node_id,
             pdu_type_str(req.type), req.msg_id, req.oid);
        handle_pdu(c->fd, &req);
    }

    LOGI("[%s] conexão de %s encerrada", g_node_id, peer);
    close(c->fd);
    free(c);
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(argv[0]); return 1; }
    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) { usage(argv[0]); return 1; }

    str_copy(g_node_id, sizeof(g_node_id), "agent");
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--id") == 0 && i + 1 < argc) {
            str_copy(g_node_id, sizeof(g_node_id), argv[++i]);
        } else if (strcmp(argv[i], "-v") == 0) {
            log_set_level(LOG_DEBUG);
        } else {
            usage(argv[0]); return 1;
        }
    }

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    collectors_install();

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { LOGE("socket: %s", strerror(errno)); return 1; }

    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOGE("bind :%d: %s", port, strerror(errno));
        close(srv); return 1;
    }
    if (listen(srv, 16) < 0) {
        LOGE("listen: %s", strerror(errno));
        close(srv); return 1;
    }

    LOGI("[%s] agente ouvindo em 0.0.0.0:%d", g_node_id, port);

    while (!g_stop) {
        struct sockaddr_in peer;
        socklen_t plen = sizeof(peer);
        int cfd = accept(srv, (struct sockaddr *)&peer, &plen);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            LOGW("accept: %s", strerror(errno));
            continue;
        }
        conn_arg_t *c = malloc(sizeof(*c));
        if (!c) { close(cfd); continue; }
        c->fd = cfd; c->peer = peer;

        pthread_t th;
        if (pthread_create(&th, NULL, conn_thread, c) != 0) {
            LOGE("pthread_create: %s", strerror(errno));
            close(cfd); free(c); continue;
        }
        pthread_detach(th);
    }

    LOGI("[%s] encerrando", g_node_id);
    close(srv);
    return 0;
}
