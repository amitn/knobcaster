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
#include "esp_mac.h"
#include "nvs_flash.h"

#include "wifi.h"
#include "prov.h"
#include "cast_discovery.h"
#include "cast_session.h"
#include "display.h"
#include "knob.h"
#include "fbdump.h"
#include "ui.h"

// Optional compiled-in Wi-Fi creds (include/secrets.h). If absent, the device
// falls back to on-device web provisioning (SoftAP + QR + form).
#if defined(__has_include)
#  if __has_include("secrets.h")
#    include "secrets.h"
#  endif
#endif
#ifndef WIFI_SSID
#  define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#  define WIFI_PASS ""
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

// Dial mode: the knob either sets volume or picks the active speaker.
typedef enum { DIAL_VOLUME = 0, DIAL_SPEAKERS } dial_mode_t;
static dial_mode_t g_dial_mode;
static int         g_sel_preview;   // previewed device index while in DIAL_SPEAKERS

typedef enum {
    SESSION_CLOSED    = 0,  // connection dropped -> rescan
    SESSION_SWITCHED  = 1,  // user swiped -> reopen new active device
    SESSION_OPEN_LIST = 2,  // user tapped center -> show device list
} session_result_t;

static void members_overlay(cast_session_t *s);   // group member volumes

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

    g_dial_mode = DIAL_VOLUME;          // each session starts in Volume mode
    ui_set_dial_mode(false);

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
        // Center tap -> group members (if a group) else the device-list overlay.
        if (ui_take_center_tap()) {
            if (cast_session_is_group(s) && cast_session_member_count(s) > 0) {
                members_overlay(s);              // keeps this session open
            } else if (g_count > 1) {
                cast_session_close(s);
                return SESSION_OPEN_LIST;
            }
        }

        // Knob short press -> toggle dial mode (Volume <-> Speakers). Leaving
        // Speakers mode commits the previewed device (reopens its session).
        if (knob_take_pressed()) {
            if (g_dial_mode == DIAL_VOLUME) {
                g_dial_mode = DIAL_SPEAKERS;
                g_sel_preview = g_active;
                ui_set_dial_mode(true);
                ui_set_now_playing(g_devices[g_sel_preview].friendly_name, "select speaker", "", -1);
            } else {
                g_dial_mode = DIAL_VOLUME;
                ui_set_dial_mode(false);
                if (g_sel_preview != g_active) {
                    g_active = g_sel_preview;
                    ESP_LOGI(TAG, "speaker picked -> %s", g_devices[g_active].friendly_name);
                    cast_session_close(s);
                    return SESSION_SWITCHED;
                }
            }
        }

        // Knob rotation -> volume (Volume mode) or speaker preview (Speakers mode).
        int detents = knob_take_delta();
        if (detents != 0) {
            if (g_dial_mode == DIAL_VOLUME) {
                cast_session_step_volume(s, detents * 0.03f);  // ~3% per detent
                cast_volume_status_t v; cast_session_get_volume(s, &v);
                ui_set_now_playing(NULL, NULL, NULL, (int)(v.level * 100 + 0.5f));
            } else if (g_count > 0) {
                g_sel_preview = ((g_sel_preview + detents) % g_count + g_count) % g_count;
                ui_set_now_playing(g_devices[g_sel_preview].friendly_name, "select speaker", "", -1);
            }
        }
        // Knob long press -> mute toggle.
        if (knob_take_long_pressed()) {
            cast_volume_status_t v; cast_session_get_volume(s, &v);
            ESP_LOGI(TAG, "knob long-press -> %s", v.muted ? "unmute" : "mute");
            cast_session_set_muted(s, !v.muted);
        }
        // On-screen transport buttons.
        switch (ui_take_transport()) {
        case UI_TRANSPORT_PREV:      cast_session_prev(s);         break;
        case UI_TRANSPORT_NEXT:      cast_session_next(s);         break;
        case UI_TRANSPORT_PLAYPAUSE: cast_session_toggle_pause(s); break;
        default: break;
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
            // Don't clobber the speaker-preview labels while picking.
            if (g_dial_mode == DIAL_VOLUME) {
                ui_set_now_playing(dev->friendly_name,
                                   m.title[0] ? m.title : player_state_str(m.state),
                                   m.subtitle, vol_pct);
                ui_set_muted(v.muted);
                ui_set_playing(m.state == CAST_PLAYER_PLAYING);
                ui_set_transport_enabled(m.supports_prev, m.supports_pause, m.supports_next);
            }
            ui_set_wifi(true);
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

// Bring up the SoftAP + web form and block until the user submits credentials
// that successfully connect. Saves working creds to NVS.
static void run_provisioning(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char ap[24];
    snprintf(ap, sizeof(ap), "CastKnob-%02X%02X", mac[4], mac[5]);
    char qr[64];
    snprintf(qr, sizeof(qr), "WIFI:S:%s;T:nopass;;", ap);   // scan to join the AP

    ESP_LOGI(TAG, "Wi-Fi provisioning: AP \"%s\"", ap);
    wifi_forget();          // stop retrying the old network while provisioning
    wifi_start_ap(ap);
    prov_start();
    ui_prov_show(qr, ap);
    ui_set_wifi(false);

    char ssid[33], pass[65];
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(200));
        if (!prov_take_creds(ssid, sizeof(ssid), pass, sizeof(pass))) continue;

        ui_prov_hide();
        ui_set_now_playing("Wi-Fi", "connecting...", ssid, -1);
        prov_stop();
        wifi_stop_ap();
        wifi_connect_to(ssid, pass);
        if (wifi_wait_connected(15000)) {
            wifi_creds_save(ssid, pass);     // remember for next boot
            return;
        }
        ESP_LOGW(TAG, "connect failed; reopening provisioning portal");
        wifi_start_ap(ap);
        prov_start();
        ui_prov_show(qr, ap);
    }
}

