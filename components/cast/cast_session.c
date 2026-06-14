#include "cast_session.h"
#include "cast_connection.h"

#include <string.h>

#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "cast.sess";

#define PING_INTERVAL_US (5 * 1000000)
#define TRANSPORT_ID_LEN 48

struct cast_session {
    cast_conn_t *conn;
    char         transport_id[TRANSPORT_ID_LEN];  // running app's virtual dest
    bool         transport_connected;
    int          req_id;
    int64_t      last_ping_us;
    cast_media_status_t  media;
    cast_volume_status_t volume;
    bool         vol_dirty;       // local volume change awaiting flush
    int64_t      last_vol_send_us;
    bool         is_group;        // device is a Cast group
    cast_member_t members[CAST_MAX_MEMBERS];
    int          member_count;
};

// --- outbound ----------------------------------------------------------------

static void send_get_status_receiver(cast_session_t *s)
{
    char p[64];
    snprintf(p, sizeof(p), "{\"type\":\"GET_STATUS\",\"requestId\":%d}", ++s->req_id);
    cast_conn_send(s->conn, CAST_SRC_DEFAULT, CAST_DST_RECEIVER, CAST_NS_RECEIVER, p);
}

static void send_get_status_multizone(cast_session_t *s)
{
    char p[64];
    snprintf(p, sizeof(p), "{\"type\":\"GET_STATUS\",\"requestId\":%d}", ++s->req_id);
    cast_conn_send(s->conn, CAST_SRC_DEFAULT, CAST_DST_RECEIVER, CAST_NS_MULTIZONE, p);
}

// Open a virtual connection to the running app and request its media status.
static void connect_transport(cast_session_t *s, const char *transport_id)
{
    strlcpy(s->transport_id, transport_id, sizeof(s->transport_id));
    cast_conn_send(s->conn, CAST_SRC_DEFAULT, s->transport_id,
                   CAST_NS_CONNECTION, "{\"type\":\"CONNECT\"}");
    char p[64];
    snprintf(p, sizeof(p), "{\"type\":\"GET_STATUS\",\"requestId\":%d}", ++s->req_id);
    cast_conn_send(s->conn, CAST_SRC_DEFAULT, s->transport_id, CAST_NS_MEDIA, p);
    s->transport_connected = true;
    ESP_LOGI(TAG, "connected to media transport %s", s->transport_id);
}

// --- inbound: apply parsed status to the session -----------------------------

static void apply_receiver_status(cast_session_t *s, const cast_receiver_status_t *rs)
{
    // Reconcile volume: accept the device's level only when no local change is
    // in flight, so external changes show but we don't snap back mid-turn.
    if (rs->has_level && !s->vol_dirty) s->volume.level = rs->level;
    if (rs->has_muted) s->volume.muted = rs->muted;

    if (rs->has_app) {
        strlcpy(s->media.app_name, rs->app_name, sizeof(s->media.app_name));
        if (rs->transport_id[0] && strcmp(rs->transport_id, s->transport_id) != 0) {
            connect_transport(s, rs->transport_id);
        }
    } else {
        // Nothing running — device is idle.
        s->media.state = CAST_PLAYER_IDLE;
        s->media.title[0] = s->media.subtitle[0] = s->media.app_name[0] = '\0';
        s->transport_id[0] = '\0';
        s->transport_connected = false;
    }
}

static void apply_device_update(cast_session_t *s, const cast_member_t *d)
{
    for (int i = 0; i < s->member_count; i++) {
        if (strcmp(s->members[i].id, d->id) == 0) { s->members[i] = *d; return; }
    }
    if (s->member_count < CAST_MAX_MEMBERS) s->members[s->member_count++] = *d;
}

