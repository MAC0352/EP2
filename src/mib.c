#include "mib.h"
#include "util.h"

#include <stdio.h>
#include <string.h>

proto_err_t mib_handler_stub(char *out, size_t outsz) {
    if (!out || outsz == 0) return ERR_INTERNAL;
    str_copy(out, outsz, "0");
    return ERR_OK;
}

/* Phase-1 table wires every OID to the stub handler.
 * Phase-2 (collectors) will replace these with real readers. */
static const mib_entry_t TABLE[] = {
    { "1.1.1", "CPU usage (%)",          mib_handler_stub },
    { "1.1.2", "Memory usage (%)",       mib_handler_stub },
    { "1.1.3", "Uptime (seconds)",       mib_handler_stub },
    { "1.2.1", "Network bytes in",       mib_handler_stub },
    { "1.2.2", "Network bytes out",      mib_handler_stub },
    { "1.3.1", "Active TCP connections", mib_handler_stub },
};

static const size_t TABLE_LEN = sizeof(TABLE) / sizeof(TABLE[0]);

const mib_entry_t *mib_lookup(const char *oid) {
    if (!oid) return NULL;
    for (size_t i = 0; i < TABLE_LEN; i++) {
        if (strcmp(TABLE[i].oid, oid) == 0) return &TABLE[i];
    }
    return NULL;
}

const mib_entry_t *mib_table(size_t *count) {
    if (count) *count = TABLE_LEN;
    return TABLE;
}

proto_err_t mib_get(const char *oid, char *out_value, size_t outsz) {
    const mib_entry_t *e = mib_lookup(oid);
    if (!e) return ERR_NO_SUCH_OID;
    if (!e->handler) return ERR_INTERNAL;
    return e->handler(out_value, outsz);
}
