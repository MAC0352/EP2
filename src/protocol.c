#include "protocol.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *pdu_type_str(pdu_type_t t) {
    switch (t) {
        case PDU_GET:      return "GET";
        case PDU_RESPONSE: return "RESPONSE";
        case PDU_ERROR:    return "ERROR";
        case PDU_TRAP:     return "TRAP";
        default:           return "UNKNOWN";
    }
}

pdu_type_t pdu_type_parse(const char *s) {
    if (!s) return PDU_UNKNOWN;
    if (strcmp(s, "GET") == 0)      return PDU_GET;
    if (strcmp(s, "RESPONSE") == 0) return PDU_RESPONSE;
    if (strcmp(s, "ERROR") == 0)    return PDU_ERROR;
    if (strcmp(s, "TRAP") == 0)     return PDU_TRAP;
    return PDU_UNKNOWN;
}

const char *proto_err_str(proto_err_t e) {
    switch (e) {
        case ERR_OK:           return "OK";
        case ERR_NO_SUCH_OID:  return "NO_SUCH_OID";
        case ERR_BAD_REQUEST:  return "BAD_REQUEST";
        case ERR_INTERNAL:     return "INTERNAL";
        case ERR_UNSUPPORTED:  return "UNSUPPORTED";
        default:               return "UNKNOWN_ERR";
    }
}

static int contains_reserved(const char *s) {
    if (!s) return 0;
    for (; *s; s++) if (*s == '|' || *s == '\n') return 1;
    return 0;
}

int pdu_encode(const pdu_t *pdu, char *buf, size_t bufsz) {
    if (!pdu || !buf || bufsz == 0) return -1;
    if (pdu->type == PDU_UNKNOWN) return -1;
    if (contains_reserved(pdu->oid) || contains_reserved(pdu->value)) return -1;

    int n = snprintf(buf, bufsz, "%d|%u|%s|%s|%s\n",
                     pdu->version ? pdu->version : PROTO_VERSION,
                     (unsigned)pdu->msg_id,
                     pdu_type_str(pdu->type),
                     pdu->oid,
                     pdu->value);
    if (n < 0 || (size_t)n >= bufsz) return -1;
    return n;
}

int pdu_decode(const char *line, pdu_t *pdu) {
    if (!line || !pdu) return -1;

    char tmp[PROTO_MAX_PDU];
    int n = str_copy(tmp, sizeof(tmp), line);
    if (n < 0) return -1;
    /* strip trailing \r\n */
    while (n > 0 && (tmp[n-1] == '\n' || tmp[n-1] == '\r')) tmp[--n] = '\0';

    char *fields[5] = {0};
    int count = str_split(tmp, '|', fields, 5);
    if (count != 5) return -1;

    char *endp = NULL;
    long ver = strtol(fields[0], &endp, 10);
    if (endp == fields[0] || *endp != '\0') return -1;

    unsigned long mid = strtoul(fields[1], &endp, 10);
    if (endp == fields[1] || *endp != '\0') return -1;

    pdu_type_t t = pdu_type_parse(fields[2]);
    if (t == PDU_UNKNOWN) return -1;

    if (strlen(fields[3]) >= PROTO_MAX_OID)   return -1;
    if (strlen(fields[4]) >= PROTO_MAX_VALUE) return -1;

    pdu->version = (int)ver;
    pdu->msg_id  = (uint32_t)mid;
    pdu->type    = t;
    str_copy(pdu->oid,   sizeof(pdu->oid),   fields[3]);
    str_copy(pdu->value, sizeof(pdu->value), fields[4]);
    return 0;
}

void pdu_make_get(pdu_t *pdu, uint32_t msg_id, const char *oid) {
    memset(pdu, 0, sizeof(*pdu));
    pdu->version = PROTO_VERSION;
    pdu->msg_id  = msg_id;
    pdu->type    = PDU_GET;
    str_copy(pdu->oid, sizeof(pdu->oid), oid ? oid : "");
}

void pdu_make_response(pdu_t *pdu, uint32_t msg_id, const char *oid, const char *value) {
    memset(pdu, 0, sizeof(*pdu));
    pdu->version = PROTO_VERSION;
    pdu->msg_id  = msg_id;
    pdu->type    = PDU_RESPONSE;
    str_copy(pdu->oid,   sizeof(pdu->oid),   oid   ? oid   : "");
    str_copy(pdu->value, sizeof(pdu->value), value ? value : "");
}

void pdu_make_error(pdu_t *pdu, uint32_t msg_id, const char *oid, proto_err_t err) {
    memset(pdu, 0, sizeof(*pdu));
    pdu->version = PROTO_VERSION;
    pdu->msg_id  = msg_id;
    pdu->type    = PDU_ERROR;
    str_copy(pdu->oid, sizeof(pdu->oid), oid ? oid : "");
    snprintf(pdu->value, sizeof(pdu->value), "%d %s", (int)err, proto_err_str(err));
}
