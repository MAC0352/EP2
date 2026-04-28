#define _POSIX_C_SOURCE 200809L
#include "manager/manager.h"
#include "util.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static FILE *g_fp = NULL;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

int storage_init(const char *path) {
    pthread_mutex_lock(&g_lock);
    if (g_fp) { pthread_mutex_unlock(&g_lock); return 0; }
    g_fp = fopen(path, "a");
    if (!g_fp) { pthread_mutex_unlock(&g_lock); return -1; }
    /* header only on empty file */
    fseek(g_fp, 0, SEEK_END);
    if (ftell(g_fp) == 0) {
        fputs("timestamp,agent,oid,value\n", g_fp);
        fflush(g_fp);
    }
    pthread_mutex_unlock(&g_lock);
    return 0;
}

void storage_append(const char *agent, const char *oid, const char *value) {
    pthread_mutex_lock(&g_lock);
    if (g_fp) {
        fprintf(g_fp, "%lld,%s,%s,%s\n",
                (long long)now_epoch(), agent, oid, value);
        fflush(g_fp);
    }
    pthread_mutex_unlock(&g_lock);
}

void storage_close(void) {
    pthread_mutex_lock(&g_lock);
    if (g_fp) { fclose(g_fp); g_fp = NULL; }
    pthread_mutex_unlock(&g_lock);
}