// Group members overlay: list the active group's speakers; the knob adjusts the
// highlighted member's volume (the group session stays open for live updates).
static void members_overlay(cast_session_t *s)
{
    int n = cast_session_member_count(s);
    if (n <= 0) return;

    static char buf[CAST_MAX_MEMBERS][96];
    const char *labels[CAST_MAX_MEMBERS];
    for (int i = 0; i < n; i++) {
        cast_member_t m; cast_session_get_member(s, i, &m);
        snprintf(buf[i], sizeof(buf[i]), "%s  %d%%%s", m.name[0] ? m.name : "member",
                 (int)(m.level * 100 + 0.5f), m.muted ? "  M" : "");
        labels[i] = buf[i];
    }
    ui_devlist_show(labels, n, 0);

    int sel = 0;
    int64_t t0 = esp_timer_get_time();
    for (;;) {
        if (!cast_session_poll(s, 50)) break;     // keep the group session alive

        int d = knob_take_delta();
        if (d != 0) { cast_session_step_member_volume(s, sel, d * 0.03f); t0 = esp_timer_get_time(); }
        int tapped = ui_devlist_take_tap();
        if (tapped >= 0) { sel = tapped; ui_devlist_set_sel(sel); t0 = esp_timer_get_time(); }
        if (ui_devlist_take_cancel()) break;
        if (knob_take_pressed())      break;
        if (esp_timer_get_time() - t0 > 15000000) break;

        // Reflect the latest member volumes (knob changes + DEVICE_UPDATED pushes).
        int cur = cast_session_member_count(s);
        for (int i = 0; i < n && i < cur; i++) {
            cast_member_t m; cast_session_get_member(s, i, &m);
            snprintf(buf[i], sizeof(buf[i]), "%s  %d%%%s", m.name[0] ? m.name : "member",
                     (int)(m.level * 100 + 0.5f), m.muted ? "  M" : "");
            ui_devlist_set_row(i, buf[i]);
        }
    }
    ui_devlist_hide();
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
    fbdump_start();   // `just shot` -> screen.png

    wifi_init();

    // Prefer saved creds (NVS), then compiled-in secrets; otherwise provision.
    char ssid[33] = {0}, pass[65] = {0};
    bool have = wifi_creds_load(ssid, sizeof(ssid), pass, sizeof(pass));
    if (!have && WIFI_SSID[0]) {
        strlcpy(ssid, WIFI_SSID, sizeof(ssid));
        strlcpy(pass, WIFI_PASS, sizeof(pass));
        have = true;
    }
    if (have) {
        ui_set_now_playing("Cast Knob", "connecting...", ssid, -1);
        wifi_connect_to(ssid, pass);
    }
    if (!wifi_wait_connected(have ? 15000 : 1)) {
        run_provisioning();                 // SoftAP + QR + web form
    }
    ui_set_wifi(true);

    cast_discovery_init();

    for (;;) {
        if (!wifi_is_connected()) {
            ESP_LOGI(TAG, "Wi-Fi down, waiting to reconnect...");
            ui_set_wifi(false);
            ui_set_now_playing("Cast Knob", "reconnecting...", "", -1);
            if (!wifi_wait_connected(20000)) {
                run_provisioning();         // persistent failure -> portal
            }
            ui_set_wifi(true);
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
