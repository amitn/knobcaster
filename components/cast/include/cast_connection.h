// CASTV2 connection: one TLS socket to a Cast device, with length-prefixed
// CastMessage framing. Cast devices use self-signed certs, so TLS connects
// without CA verification.
#pragma once

#include "cast_types.h"
#include "cast_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cast_conn cast_conn_t;

// Open a TLS connection to ip:port (port is usually CAST_PORT). NULL on failure.
cast_conn_t *cast_conn_open(esp_ip4_addr_t ip, uint16_t port);

// Close and free the connection.
void cast_conn_close(cast_conn_t *c);

// Send one framed CastMessage with a STRING (JSON) payload. Returns false on error.
bool cast_conn_send(cast_conn_t *c, const char *src, const char *dst,
                    const char *ns, const char *payload_json);

// Receive one framed CastMessage, blocking up to timeout_ms. On success fills
// *out (whose `payload` points into the connection's internal buffer, valid
// until the next recv). Returns false on timeout, close, or decode error.
bool cast_conn_recv(cast_conn_t *c, cast_msg_t *out, int timeout_ms);

#ifdef __cplusplus
}
#endif
