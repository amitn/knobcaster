#include "wifi.h"

#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "wifi";

#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t s_events;
static esp_netif_t       *s_netif;
static esp_netif_t       *s_ap_netif;
static esp_ip4_addr_t     s_ip;
static int                s_retry;
static bool               s_have_config;   // a connect target has been set

static int backoff_ms(int retry)
{
    int ms = 500 << (retry > 5 ? 5 : retry); // 0.5s .. 16s
    return ms > 16000 ? 16000 : ms;
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_START) {
        if (s_have_config) esp_wifi_connect();
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_events, WIFI_CONNECTED_BIT);
        s_ip.addr = 0;
        if (!s_have_config) return;        // not trying to connect (e.g. provisioning)
        int delay = backoff_ms(s_retry);
        ESP_LOGW(TAG, "disconnected; retry #%d in %d ms", s_retry + 1, delay);
        s_retry++;
        vTaskDelay(pdMS_TO_TICKS(delay));
        esp_wifi_connect();
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        s_ip = event->ip_info.ip;
        s_retry = 0;
        ESP_LOGI(TAG, "connected, IP=" IPSTR, IP2STR(&s_ip));
        xEventGroupSetBits(s_events, WIFI_CONNECTED_BIT);
    }
}

void wifi_init(void)
{
    s_events = xEventGroupCreate();
    s_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &on_ip_event, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void wifi_connect_to(const char *ssid, const char *pass)
{
    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, pass, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    esp_wifi_disconnect();
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    s_have_config = true;
    s_retry = 0;
    ESP_LOGI(TAG, "connecting to SSID \"%s\"", ssid);
    esp_wifi_connect();
}

bool wifi_is_connected(void)
{
    return s_events && (xEventGroupGetBits(s_events) & WIFI_CONNECTED_BIT);
}

bool wifi_wait_connected(int timeout_ms)
{
    if (!s_events) return false;
    EventBits_t bits = xEventGroupWaitBits(
        s_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

esp_ip4_addr_t wifi_get_ip(void)
{
    return s_ip;
}

void wifi_start_ap(const char *ssid)
{
    if (!s_ap_netif) s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_config_t ap = { 0 };
    strlcpy((char *)ap.ap.ssid, ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;   // open provisioning AP

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_LOGI(TAG, "SoftAP \"%s\" up (http://192.168.4.1)", ssid);
}

void wifi_stop_ap(void)
{
    esp_wifi_set_mode(WIFI_MODE_STA);
    ESP_LOGI(TAG, "SoftAP stopped");
}

int wifi_scan(wifi_ap_t *out, int max, int timeout_ms)
{
    (void)timeout_ms;
    wifi_scan_config_t sc = { .show_hidden = false };
    if (esp_wifi_scan_start(&sc, true) != ESP_OK) return 0;  // blocking

    uint16_t num = max;
    wifi_ap_record_t *recs = calloc(max, sizeof(wifi_ap_record_t));
    if (!recs) { esp_wifi_clear_ap_list(); return 0; }
    esp_wifi_scan_get_ap_records(&num, recs);

    int n = 0;
    for (int i = 0; i < num && n < max; i++) {
        const char *ssid = (const char *)recs[i].ssid;
        if (ssid[0] == '\0') continue;                 // hidden
        bool dup = false;
        for (int j = 0; j < n; j++)
            if (strcmp(out[j].ssid, ssid) == 0) { dup = true; break; }
        if (dup) continue;
        strlcpy(out[n].ssid, ssid, sizeof(out[n].ssid));
        out[n].rssi = recs[i].rssi;
        out[n].secure = recs[i].authmode != WIFI_AUTH_OPEN;
        n++;
    }
    free(recs);
    return n;
}

bool wifi_creds_load(char *ssid, int ssid_len, char *pass, int pass_len)
{
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READONLY, &h) != ESP_OK) return false;
    size_t sl = ssid_len, pl = pass_len;
    esp_err_t e1 = nvs_get_str(h, "ssid", ssid, &sl);
    esp_err_t e2 = nvs_get_str(h, "pass", pass, &pl);
    nvs_close(h);
    return e1 == ESP_OK && e2 == ESP_OK && ssid[0];
}

void wifi_creds_save(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open("wifi", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, "ssid", ssid);
    nvs_set_str(h, "pass", pass);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "saved credentials for \"%s\"", ssid);
}
