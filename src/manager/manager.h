#ifndef MINISNMP_MANAGER_H
#define MINISNMP_MANAGER_H

#include <pthread.h>
#include <stdint.h>
#include <stddef.h>

#define MGR_MAX_AGENTS    64
#define MGR_MAX_OIDS      16
#define MGR_ID_LEN        32
#define MGR_HOST_LEN      64
#define MGR_OID_LEN       64
#define MGR_VAL_LEN       128

typedef enum { AG_UNKNOWN = 0, AG_UP = 1, AG_DOWN = 2 } agent_state_t;

typedef struct {
    char    id[MGR_ID_LEN];
    char    host[MGR_HOST_LEN];
    int     port;

    /* runtime state — protected by lock */
    pthread_mutex_t lock;
    agent_state_t   state;
    int             consecutive_failures;
    uint64_t        last_ok_ms;
    uint64_t        last_rtt_ms;
    char            last_value[MGR_MAX_OIDS][MGR_VAL_LEN]; /* parallel to oids[] */
} agent_t;

typedef struct {
    agent_t  agents[MGR_MAX_AGENTS];
    int      n_agents;

    char     oids[MGR_MAX_OIDS][MGR_OID_LEN];
    int      n_oids;

    int      interval_sec;     /* polling interval per agent */
    int      timeout_ms;       /* socket recv timeout */
    int      failure_threshold;/* K consecutive timeouts → DOWN */
    int      stop;
} manager_t;

/* Config loader. File format (one per line):
 *   <id> <host> <port>
 * '#' starts a comment. Returns 0 on success, -1 on error. */
int manager_load_conf(manager_t *m, const char *path);

/* Storage: append "epoch,agent,oid,value" to history.csv. Thread-safe. */
int storage_init(const char *path);
void storage_append(const char *agent, const char *oid, const char *value);
void storage_close(void);

/* Failure helpers. */
void failure_record_ok(agent_t *a, uint64_t rtt_ms);
void failure_record_fail(agent_t *a, int threshold);

/* Scheduler: spawn one worker per agent. */
int scheduler_start(manager_t *m);
void scheduler_stop(manager_t *m);
void scheduler_join(manager_t *m);

/* Single-shot poll of one agent: open TCP, GET each oid, store value, close.
 * Returns 0 if all OIDs were answered, -1 on any timeout/error. */
int scheduler_poll_once(manager_t *m, agent_t *a);

/* TUI: print snapshot of all agents in a refreshable table. */
void tui_render(const manager_t *m);

#endif
