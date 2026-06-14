// Cast session: drives one device's connection — handshake, heartbeat, and
// receiver/media status parsing into snapshots. Built on cast_connection.
//
// Single-threaded use for now (call open/poll/get from the same task).
#pragma once

#include "cast_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CAST_PLAYER_UNKNOWN = 0,
    CAST_PLAYER_IDLE,
    CAST_PLAYER_BUFFERING,
    CAST_PLAYER_PLAYING,
    CAST_PLAYER_PAUSED,
} cast_player_state_t;

typedef struct {
    cast_player_state_t state;
    char title[96];
    char subtitle[96];      // artist / secondary line
    char app_name[48];      // "Spotify", "YouTube Music", ...
    int  media_session_id;  // required for transport commands
    bool supports_pause;
    bool supports_next;
    bool supports_prev;
    bool supports_seek;
} cast_media_status_t;

typedef struct {
    float level;   // 0.0 .. 1.0
    bool  muted;
} cast_volume_status_t;

typedef struct cast_session cast_session_t;

// Open a session: TLS connect + CONNECT + GET_STATUS. NULL on failure.
cast_session_t *cast_session_open(const cast_device_t *dev);

// Close and free.
void cast_session_close(cast_session_t *s);

// Pump the session: send heartbeat if due, read/parse one inbound message.
// Call repeatedly with a modest timeout (e.g. 500 ms). Returns false if the
// connection died (caller should reopen).
bool cast_session_poll(cast_session_t *s, int timeout_ms);

// Latest snapshots.
void cast_session_get_media(cast_session_t *s, cast_media_status_t *out);
void cast_session_get_volume(cast_session_t *s, cast_volume_status_t *out);

// --- transport controls (M4) -------------------------------------------------
// All return false if the command could not be sent (e.g. no media session yet).
// Device-level volume (RECEIVER namespace):
bool cast_session_set_volume(cast_session_t *s, float level);   // 0.0 .. 1.0
bool cast_session_step_volume(cast_session_t *s, float delta);  // relative, clamped
bool cast_session_set_muted(cast_session_t *s, bool muted);
// Media transport (MEDIA namespace, needs an active media session):
bool cast_session_play(cast_session_t *s);
bool cast_session_pause(cast_session_t *s);
bool cast_session_toggle_pause(cast_session_t *s);  // play/pause by current state
bool cast_session_stop(cast_session_t *s);
bool cast_session_next(cast_session_t *s);
bool cast_session_prev(cast_session_t *s);

#ifdef __cplusplus
}
#endif
