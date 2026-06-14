#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"
#include "esp_lvgl_port.h"

// Widgets we update at runtime.
static lv_obj_t *s_device_lbl;
static lv_obj_t *s_title_lbl;
static lv_obj_t *s_subtitle_lbl;
static lv_obj_t *s_vol_arc;
static lv_obj_t *s_hint_lbl;
static lv_obj_t *s_wifi_lbl;   // Wi-Fi status icon (top)
static lv_obj_t *s_prev_btn, *s_play_btn, *s_next_btn;
static lv_obj_t *s_play_lbl;   // label inside the play/pause button

// Per-device volume-arc color (set by ui_set_volume_color). Mute overrides it
// with red; unmuting restores this. Default blue until a device is selected.
static uint32_t s_vol_color = 0x1E88E5;
static bool     s_muted;

static volatile ui_transport_t s_transport;

static void transport_cb(lv_event_t *e)
{
    s_transport = (ui_transport_t)(intptr_t)lv_event_get_user_data(e);
}

// Create a round transport button with a symbol; returns the button.
static lv_obj_t *make_transport_btn(lv_obj_t *parent, const char *sym,
                                    int x_off, ui_transport_t act)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, 56, 44);
    lv_obj_set_style_radius(b, 22, 0);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, x_off, -88);
    lv_obj_add_event_cb(b, transport_cb, LV_EVENT_CLICKED, (void *)(intptr_t)act);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, sym);
    lv_obj_center(l);
    return b;
}

static void set_btn_enabled(lv_obj_t *b, bool en)
{
    if (en) {
        lv_obj_remove_state(b, LV_STATE_DISABLED);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    } else {
        lv_obj_add_state(b, LV_STATE_DISABLED);     // dimmed by the theme
        lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
    }
}

// Pending swipe direction, consumed by ui_take_swipe().
static volatile int s_swipe;
// Pending center tap, consumed by ui_take_center_tap().
static volatile bool s_center_tap;

static void swipe_cb(lv_event_t *e)
{
    (void)e;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if (dir == LV_DIR_LEFT)       s_swipe = +1;  // next device
    else if (dir == LV_DIR_RIGHT) s_swipe = -1;  // previous device
}

// Tap on the device name -> open the device list.
static void device_tap_cb(lv_event_t *e)
{
    (void)e;
    s_center_tap = true;
}

