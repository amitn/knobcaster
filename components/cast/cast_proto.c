#include "cast_proto.h"

#include <string.h>

// --- protobuf wire helpers ---------------------------------------------------

static int put_varint(uint8_t *buf, int off, uint64_t v)
{
    while (v >= 0x80) {
        buf[off++] = (uint8_t)(v & 0x7F) | 0x80;
        v >>= 7;
    }
    buf[off++] = (uint8_t)v;
    return off;
}

// Write a length-delimited string field. Returns new offset or -1 on overflow.
static int put_field_str(uint8_t *buf, size_t cap, int off, int field, const char *s)
{
    size_t slen = strlen(s);
    uint8_t tmp[5];
    int tl = put_varint(tmp, 0, slen);
    if ((size_t)off + 1 + tl + slen > cap) return -1;
    buf[off++] = (uint8_t)((field << 3) | 2);   // wire type 2 (LEN)
    memcpy(buf + off, tmp, tl); off += tl;
    memcpy(buf + off, s, slen); off += (int)slen;
    return off;
}

int cast_msg_encode(uint8_t *buf, size_t cap,
                    const char *src, const char *dst,
                    const char *ns, const char *payload_utf8)
{
    int off = 0;
    if ((size_t)off + 2 > cap) return -1;
    buf[off++] = (1 << 3) | 0; buf[off++] = 0;          // protocol_version = 0
    if ((off = put_field_str(buf, cap, off, 2, src)) < 0) return -1;
    if ((off = put_field_str(buf, cap, off, 3, dst)) < 0) return -1;
    if ((off = put_field_str(buf, cap, off, 4, ns))  < 0) return -1;
    if ((size_t)off + 2 > cap) return -1;
    buf[off++] = (5 << 3) | 0; buf[off++] = 0;          // payload_type = STRING
    if ((off = put_field_str(buf, cap, off, 6, payload_utf8)) < 0) return -1;
    return off;
}

// --- decode ------------------------------------------------------------------

static bool get_varint(const uint8_t *buf, size_t len, size_t *off, uint64_t *out)
{
    uint64_t v = 0;
    int shift = 0;
    while (*off < len) {
        uint8_t b = buf[(*off)++];
        v |= (uint64_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) { *out = v; return true; }
        shift += 7;
        if (shift > 63) return false;
    }
    return false;
}

bool cast_msg_decode(const uint8_t *buf, size_t len, cast_msg_t *out)
{
    memset(out, 0, sizeof(*out));
    size_t off = 0;
    while (off < len) {
        uint64_t tag;
        if (!get_varint(buf, len, &off, &tag)) return false;
        int field = (int)(tag >> 3);
        int wire  = (int)(tag & 7);
        switch (wire) {
        case 0: { // varint
            uint64_t v;
            if (!get_varint(buf, len, &off, &v)) return false;
            if (field == 5) out->payload_type = (int)v;
            break;
        }
        case 2: { // length-delimited
            uint64_t l;
            if (!get_varint(buf, len, &off, &l)) return false;
            if (off + l > len) return false;
            const uint8_t *p = buf + off;
            if (field == 4) {                       // namespace
                size_t cl = l < sizeof(out->ns) - 1 ? (size_t)l : sizeof(out->ns) - 1;
                memcpy(out->ns, p, cl);
                out->ns[cl] = '\0';
            } else if (field == 6) {                // payload_utf8
                out->payload = p;
                out->payload_len = (size_t)l;
            }
            off += l;
            break;
        }
        case 5: off += 4; break;   // fixed32
        case 1: off += 8; break;   // fixed64
        default: return false;
        }
    }
    return true;
}
