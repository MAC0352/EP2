#ifndef MINISNMP_UTIL_H
#define MINISNMP_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO  = 1,
    LOG_WARN  = 2,
    LOG_ERROR = 3
} log_level_t;

void log_set_level(log_level_t level);
void log_msg(log_level_t level, const char *fmt, ...);

#define LOGD(...) log_msg(LOG_DEBUG, __VA_ARGS__)
#define LOGI(...) log_msg(LOG_INFO,  __VA_ARGS__)
#define LOGW(...) log_msg(LOG_WARN,  __VA_ARGS__)
#define LOGE(...) log_msg(LOG_ERROR, __VA_ARGS__)

/* Monotonic timestamp in milliseconds. */
uint64_t now_ms(void);

/* Wall-clock seconds since epoch. */
time_t now_epoch(void);

/* Safe string copy: always NUL-terminates, returns bytes written (excl. NUL).
 * Returns -1 if dst==NULL or dstsz==0. */
int str_copy(char *dst, size_t dstsz, const char *src);

/* Split src on delimiter into up to max_fields fields.
 * Modifies src in place (replaces delim with NUL). Returns field count. */
int str_split(char *src, char delim, char **fields, int max_fields);

/* Trim leading/trailing whitespace in place; returns s. */
char *str_trim(char *s);

#endif