static void dispatch(cast_session_t *s, const cast_msg_t *msg)
{
    const char *json = (const char *)msg->payload;
    size_t len = msg->payload_len;

    if (!strcmp(msg->ns, CAST_NS_HEARTBEAT)) {
        char type[16];
        if (cast_parse_type(json, len, type, sizeof(type)) && !strcmp(type, "PING")) {
            cast_conn_send(s->conn, CAST_SRC_DEFAULT, CAST_DST_RECEIVER,
                           CAST_NS_HEARTBEAT, "{\"type\":\"PONG\"}");
        }
    } else if (!strcmp(msg->ns, CAST_NS_RECEIVER)) {
        cast_receiver_status_t rs;
        if (cast_parse_receiver_status(json, len, &rs)) apply_receiver_status(s, &rs);
    } else if (!strcmp(msg->ns, CAST_NS_MEDIA)) {
        cast_parse_media_status(json, len, &s->media);
    } else if (!strcmp(msg->ns, CAST_NS_MULTIZONE)) {
        cast_member_t tmp[CAST_MAX_MEMBERS];
        int n = cast_parse_multizone_status(json, len, tmp, CAST_MAX_MEMBERS);
        if (n >= 0) {
            for (int i = 0; i < n; i++) s->members[i] = tmp[i];
            s->member_count = n;
        } else {
            cast_member_t d;
            if (cast_parse_device_updated(json, len, &d)) apply_device_update(s, &d);
        }
    }
}

// --- public API --------------------------------------------------------------

cast_session_t *cast_session_open(const cast_device_t *dev)
{
    cast_session_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;

    s->conn = cast_conn_open(dev->ip, dev->port ? dev->port : CAST_PORT);
    if (!s->conn) { free(s); return NULL; }

    s->is_group = dev->is_group;
    cast_conn_send(s->conn, CAST_SRC_DEFAULT, CAST_DST_RECEIVER,
                   CAST_NS_CONNECTION, "{\"type\":\"CONNECT\"}");
    send_get_status_receiver(s);
    if (s->is_group) send_get_status_multizone(s);   // enumerate member speakers
    s->last_ping_us = esp_timer_get_time();
    return s;
}

void cast_session_close(cast_session_t *s)
{
    if (!s) return;
    cast_conn_close(s->conn);
    free(s);
}

bool cast_session_poll(cast_session_t *s, int timeout_ms)
{
    int64_t now = esp_timer_get_time();
    if (now - s->last_ping_us > PING_INTERVAL_US) {
        if (!cast_conn_send(s->conn, CAST_SRC_DEFAULT, CAST_DST_RECEIVER,
                            CAST_NS_HEARTBEAT, "{\"type\":\"PING\"}")) {
            return false;
        }
        s->last_ping_us = now;
    }

    // Flush a pending volume change at most ~16 Hz so a fast knob turn sends a
    // few SET_VOLUMEs (with the latest target) instead of one per detent.
    if (s->vol_dirty && (now - s->last_vol_send_us) >= 60000) {
        s->vol_dirty = false;              // cleared before send so an echo reconciles
        s->last_vol_send_us = now;
        cast_session_set_volume(s, s->volume.level);
    }

    cast_msg_t msg;
    cast_rx_t r = cast_conn_recv(s->conn, &msg, timeout_ms);
    if (r == CAST_RX_CLOSED) return false;
    if (r == CAST_RX_MSG) dispatch(s, &msg);
    return true;  // timeout is normal
}

void cast_session_get_media(cast_session_t *s, cast_media_status_t *out)
{
    *out = s->media;
}

void cast_session_get_volume(cast_session_t *s, cast_volume_status_t *out)
{
    *out = s->volume;
}

// --- transport controls ------------------------------------------------------

bool cast_session_set_volume(cast_session_t *s, float level)
{
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    char p[96];
    snprintf(p, sizeof(p),
             "{\"type\":\"SET_VOLUME\",\"volume\":{\"level\":%.3f},\"requestId\":%d}",
             level, ++s->req_id);
    return cast_conn_send(s->conn, CAST_SRC_DEFAULT, CAST_DST_RECEIVER,
                          CAST_NS_RECEIVER, p);
}

