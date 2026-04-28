#define _POSIX_C_SOURCE 200809L
#include "agent/collectors.h"
#include "mib.h"
#include "util.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- CPU (delta entre amostras de /proc/stat) ---------- */

typedef struct {
    unsigned long long total;
    unsigned long long idle;
} cpu_sample_t;

static pthread_mutex_t cpu_lock = PTHREAD_MUTEX_INITIALIZER;
static cpu_sample_t   cpu_prev = {0, 0};
static int            cpu_have_prev = 0;

static int read_cpu_sample(cpu_sample_t *s) {
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return -1;
    char tag[16];
    unsigned long long user, nice, sys, idle, iowait = 0, irq = 0, softirq = 0, steal = 0;
    int n = fscanf(f, "%15s %llu %llu %llu %llu %llu %llu %llu %llu",
                   tag, &user, &nice, &sys, &idle, &iowait, &irq, &softirq, &steal);
    fclose(f);
    if (n < 5 || strcmp(tag, "cpu") != 0) return -1;
    s->idle  = idle + iowait;
    s->total = user + nice + sys + idle + iowait + irq + softirq + steal;
    return 0;
}

proto_err_t collect_cpu(char *out, size_t outsz) {
    cpu_sample_t cur;
    if (read_cpu_sample(&cur) != 0) return ERR_INTERNAL;

    double pct = 0.0;
    pthread_mutex_lock(&cpu_lock);
    if (cpu_have_prev && cur.total > cpu_prev.total) {
        unsigned long long dt = cur.total - cpu_prev.total;
        unsigned long long di = cur.idle  - cpu_prev.idle;
        pct = 100.0 * (double)(dt - di) / (double)dt;
    }
    cpu_prev = cur;
    cpu_have_prev = 1;
    pthread_mutex_unlock(&cpu_lock);

    if (pct < 0.0)   pct = 0.0;
    if (pct > 100.0) pct = 100.0;
    snprintf(out, outsz, "%.2f", pct);
    return ERR_OK;
}

/* ---------- Memória (/proc/meminfo) ---------- */

proto_err_t collect_mem(char *out, size_t outsz) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return ERR_INTERNAL;
    unsigned long total = 0, avail = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "MemTotal: %lu kB", &total) == 1) continue;
        if (sscanf(line, "MemAvailable: %lu kB", &avail) == 1) continue;
    }
    fclose(f);
    if (total == 0) return ERR_INTERNAL;
    double used_pct = 100.0 * (double)(total - avail) / (double)total;
    if (used_pct < 0.0) used_pct = 0.0;
    snprintf(out, outsz, "%.2f", used_pct);
    return ERR_OK;
}

/* ---------- Uptime (/proc/uptime) ---------- */

proto_err_t collect_uptime(char *out, size_t outsz) {
    FILE *f = fopen("/proc/uptime", "r");
    if (!f) return ERR_INTERNAL;
    double up = 0.0;
    int n = fscanf(f, "%lf", &up);
    fclose(f);
    if (n != 1) return ERR_INTERNAL;
    snprintf(out, outsz, "%llu", (unsigned long long)up);
    return ERR_OK;
}

/* ---------- Tráfego de rede (/proc/net/dev) ---------- */

static int read_net_totals(unsigned long long *in_bytes, unsigned long long *out_bytes) {
    FILE *f = fopen("/proc/net/dev", "r");
    if (!f) return -1;
    char line[512];
    /* skip 2 header lines */
    if (!fgets(line, sizeof(line), f) || !fgets(line, sizeof(line), f)) {
        fclose(f); return -1;
    }
    unsigned long long tot_in = 0, tot_out = 0;
    while (fgets(line, sizeof(line), f)) {
        char *colon = strchr(line, ':');
        if (!colon) continue;
        char ifname[32];
        size_t namelen = (size_t)(colon - line);
        while (namelen > 0 && (line[0] == ' ' || line[0] == '\t')) {
            memmove(line, line + 1, --namelen + 1);
        }
        colon = strchr(line, ':');
        if (!colon) continue;
        namelen = (size_t)(colon - line);
        if (namelen >= sizeof(ifname)) continue;
        memcpy(ifname, line, namelen);
        ifname[namelen] = '\0';
        if (strcmp(ifname, "lo") == 0) continue;

        unsigned long long rx_b, rx_p, rx_e, rx_d, rx_f, rx_c, rx_m, rx_co;
        unsigned long long tx_b;
        int n = sscanf(colon + 1, "%llu %llu %llu %llu %llu %llu %llu %llu %llu",
                       &rx_b, &rx_p, &rx_e, &rx_d, &rx_f, &rx_c, &rx_m, &rx_co, &tx_b);
        if (n < 9) continue;
        tot_in  += rx_b;
        tot_out += tx_b;
    }
    fclose(f);
    *in_bytes  = tot_in;
    *out_bytes = tot_out;
    return 0;
}

