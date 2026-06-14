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
#include "display.h"
#include "knob.h"
#include "ui.h"

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

// Discovered devices and the currently-selected one (swipe changes g_active).
static cast_device_t g_devices[CAST_MAX_DEVICES];
static int           g_count;
static int           g_active;

typedef enum { SESSION_CLOSED = 0, SESSION_SWITCHED = 1 } session_result_t;

// Open a session to the active device and stream its now-playing to the screen,
// handling knob (volume / play-pause) and swipe (switch device). Returns
// SESSION_SWITCHED if the user swiped (g_active already advanced — reopen), or
// SESSION_CLOSED if the connection dropped.
static session_result_t run_session(void)
{
    const cast_device_t *dev = &g_devices[g_active];
    ESP_LOGI(TAG, "session -> [%d/%d] %s (" IPSTR ")", g_active + 1, g_count,
             dev->friendly_name, IP2STR(&dev->ip));
    ui_set_now_playing(dev->friendly_name, "connecting...", "", -1);

    cast_session_t *s = cast_session_open(dev);
    if (!s) {
        ESP_LOGW(TAG, "session open failed");
        vTaskDelay(pdMS_TO_TICKS(1000));
        return SESSION_CLOSED;
    }

    int64_t last_log = 0;
    for (;;) {
        if (!cast_session_poll(s, 100)) {
            ESP_LOGW(TAG, "session closed");
            cast_session_close(s);
            return SESSION_CLOSED;
        }

        // Swipe left/right -> switch active device (immediate reconnect).
        int sw = ui_take_swipe();
        if (sw != 0 && g_count > 1) {
            g_active = (g_active + sw + g_count) % g_count;
            ESP_LOGI(TAG, "swipe %s -> device %d", sw > 0 ? "next" : "prev", g_active);
            cast_session_close(s);
            return SESSION_SWITCHED;
        }

        // Knob rotation -> volume (optimistic; arc follows immediately).
        int detents = knob_take_delta();
        if (detents != 0) {
            cast_session_step_volume(s, detents * 0.03f);  // ~3% per detent
            cast_volume_status_t v; cast_session_get_volume(s, &v);
            ui_set_now_playing(NULL, NULL, NULL, (int)(v.level * 100 + 0.5f));
        }
        // Knob short press -> play/pause; long press -> mute toggle.
        if (knob_take_pressed()) {
            ESP_LOGI(TAG, "knob press -> toggle play/pause");
            cast_session_toggle_pause(s);
        }
        if (knob_take_long_pressed()) {
            cast_volume_status_t v; cast_session_get_volume(s, &v);
            ESP_LOGI(TAG, "knob long-press -> %s", v.muted ? "unmute" : "mute");
            cast_session_set_muted(s, !v.muted);
        }

        int64_t now = esp_timer_get_time();
        if (now - last_log >= 2000000) {  // refresh log + screen every ~2s
            last_log = now;
            cast_media_status_t m;  cast_session_get_media(s, &m);
            cast_volume_status_t v; cast_session_get_volume(s, &v);
            int vol_pct = (int)(v.level * 100 + 0.5f);
            ESP_LOGI(TAG, "[%s] %-8s  \"%s\" - \"%s\"  vol=%d%%%s",
                     m.app_name[0] ? m.app_name : "-",
                     player_state_str(m.state),
                     m.title, m.subtitle, vol_pct, v.muted ? " (muted)" : "");
            ui_set_now_playing(dev->friendly_name,
                               m.title[0] ? m.title : player_state_str(m.state),
                               m.subtitle, vol_pct);
        }
    }
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

    // Bring up the screen first so there's visible feedback during Wi-Fi join.
    lv_display_t *disp = display_init();
    touch_init(disp);
    knob_init();
    ui_init();

    wifi_start(WIFI_SSID, WIFI_PASS);
    if (!wifi_wait_connected(30000)) {
        ESP_LOGW(TAG, "Wi-Fi not up after 30s; will keep retrying in background");
    }

    cast_discovery_init();

    for (;;) {
        if (!wifi_is_connected()) {
            ESP_LOGI(TAG, "Wi-Fi down, waiting to reconnect...");
            ui_set_now_playing("Cast Knob", "Wi-Fi...", "", -1);
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        g_count = cast_discovery_scan(g_devices, CAST_MAX_DEVICES, 3000);
        ESP_LOGI(TAG, "discovered %d Cast device(s)  [heap=%" PRIu32 "B psram=%dB]",
                 g_count, esp_get_free_heap_size(),
                 (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        for (int i = 0; i < g_count; i++) {
            ESP_LOGI(TAG, "  [%d] %-24s %-20s " IPSTR ":%u%s",
                     i, g_devices[i].friendly_name, g_devices[i].model,
                     IP2STR(&g_devices[i].ip), g_devices[i].port,
                     g_devices[i].is_group ? " (group)" : "");
        }

        if (g_count == 0) {
            ui_set_now_playing("Cast Knob", "no speakers found", "swipe = device", -1);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        if (g_active >= g_count) g_active = 0;

        // Run the active device; on swipe, reopen the newly-selected device
        // immediately (no rescan). Only a dropped connection breaks out to rescan.
        while (run_session() == SESSION_SWITCHED) {
            /* g_active updated; reopen */
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
