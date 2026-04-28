#ifndef MINISNMP_PROTOCOL_H
#define MINISNMP_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

/* Wire format (ASCII, '\n' terminated):
 *   VERSION|MSG_ID|TYPE|OID|VALUE\n
 *
 * VERSION : protocol version (currently 1)
 * MSG_ID  : 32-bit unsigned, correlates request/response
 * TYPE    : GET | RESPONSE | ERROR | TRAP
 * OID     : dotted numeric (e.g. "1.1.1"); empty allowed for some errors
 * VALUE   : free-form string; empty for GET; integer/string for RESPONSE;
 *           error code/text for ERROR.
 *
 * '|' and '\n' are reserved and MUST NOT appear inside fields.
 */

#define PROTO_VERSION       1
#define PROTO_MAX_OID       64
#define PROTO_MAX_VALUE     256
#define PROTO_MAX_PDU       512  /* full encoded line incl. '\n' + NUL */

typedef enum {
    PDU_GET      = 0,
    PDU_RESPONSE = 1,
    PDU_ERROR    = 2,
    PDU_TRAP     = 3,
    PDU_UNKNOWN  = -1
} pdu_type_t;

/* Application-level error codes carried in ERROR.value (as decimal). */
typedef enum {
    ERR_OK            = 0,
    ERR_NO_SUCH_OID   = 1,
    ERR_BAD_REQUEST   = 2,
    ERR_INTERNAL      = 3,
    ERR_UNSUPPORTED   = 4
} proto_err_t;

typedef struct {
    int        version;
    uint32_t   msg_id;
    pdu_type_t type;
    char       oid[PROTO_MAX_OID];
    char       value[PROTO_MAX_VALUE];
} pdu_t;

const char *pdu_type_str(pdu_type_t t);
pdu_type_t  pdu_type_parse(const char *s);
const char *proto_err_str(proto_err_t e);

/* Encode pdu into buf as "V|ID|TYPE|OID|VALUE\n".
 * Returns bytes written (excl. NUL) or -1 on overflow / invalid input. */
int pdu_encode(const pdu_t *pdu, char *buf, size_t bufsz);

/* Parse a single line (with or without trailing '\n') into pdu.
 * Returns 0 on success, -1 on malformed input. */
int pdu_decode(const char *line, pdu_t *pdu);

/* Convenience constructors. */
void pdu_make_get(pdu_t *pdu, uint32_t msg_id, const char *oid);
void pdu_make_response(pdu_t *pdu, uint32_t msg_id, const char *oid, const char *value);
void pdu_make_error(pdu_t *pdu, uint32_t msg_id, const char *oid, proto_err_t err);

#endif
