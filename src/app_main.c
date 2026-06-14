// ESP32-S3 Cast Knob — application entry point.
//
// Milestone M0/M2: boot, bring up NVS + event loop, join Wi-Fi, and log a
// heartbeat. Display/LVGL HAL and the Cast layer are added in later milestones
// (see docs/06-roadmap.md).
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"

#include "wifi.h"
#include "cast_discovery.h"
#include "cast_connection.h"

// Wi-Fi credentials. Copy include/secrets.h.example -> include/secrets.h.
#if defined(__has_include)
#  if __has_include("secrets.h")
#    include "secrets.h"
#  endif
#endif
#ifndef WIFI_SSID
#  error "Create include/secrets.h (copy from secrets.h.example) with WIFI_SSID / WIFI_PASS"
#endif

static const char *TAG = "app";

// One-shot probe: open a CASTV2 connection, do the connection handshake, ask
// for receiver status, and log whatever the device sends back. Proves the TLS +
// framing + protobuf pipe end-to-end. Full session/parse comes in M3 part 2.
static void probe_device(const cast_device_t *dev)
{
    ESP_LOGI(TAG, "probing %s (" IPSTR ":%u)...",
             dev->friendly_name, IP2STR(&dev->ip), dev->port);

    cast_conn_t *c = cast_conn_open(dev->ip, dev->port ? dev->port : CAST_PORT);
    if (!c) { ESP_LOGW(TAG, "connect failed"); return; }

    cast_conn_send(c, CAST_SRC_DEFAULT, CAST_DST_RECEIVER,
                   CAST_NS_CONNECTION, "{\"type\":\"CONNECT\"}");
    cast_conn_send(c, CAST_SRC_DEFAULT, CAST_DST_RECEIVER,
                   CAST_NS_RECEIVER, "{\"type\":\"GET_STATUS\",\"requestId\":1}");

    for (int i = 0; i < 6; i++) {
        cast_msg_t msg;
        if (!cast_conn_recv(c, &msg, 4000)) break;
        ESP_LOGI(TAG, "  <- ns=%s  payload=%.*s", msg.ns,
                 (int)msg.payload_len, msg.payload ? (const char *)msg.payload : "");
    }
    cast_conn_close(c);
}

static void init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, " ESP32-S3 Cast Knob  (build %s %s)", __DATE__, __TIME__);
    ESP_LOGI(TAG, "========================================");

    init_nvs();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_start(WIFI_SSID, WIFI_PASS);
    if (!wifi_wait_connected(30000)) {
        ESP_LOGW(TAG, "Wi-Fi not up after 30s; will keep retrying in background");
    }

    cast_discovery_init();

    // Periodically discover Cast devices and log them. (Cast connection /
    // now-playing / control and the LVGL UI come in later milestones.)
    static cast_device_t devices[CAST_MAX_DEVICES];
    bool probed = false;
    for (;;) {
        if (wifi_is_connected()) {
            int n = cast_discovery_scan(devices, CAST_MAX_DEVICES, 3000);
            ESP_LOGI(TAG, "discovered %d Cast device(s)  [heap=%" PRIu32 "B psram=%dB]",
                     n, esp_get_free_heap_size(),
                     (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            for (int i = 0; i < n; i++) {
                ESP_LOGI(TAG, "  [%d] %-24s %-20s " IPSTR ":%u%s",
                         i, devices[i].friendly_name, devices[i].model,
                         IP2STR(&devices[i].ip), devices[i].port,
                         devices[i].is_group ? " (group)" : "");
            }
            // One-shot connection probe against the first device found.
            if (n > 0 && !probed) {
                probed = true;
                probe_device(&devices[0]);
            }
        } else {
            ESP_LOGI(TAG, "Wi-Fi down, waiting to reconnect...");
        }
        vTaskDelay(pdMS_TO_TICKS(15000));
    }
}
