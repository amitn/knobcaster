#include "ui.h"

#include <stdio.h>

#include "lvgl.h"
#include "esp_lvgl_port.h"

// Widgets we update at runtime.
static lv_obj_t *s_device_lbl;
static lv_obj_t *s_title_lbl;
static lv_obj_t *s_subtitle_lbl;
static lv_obj_t *s_vol_arc;
static lv_obj_t *s_hint_lbl;

// Pending swipe direction, consumed by ui_take_swipe().
static volatile int s_swipe;

static void swipe_cb(lv_event_t *e)
{
    (void)e;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if (dir == LV_DIR_LEFT)       s_swipe = +1;  // next device
    else if (dir == LV_DIR_RIGHT) s_swipe = -1;  // previous device
}

void ui_init(void)
{
    lvgl_port_lock(0);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
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
    lv_obj_set_style_arc_color(s_vol_arc, lv_color_hex(0x1E88E5), LV_PART_INDICATOR);

    // Device name (top).
    s_device_lbl = lv_label_create(scr);
    lv_obj_set_style_text_color(s_device_lbl, lv_color_white(), 0);
    lv_label_set_text(s_device_lbl, "Cast Knob");
    lv_obj_align(s_device_lbl, LV_ALIGN_TOP_MID, 0, 70);

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

    // Hint (bottom).
    s_hint_lbl = lv_label_create(scr);
    lv_obj_set_style_text_color(s_hint_lbl, lv_color_hex(0x666666), 0);
    lv_label_set_text(s_hint_lbl, "dial = volume  swipe = device");
    lv_obj_align(s_hint_lbl, LV_ALIGN_BOTTOM_MID, 0, -70);

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
    lv_obj_set_style_arc_color(s_vol_arc,
        muted ? lv_color_hex(0xCC3333) : lv_color_hex(0x1E88E5),
        LV_PART_INDICATOR);
    lvgl_port_unlock();
}

int ui_take_swipe(void)
{
    int v = s_swipe;
    s_swipe = 0;
    return v;
}
