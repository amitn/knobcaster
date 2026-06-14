// Pure CASTV2 status-payload parsers (cJSON only — NO ESP-IDF deps, so this
// compiles and unit-tests on the host; see docs/07-testing.md).
//
// These functions take a raw JSON payload and fill plain structs. They contain
// no I/O, timers, or session state — the session layer (cast_session.c) calls
// them and applies the results (reconcile, transport switch, etc.).
#pragma once

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- status snapshot types (shared with cast_session) ------------------------

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
    bool has_media;         // this status carried a media/metadata block
} cast_media_status_t;

typedef struct {
    float level;   // 0.0 .. 1.0
    bool  muted;
} cast_volume_status_t;

#define CAST_MAX_MEMBERS 8

// A member speaker of a Cast group (from the multizone namespace).
typedef struct {
    char  id[64];
    char  name[64];
    float level;
    bool  muted;
} cast_member_t;

// Parsed RECEIVER_STATUS (the session applies reconcile + transport switching).
typedef struct {
    bool  has_level;  float level;
    bool  has_muted;  bool  muted;
    bool  has_app;                     // an application is running (not idle)
    char  app_name[48];                // applications[0].displayName
    char  transport_id[48];            // applications[0].transportId
} cast_receiver_status_t;

// --- pure parsers ------------------------------------------------------------

// Extract the top-level "type" string (e.g. "PING", "RECEIVER_STATUS").
// Returns false if absent/not a string.
bool cast_parse_type(const char *json, size_t len, char *out, size_t cap);

// Map a playerState string to the enum.
cast_player_state_t cast_parse_player_state(const char *s);

// Parse a RECEIVER_STATUS payload into *out. Returns false if the JSON is invalid
// or its type isn't RECEIVER_STATUS.
bool cast_parse_receiver_status(const char *json, size_t len, cast_receiver_status_t *out);

// Parse a MEDIA_STATUS payload, updating fields present in *inout (others kept).
// Returns false if invalid / not MEDIA_STATUS / empty status array.
bool cast_parse_media_status(const char *json, size_t len, cast_media_status_t *inout);

// Parse a MULTIZONE_STATUS payload into out[0..max-1]. Returns the member count,
// or -1 if the JSON is invalid / not a MULTIZONE_STATUS.
int cast_parse_multizone_status(const char *json, size_t len, cast_member_t *out, int max);

// Parse a DEVICE_UPDATED payload's device into *out. Returns false otherwise.
bool cast_parse_device_updated(const char *json, size_t len, cast_member_t *out);

#ifdef __cplusplus
}
#endif
