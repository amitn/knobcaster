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

#ifdef __cplusplus
}
#endif
