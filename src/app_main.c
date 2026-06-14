// ESP32-S3 Cast Knob — application entry point.
//
// Milestone M0/M2: boot, bring up NVS + event loop, join Wi-Fi, and log a
// heartbeat. Display/LVGL HAL and the Cast layer are added in later milestones
// (see docs/06-roadmap.md).
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
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

// --- shared state between the net task (owns Cast networking) and the UI task --
// g_devices/g_count/g_active are written by the net task and read by the UI task
// (device list); guard with g_state_mtx. g_active is only ever changed by the net
// task in response to commands.
static cast_device_t g_devices[CAST_MAX_DEVICES];
static int           g_count;
static int           g_active;
static char          g_active_id[CAST_ID_LEN];  // sticky selection across rescans
static SemaphoreHandle_t g_state_mtx;
static volatile int  g_volume_pct;              // for the UI's optimistic arc

// Cached per-device state for the device-list overlay (parallel to g_devices;
// cleared on each rescan). The active device's entry is kept live.
typedef struct {
    bool                valid;
    cast_player_state_t state;
    char                title[96];
    int                 vol_pct;
} dev_cache_t;
static dev_cache_t g_cache[CAST_MAX_DEVICES];

// Commands from the UI task -> net task (applied to the live session).
typedef enum { CMD_VOL, CMD_MUTE, CMD_TRANSPORT, CMD_SELECT, CMD_SWIPE } cmd_kind_t;
typedef struct { cmd_kind_t kind; int arg; } cmd_t;
static QueueHandle_t g_cmd_q;

// Sort discovered devices alphabetically (case-insensitive) for a stable list.
static int dev_name_cmp(const void *a, const void *b)
{
    return strcasecmp(((const cast_device_t *)a)->friendly_name,
                      ((const cast_device_t *)b)->friendly_name);
}

static void state_lock(void)   { xSemaphoreTake(g_state_mtx, portMAX_DELAY); }
static void state_unlock(void) { xSemaphoreGive(g_state_mtx); }
static void cmd_send(cmd_kind_t kind, int arg)
{
    cmd_t c = { kind, arg };
    if (g_cmd_q) xQueueSend(g_cmd_q, &c, 0);
}

typedef enum {
    SESSION_CLOSED    = 0,  // connection dropped -> rescan
    SESSION_SWITCHED  = 1,  // user picked a different device -> reopen
} session_result_t;

static void cache_set(int idx, const cast_media_status_t *m, const cast_volume_status_t *v)
{
    if (idx < 0 || idx >= CAST_MAX_DEVICES) return;
    g_cache[idx].valid = true;
    g_cache[idx].state = m->state;
    g_cache[idx].vol_pct = (int)(v->level * 100 + 0.5f);
    strlcpy(g_cache[idx].title, m->title, sizeof(g_cache[idx].title));
}

// [net task] Open a session to the active device, apply queued UI commands, and
// render now-playing. Returns SESSION_SWITCHED if the user picked another device
// (g_active already updated — reopen) or SESSION_CLOSED if the connection dropped.
static session_result_t run_session(void)
{
    state_lock();
    int active = g_active;
    cast_device_t dev = g_devices[active];
    state_unlock();

    ESP_LOGI(TAG, "session -> [%d/%d] %s (" IPSTR ")", active + 1, g_count,
             dev.friendly_name, IP2STR(&dev.ip));
    ui_set_now_playing(dev.friendly_name, "connecting...", "", -1);

    cast_session_t *s = cast_session_open(&dev);
    if (!s) {
        ESP_LOGW(TAG, "session open failed");
        vTaskDelay(pdMS_TO_TICKS(300));
        return SESSION_CLOSED;
    }

    int64_t last_log = 0;
    for (;;) {
        if (!cast_session_poll(s, 30)) {
            ESP_LOGW(TAG, "session closed");
            cast_session_close(s);
            return SESSION_CLOSED;
        }

        // Apply commands from the UI task.
        cmd_t c;
        while (xQueueReceive(g_cmd_q, &c, 0)) {
            switch (c.kind) {
            case CMD_VOL:
                cast_session_step_volume(s, c.arg * 0.03f);
                break;
            case CMD_MUTE: {
                cast_volume_status_t v; cast_session_get_volume(s, &v);
                cast_session_set_muted(s, !v.muted);
                break;
            }
            case CMD_TRANSPORT:
                if (c.arg == UI_TRANSPORT_PREV)            cast_session_prev(s);
                else if (c.arg == UI_TRANSPORT_NEXT)       cast_session_next(s);
                else if (c.arg == UI_TRANSPORT_PLAYPAUSE)  cast_session_toggle_pause(s);
                break;
            case CMD_SWIPE: {
                state_lock();
                g_active = (g_active + c.arg + g_count) % g_count;
                state_unlock();
                cast_session_close(s);
                return SESSION_SWITCHED;
            }
            case CMD_SELECT: {
                state_lock();
                bool diff = (c.arg != g_active && c.arg >= 0 && c.arg < g_count);
                if (diff) g_active = c.arg;
                state_unlock();
                if (diff) { cast_session_close(s); return SESSION_SWITCHED; }
                break;
            }
            }
        }

        int64_t now = esp_timer_get_time();
        if (now - last_log >= 1000000) {  // refresh log + screen every ~1s
            last_log = now;
            cast_media_status_t m;  cast_session_get_media(s, &m);
            cast_volume_status_t v; cast_session_get_volume(s, &v);
            int vol_pct = (int)(v.level * 100 + 0.5f);
            g_volume_pct = vol_pct;
            ESP_LOGI(TAG, "[%s] %-8s  \"%s\" - \"%s\"  vol=%d%%%s",
                     m.app_name[0] ? m.app_name : "-", player_state_str(m.state),
                     m.title, m.subtitle, vol_pct, v.muted ? " (muted)" : "");
            ui_set_now_playing(dev.friendly_name,
                               m.title[0] ? m.title : player_state_str(m.state),
                               m.subtitle, vol_pct);
            ui_set_muted(v.muted);
            ui_set_playing(m.state == CAST_PLAYER_PLAYING);
            ui_set_transport_enabled(m.supports_prev, m.supports_pause, m.supports_next);
            ui_set_wifi(true);
            state_lock(); cache_set(active, &m, &v); state_unlock();
        }
    }
}

