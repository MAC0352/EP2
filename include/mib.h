#ifndef MINISNMP_MIB_H
#define MINISNMP_MIB_H

#include <stddef.h>

#include "protocol.h"

/* MIB simplificada:
 *   1.1.1  CPU usage (%)
 *   1.1.2  Memory usage (%)
 *   1.1.3  Uptime (seconds)
 *   1.2.1  Network bytes in
 *   1.2.2  Network bytes out
 *   1.3.1  Active TCP connections
 */

/* Handler reads the current value for an OID into out (NUL-terminated).
 * Returns ERR_OK on success or an appropriate proto_err_t code. */
typedef proto_err_t (*mib_handler_fn)(char *out, size_t outsz);

typedef struct {
    const char     *oid;
    const char     *description;
    mib_handler_fn  handler;
} mib_entry_t;

/* Lookup a MIB entry by exact OID match. Returns NULL if not found. */
const mib_entry_t *mib_lookup(const char *oid);

/* Iterate the static table (for diagnostics / listing). */
const mib_entry_t *mib_table(size_t *count);

/* Convenience: resolve oid + invoke handler. Fills out_value and returns
 * the proto_err_t result. ERR_NO_SUCH_OID if oid not in table. */
proto_err_t mib_get(const char *oid, char *out_value, size_t outsz);

/* Default placeholder handler (returns "0"). Useful before collectors land. */
proto_err_t mib_handler_stub(char *out, size_t outsz);

/* Override resolver: when set, mib_get consults this function first and uses
 * its returned handler if non-NULL. Lets the agent inject /proc collectors
 * without mutating the static table. */
typedef mib_handler_fn (*mib_resolver_fn)(const char *oid);
void mib_set_resolver(mib_resolver_fn r);

#endif