bool cast_session_step_volume(cast_session_t *s, float delta)
{
    // Optimistically advance the local snapshot so repeated steps accumulate
    // instantly; the actual SET_VOLUME is rate-limited in cast_session_poll().
    s->volume.level += delta;
    if (s->volume.level < 0.0f) s->volume.level = 0.0f;
    if (s->volume.level > 1.0f) s->volume.level = 1.0f;
    s->vol_dirty = true;
    return true;
}

bool cast_session_set_muted(cast_session_t *s, bool muted)
{
    char p[96];
    snprintf(p, sizeof(p),
             "{\"type\":\"SET_VOLUME\",\"volume\":{\"muted\":%s},\"requestId\":%d}",
             muted ? "true" : "false", ++s->req_id);
    return cast_conn_send(s->conn, CAST_SRC_DEFAULT, CAST_DST_RECEIVER,
                          CAST_NS_RECEIVER, p);
}

// Send a simple media command that only needs {type, mediaSessionId}.
static bool media_cmd(cast_session_t *s, const char *type)
{
    if (!s->transport_connected || s->media.media_session_id == 0) return false;
    char p[128];
    snprintf(p, sizeof(p),
             "{\"type\":\"%s\",\"mediaSessionId\":%d,\"requestId\":%d}",
             type, s->media.media_session_id, ++s->req_id);
    return cast_conn_send(s->conn, CAST_SRC_DEFAULT, s->transport_id,
                          CAST_NS_MEDIA, p);
}

// Next/Prev are QUEUE_UPDATE with a relative jump (portable across receivers).
static bool queue_jump(cast_session_t *s, int jump)
{
    if (!s->transport_connected || s->media.media_session_id == 0) return false;
    char p[128];
    snprintf(p, sizeof(p),
             "{\"type\":\"QUEUE_UPDATE\",\"mediaSessionId\":%d,\"jump\":%d,\"requestId\":%d}",
             s->media.media_session_id, jump, ++s->req_id);
    return cast_conn_send(s->conn, CAST_SRC_DEFAULT, s->transport_id,
                          CAST_NS_MEDIA, p);
}

bool cast_session_play(cast_session_t *s)  { return media_cmd(s, "PLAY"); }
bool cast_session_pause(cast_session_t *s) { return media_cmd(s, "PAUSE"); }
bool cast_session_stop(cast_session_t *s)  { return media_cmd(s, "STOP"); }
bool cast_session_next(cast_session_t *s)  { return queue_jump(s, 1); }
bool cast_session_prev(cast_session_t *s)  { return queue_jump(s, -1); }

bool cast_session_toggle_pause(cast_session_t *s)
{
    return (s->media.state == CAST_PLAYER_PLAYING) ? cast_session_pause(s)
                                                   : cast_session_play(s);
}

// --- groups / multizone ------------------------------------------------------

bool cast_session_is_group(cast_session_t *s)    { return s->is_group; }
int  cast_session_member_count(cast_session_t *s) { return s->member_count; }

void cast_session_get_member(cast_session_t *s, int i, cast_member_t *out)
{
    if (i >= 0 && i < s->member_count) *out = s->members[i];
}

bool cast_session_set_member_volume(cast_session_t *s, int i, float level)
{
    if (i < 0 || i >= s->member_count) return false;
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    char p[160];
    snprintf(p, sizeof(p),
             "{\"type\":\"SET_DEVICE_VOLUME\",\"deviceId\":\"%s\","
             "\"volume\":{\"level\":%.3f},\"requestId\":%d}",
             s->members[i].id, level, ++s->req_id);
    return cast_conn_send(s->conn, CAST_SRC_DEFAULT, CAST_DST_RECEIVER,
                          CAST_NS_MULTIZONE, p);
}

bool cast_session_step_member_volume(cast_session_t *s, int i, float delta)
{
    if (i < 0 || i >= s->member_count) return false;
    float level = s->members[i].level + delta;
    s->members[i].level = level < 0 ? 0 : (level > 1 ? 1 : level);  // optimistic
    return cast_session_set_member_volume(s, i, s->members[i].level);
}