// [UI task] Modal device-list overlay. Snapshots the device list, shows it, and
// returns the chosen index (or -1). The net task keeps the active session alive
// meanwhile, so re-selecting the same device never reconnects.
static int device_list_overlay(void)
{
    static char buf[CAST_MAX_DEVICES][96];
    const char *labels[CAST_MAX_DEVICES];
    state_lock();
    int count = g_count, active = g_active;
    for (int i = 0; i < count; i++) {
        const dev_cache_t *c = &g_cache[i];
        const char *mark = (i == active) ? "> " : "";
        if (c->valid)
            snprintf(buf[i], sizeof(buf[i]), "%s%.60s  %.10s %d%%",
                     mark, g_devices[i].friendly_name, player_state_str(c->state), c->vol_pct);
        else
            snprintf(buf[i], sizeof(buf[i]), "%s%.80s", mark, g_devices[i].friendly_name);
        labels[i] = buf[i];
    }
    state_unlock();
    if (count == 0) return -1;

    int sel = active;
    ui_devlist_show(labels, count, sel);

    int chosen = -1;
    int64_t t0 = esp_timer_get_time();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(20));
        int d = knob_take_delta();
        if (d != 0) {
            sel = ((sel + d) % count + count) % count;
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

// [UI task] Read knob/touch and turn it into commands for the net task. Never
// blocks on the network, so the UI stays responsive even while a device connects.
static void ui_input_task(void *arg)
{
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(15));

        // Knob rotation -> volume (optimistic arc immediately + command).
        int d = knob_take_delta();
        if (d != 0) {
            int p = g_volume_pct + d * 3;
            g_volume_pct = p < 0 ? 0 : (p > 100 ? 100 : p);
            ui_set_now_playing(NULL, NULL, NULL, g_volume_pct);
            cmd_send(CMD_VOL, d);
        }
        if (knob_take_long_pressed()) cmd_send(CMD_MUTE, 0);

        ui_transport_t t = ui_take_transport();
        if (t != UI_TRANSPORT_NONE) cmd_send(CMD_TRANSPORT, t);

        int sw = ui_take_swipe();
        if (sw != 0) cmd_send(CMD_SWIPE, sw);

        // Tap device name or press knob -> device list (dial navigates, press selects).
        if (ui_take_center_tap() || knob_take_pressed()) {
            state_lock(); int n = g_count; state_unlock();
            if (n > 1) {
                int chosen = device_list_overlay();
                if (chosen >= 0) cmd_send(CMD_SELECT, chosen);
            }
        }
    }
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

    g_state_mtx = xSemaphoreCreateMutex();
    g_cmd_q = xQueueCreate(16, sizeof(cmd_t));

    init_nvs();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Bring up the screen first so there's visible feedback during Wi-Fi join.
    lv_display_t *disp = display_init();
    touch_init(disp);
    knob_init();
    ui_init();
    fbdump_start();   // `just shot` -> screen.png
    xTaskCreate(ui_input_task, "ui_input", 6144, NULL, 5, NULL);  // responsive input

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

        static cast_device_t scan[CAST_MAX_DEVICES];
        int n = cast_discovery_scan(scan, CAST_MAX_DEVICES, 3000);
        qsort(scan, n, sizeof(scan[0]), dev_name_cmp);   // stable A-Z order
        ESP_LOGI(TAG, "discovered %d Cast device(s)  [heap=%" PRIu32 "B psram=%dB]",
                 n, esp_get_free_heap_size(),
                 (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        for (int i = 0; i < n; i++) {
            ESP_LOGI(TAG, "  [%d] %-24s %-20s " IPSTR ":%u%s",
                     i, scan[i].friendly_name, scan[i].model,
                     IP2STR(&scan[i].ip), scan[i].port, scan[i].is_group ? " (group)" : "");
        }

        // Publish the new list + sticky selection atomically for the UI task.
        state_lock();
        memcpy(g_devices, scan, sizeof(g_devices));
        g_count = n;
        memset(g_cache, 0, sizeof(g_cache));   // device order may have changed
        g_active = 0;
        if (g_active_id[0]) {
            for (int i = 0; i < n; i++) {
                if (strcmp(g_devices[i].id, g_active_id) == 0) { g_active = i; break; }
            }
        }
        state_unlock();

        if (n == 0) {
            ui_set_now_playing("Cast Knob", "no speakers found", "tap name: speakers", -1);
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        // Run the active device; picking another device (list/swipe) reopens it
        // immediately (no rescan). Only a dropped connection breaks out.
        session_result_t r;
        do {
            r = run_session();
        } while (r == SESSION_SWITCHED);
        state_lock();
        strlcpy(g_active_id, g_devices[g_active].id, sizeof(g_active_id));
        state_unlock();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
