// SH8601 QSPI display + LVGL bring-up.
#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Bring up the SH8601 panel, backlight, and LVGL (via esp_lvgl_port). Returns
// the LVGL display handle, or NULL on failure. esp_lvgl_port runs LVGL in its
// own task — guard any LVGL calls from other tasks with lvgl_port_lock()/unlock().
lv_display_t *display_init(void);

// Turn the backlight on/off (GPIO47, active-high).
void display_backlight(bool on);

// Screen power for the idle-sleep feature. display_sleep() blanks the panel +
// backlight (LVGL/Wi-Fi/Cast keep running, so any input wakes it and the screen
// content stays current). display_wake() re-powers them. Both are idempotent and
// take the LVGL port lock internally; call from a task that doesn't hold it.
void display_sleep(void);
void display_wake(void);
bool display_is_asleep(void);

// Bring up the CST816 capacitive touch (I2C) and register it with LVGL as a
// pointer input device on `disp`. Call after display_init(). Returns false on
// failure (the UI still works without touch).
bool touch_init(lv_display_t *disp);

#ifdef __cplusplus
}
#endif
