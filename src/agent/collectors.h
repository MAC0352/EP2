#ifndef MINISNMP_COLLECTORS_H
#define MINISNMP_COLLECTORS_H

#include <stddef.h>
#include "protocol.h"

/* Wire all MIB OIDs to real /proc-based collectors.
 * Must be called once at agent startup, before serving requests. */
void collectors_install(void);

/* Individual collectors (exposed for testing). All return ERR_OK on success. */
proto_err_t collect_cpu(char *out, size_t outsz);
proto_err_t collect_mem(char *out, size_t outsz);
proto_err_t collect_uptime(char *out, size_t outsz);
proto_err_t collect_net_in(char *out, size_t outsz);
proto_err_t collect_net_out(char *out, size_t outsz);
proto_err_t collect_tcp_conns(char *out, size_t outsz);

#endif
