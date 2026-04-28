#include "manager/manager.h"
#include "util.h"

#include <pthread.h>

void failure_record_ok(agent_t *a, uint64_t rtt_ms) {
    pthread_mutex_lock(&a->lock);
    if (a->state != AG_UP) {
        LOGI("[%s] estado UP (rtt=%lums)", a->id, (unsigned long)rtt_ms);
    }
    a->state = AG_UP;
    a->consecutive_failures = 0;
    a->last_ok_ms = now_ms();
    a->last_rtt_ms = rtt_ms;
    pthread_mutex_unlock(&a->lock);
}

void failure_record_fail(agent_t *a, int threshold) {
    pthread_mutex_lock(&a->lock);
    a->consecutive_failures++;
    if (a->state != AG_DOWN && a->consecutive_failures >= threshold) {
        a->state = AG_DOWN;
        LOGW("[%s] estado DOWN após %d falhas consecutivas",
             a->id, a->consecutive_failures);
    }
    pthread_mutex_unlock(&a->lock);
}
