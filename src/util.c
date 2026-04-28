#define _POSIX_C_SOURCE 200809L
#include "util.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static log_level_t g_level = LOG_INFO;
static const char *level_tag[] = {"DEBUG", "INFO", "WARN", "ERROR"};

void log_set_level(log_level_t level) { g_level = level; }

void log_msg(log_level_t level, const char *fmt, ...) {
    if (level < g_level) return;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);
    char tbuf[32];
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tm);

    FILE *out = (level >= LOG_WARN) ? stderr : stdout;
    fprintf(out, "[%s.%03ld] %-5s ", tbuf, ts.tv_nsec / 1000000, level_tag[level]);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);

    fputc('\n', out);
    fflush(out);
}

uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000);
}

time_t now_epoch(void) { return time(NULL); }

int str_copy(char *dst, size_t dstsz, const char *src) {
    if (!dst || dstsz == 0) return -1;
    if (!src) { dst[0] = '\0'; return 0; }
    size_t n = 0;
    while (n + 1 < dstsz && src[n]) { dst[n] = src[n]; n++; }
    dst[n] = '\0';
    return (int)n;
}

int str_split(char *src, char delim, char **fields, int max_fields) {
    if (!src || !fields || max_fields <= 0) return 0;
    int count = 0;
    fields[count++] = src;
    for (char *p = src; *p && count < max_fields; p++) {
        if (*p == delim) {
            *p = '\0';
            fields[count++] = p + 1;
        }
    }
    /* If more delimiters remain after max_fields, leave them inside last field. */
    return count;
}

char *str_trim(char *s) {
    if (!s) return s;
    while (*s && isspace((unsigned char)*s)) s++;
    if (!*s) return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}
