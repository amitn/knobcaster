// Minimal CASTV2 `CastMessage` protobuf encode/decode + protocol constants.
//
// CastMessage (cast_channel.proto):
//   1 protocol_version (enum, we always send 0 = CASTV2_1_0)
//   2 source_id        (string)
//   3 destination_id   (string)
//   4 namespace        (string)
//   5 payload_type     (enum, 0 = STRING)
//   6 payload_utf8     (string, JSON)
//   7 payload_binary   (bytes, unused)
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CAST_NS_LEN 80

// Cast namespaces and well-known endpoint ids.
#define CAST_NS_CONNECTION "urn:x-cast:com.google.cast.tp.connection"
#define CAST_NS_HEARTBEAT  "urn:x-cast:com.google.cast.tp.heartbeat"
#define CAST_NS_RECEIVER   "urn:x-cast:com.google.cast.receiver"
#define CAST_NS_MEDIA      "urn:x-cast:com.google.cast.media"
#define CAST_NS_MULTIZONE  "urn:x-cast:com.google.cast.multizone"
#define CAST_SRC_DEFAULT   "sender-0"
#define CAST_DST_RECEIVER  "receiver-0"
#define CAST_PORT          8009

// A decoded inbound message. `payload` points into the caller's buffer and is
// NOT null-terminated — use payload_len (e.g. cJSON_ParseWithLength / "%.*s").
typedef struct {
    char           ns[CAST_NS_LEN];
    int            payload_type;
    const uint8_t *payload;
    size_t         payload_len;
} cast_msg_t;

// Encode a STRING CastMessage into buf. Returns encoded length or -1 on overflow.
int cast_msg_encode(uint8_t *buf, size_t cap,
                    const char *src, const char *dst,
                    const char *ns, const char *payload_utf8);

// Decode a CastMessage from buf[0..len). Returns false on malformed input.
bool cast_msg_decode(const uint8_t *buf, size_t len, cast_msg_t *out);
