#include "albumart.h"
#include "ui.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_jpeg_dec.h"

static const char *TAG = "albumart";

#define ART_URL_MAX   512
#define ART_JPEG_MAX  (512 * 1024)   // cap a download (album JPEGs are ~20-120KB)

// One-slot mailbox: only the newest request matters (track changed). An empty
// url means "clear". xQueueOverwrite keeps just the latest.
typedef struct { char url[ART_URL_MAX]; } art_msg_t;
static QueueHandle_t s_q;

// --- download ---------------------------------------------------------------

// Accumulates the response body into a growing PSRAM buffer.
typedef struct { uint8_t *buf; size_t cap, len; bool oom; } art_dl_t;

static esp_err_t on_http_event(esp_http_client_event_t *e)
{
    if (e->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    art_dl_t *d = e->user_data;
    if (d->oom) return ESP_OK;

    if (d->len + e->data_len > d->cap) {
        size_t ncap = d->cap ? d->cap * 2 : 65536;
        while (ncap < d->len + e->data_len) ncap *= 2;
        if (ncap > ART_JPEG_MAX) { d->oom = true; return ESP_OK; }
        uint8_t *nb = heap_caps_realloc(d->buf, ncap, MALLOC_CAP_SPIRAM);
        if (!nb) { d->oom = true; return ESP_OK; }
        d->buf = nb; d->cap = ncap;
    }
    memcpy(d->buf + d->len, e->data, e->data_len);
    d->len += e->data_len;
    return ESP_OK;
}

// GET url into a freshly malloc'd PSRAM buffer (caller frees). false on failure.
static bool http_get(const char *url, uint8_t **out, size_t *out_len)
{
    art_dl_t dl = {0};
    // No cert verification (this project builds esp-tls in insecure mode for the
    // self-signed Cast devices; the art CDN is non-sensitive, so we skip too).
    esp_http_client_config_t cfg = {
        .url                         = url,
        .timeout_ms                  = 8000,
        .skip_cert_common_name_check = true,
        .user_agent                  = "CastKnob/1.0",
        .keep_alive_enable           = false,
        .event_handler               = on_http_event,
        .user_data                   = &dl,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return false;

    esp_err_t err = esp_http_client_perform(c);
    int status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);

    if (err != ESP_OK || status != 200 || dl.oom || dl.len == 0) {
        if (dl.buf) free(dl.buf);
        ESP_LOGW(TAG, "art fetch failed (err=%s status=%d oom=%d len=%u)",
                 esp_err_to_name(err), status, dl.oom, (unsigned)dl.len);
        return false;
    }
    *out = dl.buf; *out_len = dl.len;
    return true;
}

// --- decode -----------------------------------------------------------------

// Decode a JPEG to a 16-byte-aligned RGB565 (LE) buffer in PSRAM. Caller frees
// *out via heap_caps_free. false on failure.
static bool decode_jpeg(const uint8_t *jpg, size_t len, uint8_t **out, int *w, int *h)
{
    jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
    cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;     // matches LVGL RGB565

    jpeg_dec_handle_t dec = NULL;
    if (jpeg_dec_open(&cfg, &dec) != JPEG_ERR_OK) return false;

    bool ok = false;
    uint8_t *outbuf = NULL;
    jpeg_dec_io_t io = { .inbuf = (uint8_t *)jpg, .inbuf_len = (int)len };
    jpeg_dec_header_info_t info = {0};
    if (jpeg_dec_parse_header(dec, &io, &info) != JPEG_ERR_OK) goto done;

    // Output buffer: width*height*2, rounded up to a 16-byte multiple.
    int outlen = 0;
    if (jpeg_dec_get_outbuf_len(dec, &outlen) != JPEG_ERR_OK || outlen <= 0)
        outlen = info.width * info.height * 2;
    outbuf = heap_caps_aligned_alloc(16, (outlen + 15) & ~15, MALLOC_CAP_SPIRAM);
    if (!outbuf) goto done;

    io.outbuf = outbuf;
    if (jpeg_dec_process(dec, &io) != JPEG_ERR_OK) goto done;

    *out = outbuf; *w = info.width; *h = info.height;
    ok = true;

done:
    jpeg_dec_close(dec);
    if (!ok && outbuf) heap_caps_free(outbuf);
    return ok;
}

// --- worker -----------------------------------------------------------------

static void art_task(void *arg)
{
    (void)arg;
    art_msg_t m;
    for (;;) {
        if (xQueueReceive(s_q, &m, portMAX_DELAY) != pdTRUE) continue;

        if (!m.url[0]) { ui_clear_art(); continue; }

        ESP_LOGI(TAG, "fetch art %.80s", m.url);
        uint8_t *jpg = NULL; size_t jlen = 0;
        if (!http_get(m.url, &jpg, &jlen)) continue;

        // A newer request arrived while downloading? Drop this stale one.
        if (uxQueueMessagesWaiting(s_q) > 0) { free(jpg); continue; }

        uint8_t *rgb = NULL; int w = 0, h = 0;
        bool ok = decode_jpeg(jpg, jlen, &rgb, &w, &h);
        free(jpg);
        if (!ok) { ESP_LOGW(TAG, "jpeg decode failed"); continue; }

        ESP_LOGI(TAG, "art decoded %dx%d", w, h);
        ui_set_art(rgb, w, h);   // UI takes ownership of rgb (frees previous)
    }
}

// --- public API -------------------------------------------------------------

void albumart_start(void)
{
    if (s_q) return;
    s_q = xQueueCreate(1, sizeof(art_msg_t));
    // Low priority + pinned to the UI core so a CPU-heavy decode uses spare
    // cycles without ever starving the Cast net task (core 0).
    xTaskCreatePinnedToCore(art_task, "albumart", 12288, NULL, 2, NULL, 1);
}

void albumart_request(const char *url)
{
    if (!s_q || !url) return;
    art_msg_t m;
    strlcpy(m.url, url, sizeof(m.url));
    xQueueOverwrite(s_q, &m);   // replace any pending request with the latest
}

void albumart_clear(void)
{
    if (!s_q) return;
    art_msg_t m = { .url = "" };
    xQueueOverwrite(s_q, &m);
}
