#include "prov.h"

#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "prov";

static httpd_handle_t s_server;
static char           s_ssid[33];
static char           s_pass[65];
static volatile bool  s_submitted;

static const char FORM_HTML[] =
    "<!doctype html><html><head>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Cast Knob Wi-Fi</title></head>"
    "<body style='font-family:sans-serif;max-width:420px;margin:2em auto;padding:1em'>"
    "<h2>Cast Knob setup</h2><p>Connect this device to your Wi-Fi:</p>"
    "<form method=POST action=/save>"
    "<p>Network<br><input name=ssid style='width:100%;font-size:1.1em' placeholder='Wi-Fi name'></p>"
    "<p>Password<br><input name=pass type=password style='width:100%;font-size:1.1em'></p>"
    "<p><button type=submit style='font-size:1.1em;padding:.5em 1.2em'>Save &amp; Connect</button></p>"
    "</form></body></html>";

static const char DONE_HTML[] =
    "<!doctype html><html><body style='font-family:sans-serif;text-align:center;margin-top:3em'>"
    "<h2>Connecting&hellip;</h2><p>You can close this page.</p></body></html>";

static esp_err_t serve_form(httpd_req_t *r)
{
    httpd_resp_set_type(r, "text/html");
    return httpd_resp_send(r, FORM_HTML, HTTPD_RESP_USE_STRLEN);
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// In-place URL-decode (form-urlencoded: '+' -> space, %xx -> byte).
static void url_decode(char *s)
{
    char *o = s;
    for (char *p = s; *p; p++) {
        if (*p == '+') {
            *o++ = ' ';
        } else if (*p == '%' && p[1] && p[2]) {
            int hi = hexval(p[1]), lo = hexval(p[2]);
            if (hi >= 0 && lo >= 0) { *o++ = (char)(hi * 16 + lo); p += 2; }
            else *o++ = *p;
        } else {
            *o++ = *p;
        }
    }
    *o = '\0';
}

static esp_err_t save_post(httpd_req_t *r)
{
    char buf[256];
    int len = r->content_len < (int)sizeof(buf) - 1 ? r->content_len : (int)sizeof(buf) - 1;
    int got = 0;
    while (got < len) {
        int x = httpd_req_recv(r, buf + got, len - got);
        if (x <= 0) break;
        got += x;
    }
    buf[got] = '\0';

    char ssid[33] = {0}, pass[65] = {0};
    httpd_query_key_value(buf, "ssid", ssid, sizeof(ssid));
    httpd_query_key_value(buf, "pass", pass, sizeof(pass));
    url_decode(ssid);
    url_decode(pass);

    strlcpy(s_ssid, ssid, sizeof(s_ssid));
    strlcpy(s_pass, pass, sizeof(s_pass));
    s_submitted = true;
    ESP_LOGI(TAG, "received credentials for \"%s\"", s_ssid);

    httpd_resp_set_type(r, "text/html");
    return httpd_resp_send(r, DONE_HTML, HTTPD_RESP_USE_STRLEN);
}

void prov_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard;   // catch captive-portal probes
    cfg.lru_purge_enable = true;
    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd start failed");
        return;
    }
    httpd_uri_t root = { .uri = "/",      .method = HTTP_GET,  .handler = serve_form };
    httpd_uri_t save = { .uri = "/save",  .method = HTTP_POST, .handler = save_post };
    httpd_uri_t any  = { .uri = "/*",     .method = HTTP_GET,  .handler = serve_form };
    httpd_register_uri_handler(s_server, &root);
    httpd_register_uri_handler(s_server, &save);
    httpd_register_uri_handler(s_server, &any);
    s_submitted = false;
    ESP_LOGI(TAG, "provisioning server started");
}

void prov_stop(void)
{
    if (s_server) { httpd_stop(s_server); s_server = NULL; }
}

bool prov_take_creds(char *ssid, int ssid_len, char *pass, int pass_len)
{
    if (!s_submitted) return false;
    s_submitted = false;
    strlcpy(ssid, s_ssid, ssid_len);
    strlcpy(pass, s_pass, pass_len);
    return true;
}
