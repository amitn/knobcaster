// LVGL UI for the Cast Knob. For now a static "ready" screen; later it renders
// the now-playing / device-list model (see docs/04-ui-ux.md).
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Build the initial UI on the active screen. Call after display_init().
// Takes the lvgl_port lock internally.
void ui_init(void);

// Update the now-playing text (thread-safe; takes the lvgl_port lock).
void ui_set_now_playing(const char *device, const char *title,
                        const char *subtitle, int volume_pct);

#ifdef __cplusplus
}
#endif
