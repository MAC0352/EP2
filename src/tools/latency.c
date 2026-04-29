/* Mede latência de GET/RESPONSE: abre uma conexão TCP, envia N GETs
 * sequenciais para um OID dado e imprime estatísticas (min, p50, p95,
 * max, média) em microssegundos. Uma única conexão é usada para todos
 * os GETs, isolando o custo de processamento por PDU. */

#define _POSIX_C_SOURCE 200809L
#include "protocol.h"
#include "util.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int connect_tcp(const char *host, int port) {
    char p[16]; snprintf(p, sizeof(p), "%d", port);
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, p, &hints, &res) != 0 || !res) return -1;
    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0 || connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        if (fd >= 0) close(fd);
        freeaddrinfo(res); return -1;
    }
    freeaddrinfo(res); return fd;
}

static int write_all(int fd, const char *b, size_t n) {
    size_t o = 0;
    while (o < n) {
        ssize_t w = write(fd, b+o, n-o);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        o += (size_t)w;
    }
    return 0;
}

static int read_line(int fd, char *b, size_t bsz) {
    size_t o = 0;
    while (o + 1 < bsz) {
        char c; ssize_t r = read(fd, &c, 1);
        if (r == 0) break;
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        b[o++] = c;
        if (c == '\n') break;
    }
    b[o] = '\0';
    return (int)o;
}

static int cmp_u64(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static uint64_t now_us(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000);
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Uso: %s <host> <port> <N> [oid]\n", argv[0]);
        return 1;
    }
    const char *host = argv[1];
    int port = atoi(argv[2]);
    int N = atoi(argv[3]);
    const char *oid = (argc >= 5) ? argv[4] : "1.1.3";
    if (N <= 0) return 1;

    int fd = connect_tcp(host, port);
    if (fd < 0) { fprintf(stderr, "connect %s:%d falhou\n", host, port); return 2; }

    uint64_t *samples = calloc((size_t)N, sizeof(uint64_t));
    if (!samples) { close(fd); return 2; }

    int ok = 0;
    for (int i = 0; i < N; i++) {
        pdu_t req, resp;
        pdu_make_get(&req, (uint32_t)(i + 1), oid);
        char buf[PROTO_MAX_PDU];
        int n = pdu_encode(&req, buf, sizeof(buf));
        if (n < 0) break;

        uint64_t t0 = now_us();
        if (write_all(fd, buf, (size_t)n) != 0) break;
        char line[PROTO_MAX_PDU];
        int rn = read_line(fd, line, sizeof(line));
        uint64_t t1 = now_us();
        if (rn <= 0 || pdu_decode(line, &resp) != 0) break;

        samples[ok++] = t1 - t0;
    }
    close(fd);

    if (ok == 0) { free(samples); fprintf(stderr, "nenhum RTT capturado\n"); return 3; }

    qsort(samples, (size_t)ok, sizeof(uint64_t), cmp_u64);
    uint64_t sum = 0;
    for (int i = 0; i < ok; i++) sum += samples[i];
    double mean = (double)sum / ok;
    uint64_t p50 = samples[ok / 2];
    uint64_t p95 = samples[(ok * 95) / 100 < ok ? (ok * 95) / 100 : ok - 1];

    /* Linha CSV-amigável + linha humana. */
    printf("N=%d ok=%d min=%lu p50=%lu p95=%lu max=%lu mean=%.1f (us)\n",
           N, ok, (unsigned long)samples[0], (unsigned long)p50,
           (unsigned long)p95, (unsigned long)samples[ok-1], mean);
    printf("CSV,%d,%d,%lu,%lu,%lu,%lu,%.1f\n",
           N, ok, (unsigned long)samples[0], (unsigned long)p50,
           (unsigned long)p95, (unsigned long)samples[ok-1], mean);

    free(samples);
    return 0;
}
