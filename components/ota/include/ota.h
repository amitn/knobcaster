// Over-the-air firmware updates from GitHub Releases.
//
// The knob checks `github.com/<repo>/releases/latest`, compares the release tag
// (vMAJOR.MINOR.PATCH) to the running image's version, and — if the release is
// newer — pulls `firmware.bin` over HTTPS (CA-bundle verified) into the inactive
// OTA slot, then reboots into it. The new image boots in "pending verify"; a
// healthy boot calls ota_mark_valid(), otherwise the bootloader rolls back to
// the previous slot (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE).
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_IDLE,       // nothing in flight
    OTA_CHECKING,   // querying GitHub for the latest release
    OTA_UPDATING,   // downloading + writing the new image
    OTA_FAILED,     // last attempt failed (check/download/verify)
} ota_state_t;

// Spawn the OTA worker task (idle until ota_check_now()). Call once, after
// Wi-Fi and the event loop are up.
void ota_start(void);

// Ask the worker to check GitHub for a newer release (non-blocking; coalesced if
// a check/update is already running). Safe to call on a schedule.
void ota_check_now(void);

// Cancel the pending rollback for the *running* image once the boot is known
// healthy (e.g. Wi-Fi up + UI rendering). Idempotent and a no-op for images that
// weren't installed via OTA.
void ota_mark_valid(void);

// Current worker state and download progress (0..100, valid during OTA_UPDATING).
ota_state_t ota_state(void);
int         ota_progress_pct(void);

#ifdef __cplusplus
}
#endif
