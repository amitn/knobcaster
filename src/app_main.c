// ESP32-S3 Cast Knob — application entry point.
//
// Milestone M0/M2: boot, bring up NVS + event loop, join Wi-Fi, and log a
// heartbeat. Display/LVGL HAL and the Cast layer are added in later milestones
// (see docs/06-roadmap.md).
#include <inttypes.h>
#include <string.h>

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
static char          g_active_id[CAST_ID_LEN];  // sticky selection across rescans

// Cached per-device state for the device-list overlay (parallel to g_devices;
// cleared on each rescan). The active device's entry is kept live.
typedef struct {
    bool                valid;
    cast_player_state_t state;
    char                title[96];
    int                 vol_pct;
} dev_cache_t;
static dev_cache_t g_cache[CAST_MAX_DEVICES];

typedef enum {
    SESSION_CLOSED    = 0,  // connection dropped -> rescan
    SESSION_SWITCHED  = 1,  // user swiped -> reopen new active device
    SESSION_OPEN_LIST = 2,  // user tapped center -> show device list
} session_result_t;

static void cache_set(int idx, const cast_media_status_t *m, const cast_volume_status_t *v)
{
    if (idx < 0 || idx >= CAST_MAX_DEVICES) return;
    g_cache[idx].valid = true;
    g_cache[idx].state = m->state;
    g_cache[idx].vol_pct = (int)(v->level * 100 + 0.5f);
    strlcpy(g_cache[idx].title, m->title, sizeof(g_cache[idx].title));
}

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
        // Center tap -> open the device-list overlay.
        if (ui_take_center_tap() && g_count > 1) {
            cast_session_close(s);
            return SESSION_OPEN_LIST;
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
            ui_set_muted(v.muted);
            cache_set(g_active, &m, &v);
        }
    }
}

// Briefly connect to a device to populate its cached state for the list.
static void cache_refresh(int idx)
{
    cast_session_t *s = cast_session_open(&g_devices[idx]);
    if (!s) { g_cache[idx].valid = false; return; }
    int64_t t0 = esp_timer_get_time();
    while (esp_timer_get_time() - t0 < 800000) {   // ~0.8s to receive status
        if (!cast_session_poll(s, 200)) break;
    }
    cast_media_status_t m;  cast_session_get_media(s, &m);
    cast_volume_status_t v; cast_session_get_volume(s, &v);
    cache_set(idx, &m, &v);
    cast_session_close(s);
}

// Modal device-list overlay. Returns the chosen device index, or -1 to keep the
// current one. Knob rotates the highlight / press selects; tap a row to select;
// tap the background or wait 12 s to cancel.
static int device_list_overlay(void)
{
    // Populate any devices we haven't cached yet (active stays live-cached).
    for (int i = 0; i < g_count; i++) {
        if (i == g_active || g_cache[i].valid) continue;
        ui_set_now_playing(NULL, "scanning...", g_devices[i].friendly_name, -1);
        cache_refresh(i);
    }

    static char buf[CAST_MAX_DEVICES][64];
    const char *labels[CAST_MAX_DEVICES];
    for (int i = 0; i < g_count; i++) {
        const dev_cache_t *c = &g_cache[i];
        const char *st = c->valid ? player_state_str(c->state) : "-";
        snprintf(buf[i], sizeof(buf[i]), "%s  %s  %d%%",
                 g_devices[i].friendly_name, st, c->valid ? c->vol_pct : 0);
        labels[i] = buf[i];
    }

    int sel = g_active;
    ui_devlist_show(labels, g_count, sel);

    int chosen = -1;
    int64_t t0 = esp_timer_get_time();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(30));
        int d = knob_take_delta();
        if (d != 0) {
            sel = ((sel + d) % g_count + g_count) % g_count;
            ui_devlist_set_sel(sel);
            t0 = esp_timer_get_time();
        }
        if (knob_take_pressed())          { chosen = sel; break; }
        int tapped = ui_devlist_take_tap();
        if (tapped >= 0)                  { chosen = tapped; break; }
        if (ui_devlist_take_cancel())     { chosen = -1; break; }
        if (esp_timer_get_time() - t0 > 12000000) { chosen = -1; break; }
    }
    ui_devlist_hide();
    return chosen;
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

        memset(g_cache, 0, sizeof(g_cache));  // device order may have changed

        if (g_count == 0) {
            ui_set_now_playing("Cast Knob", "no speakers found", "swipe = device", -1);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        // Sticky selection: keep the same device active across rescans (the
        // list may reorder) by matching its id; default to the first device.
        g_active = 0;
        if (g_active_id[0]) {
            for (int i = 0; i < g_count; i++) {
                if (strcmp(g_devices[i].id, g_active_id) == 0) { g_active = i; break; }
            }
        }

        // Run the active device; swipe or device-list selection reopens the new
        // active device immediately (no rescan). Only a dropped connection
        // breaks out to rescan.
        session_result_t r;
        do {
            r = run_session();
            if (r == SESSION_OPEN_LIST) {
                int chosen = device_list_overlay();
                if (chosen >= 0) g_active = chosen;
                r = SESSION_SWITCHED;  // reopen the (possibly new) active device
            }
        } while (r == SESSION_SWITCHED);
        strlcpy(g_active_id, g_devices[g_active].id, sizeof(g_active_id));
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
