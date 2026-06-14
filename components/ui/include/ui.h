// LVGL UI for the Cast Knob. For now a static "ready" screen; later it renders
// the now-playing / device-list model (see docs/04-ui-ux.md).
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Build the initial UI on the active screen. Call after display_init().
// Takes the lvgl_port lock internally.
void ui_init(void);

// Update the now-playing text (thread-safe; takes the lvgl_port lock).
void ui_set_now_playing(const char *device, const char *title,
                        const char *subtitle, int volume_pct);

// Reflect mute state on the volume arc (red when muted). Thread-safe.
void ui_set_muted(bool muted);

// Reflect play state on the center transport button (play vs pause icon).
void ui_set_playing(bool playing);

// On-screen transport buttons.
typedef enum {
    UI_TRANSPORT_NONE = 0,
    UI_TRANSPORT_PREV,
    UI_TRANSPORT_PLAYPAUSE,
    UI_TRANSPORT_NEXT,
} ui_transport_t;

// Consume a pending transport-button press (UI_TRANSPORT_NONE if none).
ui_transport_t ui_take_transport(void);

// Enable/disable (dim) transport buttons per the app's supported commands.
void ui_set_transport_enabled(bool prev, bool playpause, bool next);

// Consume a pending horizontal swipe: +1 = swipe left (next device),
// -1 = swipe right (previous device), 0 = none.
int ui_take_swipe(void);

// Consume a pending center tap on the now-playing screen (opens the device list).
bool ui_take_center_tap(void);

// --- device-list overlay -----------------------------------------------------
// Show a modal list of devices (labels[0..count-1]) with `sel` highlighted.
void ui_devlist_show(const char *const labels[], int count, int sel);
// Move the highlight to row idx (and scroll it into view).
void ui_devlist_set_sel(int idx);
// Consume a tapped row index, or -1 if none.
int  ui_devlist_take_tap(void);
// Consume a background tap (dismiss request). Returns true once.
bool ui_devlist_take_cancel(void);
// Tear down the overlay.
void ui_devlist_hide(void);

#ifdef __cplusplus
}
#endif
