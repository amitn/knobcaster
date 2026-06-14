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
#include "esp_timer.h"
#include "nvs_flash.h"

#include "wifi.h"
#include "cast_discovery.h"
#include "cast_session.h"

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

static const char *player_state_str(cast_player_state_t st)
{
    switch (st) {
    case CAST_PLAYER_PLAYING:   return "PLAYING";
    case CAST_PLAYER_PAUSED:    return "PAUSED";
    case CAST_PLAYER_BUFFERING: return "BUFFERING";
    case CAST_PLAYER_IDLE:      return "IDLE";
    default:                    return "?";
    }
}

// Open a session to a device and stream its now-playing + volume to the log
// until the connection drops. (UI + multi-device control come in later
// milestones; this validates the M3 read path.)
static void run_session(const cast_device_t *dev)
{
    ESP_LOGI(TAG, "opening session to %s (" IPSTR ")...",
             dev->friendly_name, IP2STR(&dev->ip));
    cast_session_t *s = cast_session_open(dev);
    if (!s) { ESP_LOGW(TAG, "session open failed"); return; }

    int64_t last_log = 0;
    for (;;) {
        if (!cast_session_poll(s, 500)) {
            ESP_LOGW(TAG, "session closed");
            break;
        }
        int64_t now = esp_timer_get_time();
        if (now - last_log >= 2000000) {  // log a snapshot every ~2s
            last_log = now;
            cast_media_status_t m;  cast_session_get_media(s, &m);
            cast_volume_status_t v; cast_session_get_volume(s, &v);
            ESP_LOGI(TAG, "[%s] %-8s  \"%s\" - \"%s\"  vol=%d%%%s",
                     m.app_name[0] ? m.app_name : "-",
                     player_state_str(m.state),
                     m.title, m.subtitle,
                     (int)(v.level * 100 + 0.5f), v.muted ? " (muted)" : "");
        }
    }
    cast_session_close(s);
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
    for (;;) {
        if (!wifi_is_connected()) {
            ESP_LOGI(TAG, "Wi-Fi down, waiting to reconnect...");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

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

        // Stream the first device's now-playing until its connection drops,
        // then rescan. (Device selection / UI arrive in later milestones.)
        if (n > 0) {
            run_session(&devices[0]);
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
