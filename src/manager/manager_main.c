#define _POSIX_C_SOURCE 200809L
#include "manager/manager.h"
#include "mib.h"
#include "util.h"

#include <ctype.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static manager_t G_M;
static volatile sig_atomic_t g_stop = 0;
static void on_signal(int s) { (void)s; g_stop = 1; }

int manager_load_conf(manager_t *m, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { LOGE("não foi possível abrir %s", path); return -1; }

    char line[256];
    int lineno = 0;
    while (fgets(line, sizeof(line), f) && m->n_agents < MGR_MAX_AGENTS) {
        lineno++;
        char *s = str_trim(line);
        if (*s == '\0' || *s == '#') continue;

        char id[MGR_ID_LEN], host[MGR_HOST_LEN];
        int port;
        if (sscanf(s, "%31s %63s %d", id, host, &port) != 3) {
            LOGW("%s:%d: linha inválida", path, lineno);
            continue;
        }
        agent_t *a = &m->agents[m->n_agents++];
        memset(a, 0, sizeof(*a));
        pthread_mutex_init(&a->lock, NULL);
        str_copy(a->id, sizeof(a->id), id);
        str_copy(a->host, sizeof(a->host), host);
        a->port = port;
        a->state = AG_UNKNOWN;
    }
    fclose(f);
    LOGI("config: %d agente(s) carregado(s) de %s", m->n_agents, path);
    return m->n_agents > 0 ? 0 : -1;
}

void tui_render(const manager_t *m) {
    /* Clear screen + home */
    fputs("\x1b[2J\x1b[H", stdout);
    printf("=== mini-SNMP manager  |  intervalo=%ds  timeout=%dms  threshold=%d ===\n\n",
           m->interval_sec, m->timeout_ms, m->failure_threshold);

    printf("%-12s %-20s %-6s %-7s %-8s", "ID", "HOST", "PORT", "STATE", "RTTms");
    for (int j = 0; j < m->n_oids; j++) printf(" %-10s", m->oids[j]);
    printf("\n");
    printf("------------------------------------------------------------");
    for (int j = 0; j < m->n_oids; j++) printf("-----------");
    printf("\n");

    for (int i = 0; i < m->n_agents; i++) {
        const agent_t *a = &m->agents[i];
        const char *st = a->state == AG_UP ? "UP"
                       : a->state == AG_DOWN ? "DOWN" : "?";
        printf("%-12s %-20s %-6d %-7s %-8lu",
               a->id, a->host, a->port, st, (unsigned long)a->last_rtt_ms);
        for (int j = 0; j < m->n_oids; j++) {
            printf(" %-10.10s", a->last_value[j][0] ? a->last_value[j] : "-");
        }
        printf("\n");
    }
    printf("\n(ctrl-c para encerrar)\n");
    fflush(stdout);
}

static void usage(const char *p) {
    fprintf(stderr,
            "Uso: %s <agents.conf> [--interval N] [--timeout-ms N] [--threshold N] [--no-tui] [-v]\n",
            p);
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(argv[0]); return 1; }

    memset(&G_M, 0, sizeof(G_M));
    G_M.interval_sec      = 5;
    G_M.timeout_ms        = 2000;
    G_M.failure_threshold = 3;

    /* Default OID set: all 6 entries from the MIB. */
    size_t mc = 0;
    const mib_entry_t *mt = mib_table(&mc);
    for (size_t i = 0; i < mc && (int)i < MGR_MAX_OIDS; i++) {
        str_copy(G_M.oids[G_M.n_oids++], MGR_OID_LEN, mt[i].oid);
    }

    int tui = 1;
    const char *conf = argv[1];
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--interval") && i+1 < argc)      G_M.interval_sec = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--timeout-ms") && i+1<argc) G_M.timeout_ms   = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--threshold") && i+1<argc)  G_M.failure_threshold = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--no-tui"))                 tui = 0;
        else if (!strcmp(argv[i], "-v"))                       log_set_level(LOG_DEBUG);
        else { usage(argv[0]); return 1; }
    }

    if (manager_load_conf(&G_M, conf) != 0) return 1;
    if (storage_init("history.csv") != 0) {
        LOGE("falha ao abrir history.csv");
        return 1;
    }

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    if (scheduler_start(&G_M) != 0) {
        LOGE("falha ao iniciar scheduler");
        return 1;
    }

    while (!g_stop) {
        if (tui) tui_render(&G_M);
        sleep(1);
    }

    LOGI("encerrando manager...");
    scheduler_stop(&G_M);
    scheduler_join(&G_M);
    storage_close();
    return 0;
}
