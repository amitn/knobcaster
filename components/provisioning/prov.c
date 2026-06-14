#include "prov.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "prov";

static httpd_handle_t s_server;
static char           s_ssid[33];
static char           s_pass[65];
static volatile bool  s_submitted;
static volatile bool  s_dns_run;

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

// Captive-portal DNS: answer every A query with 192.168.4.1 so the phone's
// connectivity check fails to the real internet and pops the setup page.
static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) { ESP_LOGE(TAG, "dns socket failed"); vTaskDelete(NULL); return; }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "dns bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };   // so we can poll s_dns_run
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t buf[320];
    while (s_dns_run) {
        struct sockaddr_in src;
        socklen_t sl = sizeof(src);
        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&src, &sl);
        if (n < 12) continue;                            // too small / timeout

        // Find end of QNAME, then read QTYPE.
        int p = 12;
        while (p < n && buf[p] != 0) p += buf[p] + 1;
        if (p >= n) continue;
        p += 1;                                          // skip the null label
        if (p + 4 > n) continue;
        int qtype = (buf[p] << 8) | buf[p + 1];
        p += 4;                                          // skip QTYPE + QCLASS

        buf[2] |= 0x80;                                  // QR = response
        buf[3] = (buf[3] & 0x78);                        // RCODE 0, clear RA/Z
        buf[7] = (qtype == 1) ? 1 : 0;                   // ANCOUNT (only for A)
        buf[8] = buf[9] = buf[10] = buf[11] = 0;         // NS/AR counts

        if (qtype == 1 && p + 16 <= (int)sizeof(buf)) {
            uint8_t ans[] = {
                0xC0, 0x0C,             // name -> pointer to the question
                0x00, 0x01,             // type A
                0x00, 0x01,             // class IN
                0x00, 0x00, 0x00, 0x3C, // TTL 60s
                0x00, 0x04,             // RDLENGTH
                192, 168, 4, 1,         // 192.168.4.1
            };
            memcpy(buf + p, ans, sizeof(ans));
            p += sizeof(ans);
        }
        sendto(sock, buf, p, 0, (struct sockaddr *)&src, sl);
    }
    close(sock);
    vTaskDelete(NULL);
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

    if (!s_dns_run) {
        s_dns_run = true;
        xTaskCreate(dns_task, "captive_dns", 4096, NULL, 5, NULL);
    }
    ESP_LOGI(TAG, "provisioning server started (HTTP + captive DNS)");
}

void prov_stop(void)
{
    s_dns_run = false;   // dns_task exits within ~1s (recv timeout) and self-deletes
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