void ui_init(void)
{
    lvgl_port_lock(0);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, swipe_cb, LV_EVENT_GESTURE, NULL);

    // Volume ring around the round display.
    s_vol_arc = lv_arc_create(scr);
    lv_obj_set_size(s_vol_arc, 340, 340);
    lv_obj_center(s_vol_arc);
    lv_arc_set_rotation(s_vol_arc, 135);
    lv_arc_set_bg_angles(s_vol_arc, 0, 270);
    lv_arc_set_range(s_vol_arc, 0, 100);
    lv_arc_set_value(s_vol_arc, 0);
    lv_obj_remove_style(s_vol_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(s_vol_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(s_vol_arc, lv_color_hex(s_vol_color), LV_PART_INDICATOR);

    // Wi-Fi status icon (very top).
    s_wifi_lbl = lv_label_create(scr);
    lv_label_set_text(s_wifi_lbl, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(s_wifi_lbl, lv_color_hex(0xCC3333), 0);
    lv_obj_align(s_wifi_lbl, LV_ALIGN_TOP_MID, 0, 40);

    // Device name (top) — tapping it opens the device list.
    s_device_lbl = lv_label_create(scr);
    lv_obj_set_style_text_color(s_device_lbl, lv_color_white(), 0);
    lv_label_set_text(s_device_lbl, "Cast Knob");
    lv_obj_align(s_device_lbl, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_add_flag(s_device_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_device_lbl, 24);   // larger tap target
    lv_obj_add_event_cb(s_device_lbl, device_tap_cb, LV_EVENT_CLICKED, NULL);

    // Title (center).
    s_title_lbl = lv_label_create(scr);
    lv_obj_set_style_text_color(s_title_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_title_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_title_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_title_lbl, 240);
    lv_label_set_text(s_title_lbl, "ready");
    lv_obj_center(s_title_lbl);

    // Subtitle / artist (below title).
    s_subtitle_lbl = lv_label_create(scr);
    lv_obj_set_style_text_color(s_subtitle_lbl, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_align(s_subtitle_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_subtitle_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_subtitle_lbl, 240);
    lv_label_set_text(s_subtitle_lbl, "");
    lv_obj_align(s_subtitle_lbl, LV_ALIGN_CENTER, 0, 28);

    // Transport buttons (prev / play-pause / next).
    s_prev_btn = make_transport_btn(scr, LV_SYMBOL_PREV, -72, UI_TRANSPORT_PREV);
    s_play_btn = make_transport_btn(scr, LV_SYMBOL_PLAY, 0, UI_TRANSPORT_PLAYPAUSE);
    s_next_btn = make_transport_btn(scr, LV_SYMBOL_NEXT, 72, UI_TRANSPORT_NEXT);
    s_play_lbl = lv_obj_get_child(s_play_btn, 0);

    // Hint (bottom).
    s_hint_lbl = lv_label_create(scr);
    lv_obj_set_style_text_color(s_hint_lbl, lv_color_hex(0x666666), 0);
    lv_label_set_text(s_hint_lbl, "tap name: speakers   swipe: device");
    lv_obj_align(s_hint_lbl, LV_ALIGN_BOTTOM_MID, 0, -44);

    lvgl_port_unlock();
}

void ui_set_now_playing(const char *device, const char *title,
                        const char *subtitle, int volume_pct)
{
    lvgl_port_lock(0);
    if (device)   lv_label_set_text(s_device_lbl, device);
    if (title)    lv_label_set_text(s_title_lbl, title);
    if (subtitle) lv_label_set_text(s_subtitle_lbl, subtitle);
    if (volume_pct >= 0) lv_arc_set_value(s_vol_arc, volume_pct);
    lvgl_port_unlock();
}

void ui_set_muted(bool muted)
{
    lvgl_port_lock(0);
    s_muted = muted;
    lv_obj_set_style_arc_color(s_vol_arc,
        muted ? lv_color_hex(0xCC3333) : lv_color_hex(s_vol_color),
        LV_PART_INDICATOR);
    lvgl_port_unlock();
}

void ui_set_volume_color(uint32_t rgb)
{
    lvgl_port_lock(0);
    s_vol_color = rgb;
    if (!s_muted)   // muted shows red; apply the device color only when unmuted
        lv_obj_set_style_arc_color(s_vol_arc, lv_color_hex(rgb), LV_PART_INDICATOR);
    lvgl_port_unlock();
}

void ui_set_playing(bool playing)
{
    lvgl_port_lock(0);
    if (s_play_lbl)
        lv_label_set_text(s_play_lbl, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    lvgl_port_unlock();
}

void ui_set_dial_mode(bool speakers_mode)
{
    lvgl_port_lock(0);
    lv_label_set_text(s_hint_lbl, speakers_mode ? "rotate: pick   press: select"
                                                : "press: speakers   swipe: device");
    // Highlight the device name (blue) while picking a speaker.
    lv_obj_set_style_text_color(s_device_lbl,
        speakers_mode ? lv_color_hex(0x1E88E5) : lv_color_white(), 0);
    lvgl_port_unlock();
}

ui_transport_t ui_take_transport(void)
{
    ui_transport_t v = s_transport;
    s_transport = UI_TRANSPORT_NONE;
    return v;
}

void ui_set_wifi(bool connected)
{
    lvgl_port_lock(0);
    if (s_wifi_lbl)
        lv_obj_set_style_text_color(s_wifi_lbl,
            connected ? lv_color_hex(0x33CC66) : lv_color_hex(0xCC3333), 0);
    lvgl_port_unlock();
}

// --- Wi-Fi provisioning screen -----------------------------------------------

static lv_obj_t *s_prov_overlay;

void ui_prov_show(const char *qr_text, const char *ap_name)
{
    lvgl_port_lock(0);
    if (s_prov_overlay) lv_obj_delete(s_prov_overlay);

    s_prov_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_prov_overlay, 360, 360);
    lv_obj_center(s_prov_overlay);
    lv_obj_set_style_bg_color(s_prov_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_prov_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_prov_overlay, 0, 0);
    lv_obj_clear_flag(s_prov_overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(s_prov_overlay);
    lv_label_set_text(title, "Set up Wi-Fi");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 36);

    // QR code (scan to join the setup network).
    lv_obj_t *qr = lv_qrcode_create(s_prov_overlay);
    lv_qrcode_set_size(qr, 170);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_update(qr, qr_text, strlen(qr_text));
    lv_obj_set_style_border_color(qr, lv_color_white(), 0);
    lv_obj_set_style_border_width(qr, 6, 0);     // quiet zone
    lv_obj_center(qr);

    lv_obj_t *hint = lv_label_create(s_prov_overlay);
    lv_label_set_text_fmt(hint, "scan, or join\n%s\nthen open 192.168.4.1", ap_name);
    lv_obj_set_style_text_color(hint, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -28);

    lvgl_port_unlock();
}

void ui_prov_hide(void)
{
    lvgl_port_lock(0);
    if (s_prov_overlay) { lv_obj_delete(s_prov_overlay); s_prov_overlay = NULL; }
    lvgl_port_unlock();
}

void ui_set_transport_enabled(bool prev, bool playpause, bool next)
{
    lvgl_port_lock(0);
    set_btn_enabled(s_prev_btn, prev);
    set_btn_enabled(s_play_btn, playpause);
    set_btn_enabled(s_next_btn, next);
    lvgl_port_unlock();
}

int ui_take_swipe(void)
{
    int v = s_swipe;
    s_swipe = 0;
    return v;
}

bool ui_take_center_tap(void)
{
    bool v = s_center_tap;
    s_center_tap = false;
    return v;
}

// --- device-list overlay -----------------------------------------------------

#define UI_MAX_ROWS 16

static lv_obj_t     *s_overlay;
static lv_obj_t     *s_rows[UI_MAX_ROWS];
static int           s_row_count;
static int           s_sel;
static volatile int  s_tapped = -1;
static volatile bool s_cancel;

static void style_row(int i, bool sel)
{
    lv_obj_set_style_bg_color(s_rows[i],
        sel ? lv_color_hex(0x1E88E5) : lv_color_hex(0x222222), 0);
}

static void row_cb(lv_event_t *e)
{
    s_tapped = (int)(intptr_t)lv_event_get_user_data(e);
}

static void overlay_bg_cb(lv_event_t *e)
{
    // Fires only for clicks on the overlay background (children don't bubble).
    if (lv_event_get_target(e) == s_overlay) s_cancel = true;
}

void ui_devlist_show(const char *const labels[], int count, int sel)
{
    if (count > UI_MAX_ROWS) count = UI_MAX_ROWS;
    lvgl_port_lock(0);

    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_overlay, 360, 360);
    lv_obj_center(s_overlay);
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_overlay, 36, 0);
    lv_obj_set_style_pad_row(s_overlay, 6, 0);
    lv_obj_set_flex_flow(s_overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_overlay, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_overlay, overlay_bg_cb, LV_EVENT_CLICKED, NULL);

    s_row_count = count;
    s_tapped = -1;
    s_cancel = false;
    for (int i = 0; i < count; i++) {
        lv_obj_t *btn = lv_button_create(s_overlay);
        lv_obj_set_width(btn, lv_pct(100));
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_add_event_cb(btn, row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_label_set_text(lbl, labels[i]);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(lbl, lv_pct(100));
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        s_rows[i] = btn;
    }
    s_sel = (sel >= 0 && sel < count) ? sel : 0;
    for (int i = 0; i < count; i++) style_row(i, i == s_sel);

    lvgl_port_unlock();
}

void ui_devlist_set_sel(int idx)
{
    lvgl_port_lock(0);
    if (idx >= 0 && idx < s_row_count) {
        for (int i = 0; i < s_row_count; i++) style_row(i, i == idx);
        s_sel = idx;
        lv_obj_scroll_to_view(s_rows[idx], LV_ANIM_ON);
    }
    lvgl_port_unlock();
}

void ui_devlist_set_row(int idx, const char *text)
{
    lvgl_port_lock(0);
    if (idx >= 0 && idx < s_row_count && s_rows[idx]) {
        lv_obj_t *lbl = lv_obj_get_child(s_rows[idx], 0);
        if (lbl) lv_label_set_text(lbl, text);
    }
    lvgl_port_unlock();
}

int ui_devlist_take_tap(void)
{
    int v = s_tapped;
    s_tapped = -1;
    return v;
}

bool ui_devlist_take_cancel(void)
{
    bool v = s_cancel;
    s_cancel = false;
    return v;
}

void ui_devlist_hide(void)
{
    lvgl_port_lock(0);
    if (s_overlay) {
        lv_obj_delete(s_overlay);
        s_overlay = NULL;
    }
    s_row_count = 0;
    lvgl_port_unlock();
}
