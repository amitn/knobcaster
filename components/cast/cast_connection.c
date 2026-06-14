#include "cast_connection.h"

#include <inttypes.h>
#include <string.h>
#include <sys/socket.h>

#include "esp_tls.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "cast.conn";

// Media status JSON can be several KB; size the inbound buffer with headroom.
#define RECV_BUF_SZ 16384
#define SEND_BUF_SZ 2048

struct cast_conn {
    esp_tls_t *tls;
    uint8_t   *recv_buf;            // PSRAM
    uint8_t    send_buf[SEND_BUF_SZ];
};

cast_conn_t *cast_conn_open(esp_ip4_addr_t ip, uint16_t port)
{
    cast_conn_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;

    c->recv_buf = heap_caps_malloc(RECV_BUF_SZ, MALLOC_CAP_SPIRAM);
    if (!c->recv_buf) c->recv_buf = malloc(RECV_BUF_SZ);  // fallback to internal
    if (!c->recv_buf) { free(c); return NULL; }

    char host[16];
    snprintf(host, sizeof(host), IPSTR, IP2STR(&ip));

    esp_tls_cfg_t cfg = {
        .skip_common_name = true,   // self-signed; no CN check
        .timeout_ms = 10000,        // handshake timeout
    };
    c->tls = esp_tls_init();
    if (!c->tls) { cast_conn_close(c); return NULL; }

    int ok = esp_tls_conn_new_sync(host, strlen(host), port, &cfg, c->tls);
    if (ok != 1) {
        ESP_LOGE(TAG, "TLS connect to %s:%u failed", host, port);
        cast_conn_close(c);
        return NULL;
    }
    ESP_LOGI(TAG, "connected to %s:%u", host, port);
    return c;
}

void cast_conn_close(cast_conn_t *c)
{
    if (!c) return;
    if (c->tls) esp_tls_conn_destroy(c->tls);
    if (c->recv_buf) free(c->recv_buf);
    free(c);
}

bool cast_conn_send(cast_conn_t *c, const char *src, const char *dst,
                    const char *ns, const char *payload_json)
{
    int n = cast_msg_encode(c->send_buf + 4, SEND_BUF_SZ - 4, src, dst, ns, payload_json);
    if (n < 0) {
        ESP_LOGE(TAG, "encode overflow (ns=%s)", ns);
        return false;
    }
    // 4-byte big-endian length prefix.
    c->send_buf[0] = (uint8_t)(n >> 24);
    c->send_buf[1] = (uint8_t)(n >> 16);
    c->send_buf[2] = (uint8_t)(n >> 8);
    c->send_buf[3] = (uint8_t)(n);

    size_t total = (size_t)n + 4, sent = 0;
    while (sent < total) {
        int w = esp_tls_conn_write(c->tls, c->send_buf + sent, total - sent);
        if (w >= 0) { sent += w; continue; }
        if (w == ESP_TLS_ERR_SSL_WANT_WRITE || w == ESP_TLS_ERR_SSL_WANT_READ) continue;
        ESP_LOGW(TAG, "write error %d", w);
        return false;
    }
    return true;
}

// Set the socket receive timeout (0 ms => block indefinitely, so clamp to 1ms).
static void set_rcvtimeo(cast_conn_t *c, int timeout_ms)
{
    int fd = -1;
    if (esp_tls_get_conn_sockfd(c->tls, &fd) == ESP_OK && fd >= 0) {
        if (timeout_ms <= 0) timeout_ms = 1;
        struct timeval tv = {
            .tv_sec  = timeout_ms / 1000,
            .tv_usec = (timeout_ms % 1000) * 1000,
        };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
}

// Read exactly n bytes. Returns 1 ok, 0 clean timeout (no partial), -1 closed/error.
static int read_n(cast_conn_t *c, uint8_t *dst, size_t n, int timeout_ms)
{
    set_rcvtimeo(c, timeout_ms);
    size_t got = 0;
    while (got < n) {
        int r = esp_tls_conn_read(c->tls, (char *)dst + got, n - got);
        if (r > 0) { got += r; continue; }
        if (r == 0) return -1;  // peer closed
        if (r == ESP_TLS_ERR_SSL_WANT_READ || r == ESP_TLS_ERR_SSL_WANT_WRITE) {
            return (got == 0) ? 0 : -1;  // timeout: clean only if nothing read yet
        }
        return -1;              // hard error
    }
    return 1;
}

cast_rx_t cast_conn_recv(cast_conn_t *c, cast_msg_t *out, int timeout_ms)
{
    uint8_t hdr[4];
    int h = read_n(c, hdr, 4, timeout_ms);
    if (h <= 0) return (h == 0) ? CAST_RX_TIMEOUT : CAST_RX_CLOSED;

    uint32_t len = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16) |
                   ((uint32_t)hdr[2] << 8)  |  (uint32_t)hdr[3];
    if (len == 0 || len > RECV_BUF_SZ) {
        ESP_LOGW(TAG, "bad frame length %" PRIu32, len);
        return CAST_RX_CLOSED;
    }
    // Header arrived; the body should follow promptly. Use a bounded timeout.
    if (read_n(c, c->recv_buf, len, 3000) != 1) return CAST_RX_CLOSED;
    return cast_msg_decode(c->recv_buf, len, out) ? CAST_RX_MSG : CAST_RX_CLOSED;
}