proto_err_t collect_net_in(char *out, size_t outsz) {
    unsigned long long in_b, out_b;
    if (read_net_totals(&in_b, &out_b) != 0) return ERR_INTERNAL;
    snprintf(out, outsz, "%llu", in_b);
    return ERR_OK;
}

proto_err_t collect_net_out(char *out, size_t outsz) {
    unsigned long long in_b, out_b;
    if (read_net_totals(&in_b, &out_b) != 0) return ERR_INTERNAL;
    snprintf(out, outsz, "%llu", out_b);
    return ERR_OK;
}

/* ---------- Conexões TCP ativas (/proc/net/tcp + tcp6) ---------- */

/* TCP_ESTABLISHED == 0x01 in kernel; counted as "active". */
static int count_established(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[512];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return 0; }
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        /* Format: "  sl  local_address rem_address  st  ..." */
        char *p = line;
        while (*p == ' ') p++;
        /* skip sl */
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        /* skip local */
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        /* skip remote */
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        /* state in hex */
        unsigned st = 0;
        if (sscanf(p, "%x", &st) == 1 && st == 0x01) count++;
    }
    fclose(f);
    return count;
}

proto_err_t collect_tcp_conns(char *out, size_t outsz) {
    int total = count_established("/proc/net/tcp")
              + count_established("/proc/net/tcp6");
    snprintf(out, outsz, "%d", total);
    return ERR_OK;
}

/* ---------- Instalação na MIB ----------
 * A tabela em src/mib.c é estática; em vez de duplicar a infraestrutura,
 * registramos uma resolução paralela aqui e expomos via mib_get através de
 * uma função de override. Mais simples: re-exportamos as funções e fazemos
 * o agente chamar collect_* diretamente quando despachar.
 *
 * Para manter a interface mib_get unificada, a Fase 2 substitui os ponteiros
 * do array TABLE em src/mib.c. Como aquele array é const, usamos um shim:
 * a função collectors_dispatch abaixo é consultada antes da tabela. */

typedef struct { const char *oid; mib_handler_fn fn; } cmap_t;

static const cmap_t CMAP[] = {
    { "1.1.1", collect_cpu       },
    { "1.1.2", collect_mem       },
    { "1.1.3", collect_uptime    },
    { "1.2.1", collect_net_in    },
    { "1.2.2", collect_net_out   },
    { "1.3.1", collect_tcp_conns },
};

static int g_installed = 0;

mib_handler_fn collectors_resolve(const char *oid); /* used by mib.c when installed */

mib_handler_fn collectors_resolve(const char *oid) {
    if (!g_installed || !oid) return NULL;
    for (size_t i = 0; i < sizeof(CMAP)/sizeof(CMAP[0]); i++) {
        if (strcmp(CMAP[i].oid, oid) == 0) return CMAP[i].fn;
    }
    return NULL;
}

void collectors_install(void) {
    g_installed = 1;
    mib_set_resolver(collectors_resolve);
    /* Prime CPU baseline so the first GET reports a meaningful value. */
    cpu_sample_t s;
    if (read_cpu_sample(&s) == 0) {
        pthread_mutex_lock(&cpu_lock);
        cpu_prev = s;
        cpu_have_prev = 1;
        pthread_mutex_unlock(&cpu_lock);
    }
    LOGI("collectors instalados (6 OIDs)");
}
