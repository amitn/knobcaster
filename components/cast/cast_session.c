#include "cast_session.h"
#include "cast_connection.h"

#include <string.h>

#include "cJSON.h"
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

// --- small JSON helpers ------------------------------------------------------

static void copy_str_field(cJSON *obj, const char *key, char *dst, size_t cap)
{
    cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(it) && it->valuestring) {
        strlcpy(dst, it->valuestring, cap);
    }
}

static const char *str_field(cJSON *obj, const char *key)
{
    cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    return (cJSON_IsString(it) && it->valuestring) ? it->valuestring : NULL;
}

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

// --- inbound parsing ---------------------------------------------------------

static cast_player_state_t parse_player_state(const char *s)
{
    if (!s) return CAST_PLAYER_UNKNOWN;
    if (!strcmp(s, "PLAYING"))   return CAST_PLAYER_PLAYING;
    if (!strcmp(s, "PAUSED"))    return CAST_PLAYER_PAUSED;
    if (!strcmp(s, "BUFFERING")) return CAST_PLAYER_BUFFERING;
    if (!strcmp(s, "IDLE"))      return CAST_PLAYER_IDLE;
    return CAST_PLAYER_UNKNOWN;
}

static void handle_receiver_status(cast_session_t *s, cJSON *root)
{
    cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (!status) return;

    cJSON *vol = cJSON_GetObjectItemCaseSensitive(status, "volume");
    if (vol) {
        cJSON *level = cJSON_GetObjectItemCaseSensitive(vol, "level");
        cJSON *muted = cJSON_GetObjectItemCaseSensitive(vol, "muted");
        // Reconcile: accept the device's level only when we have no local change
        // in flight, so an external change is reflected but we don't snap back
        // mid-turn.
        if (cJSON_IsNumber(level) && !s->vol_dirty)
            s->volume.level = (float)level->valuedouble;
        if (cJSON_IsBool(muted)) s->volume.muted = cJSON_IsTrue(muted);
    }

    cJSON *apps = cJSON_GetObjectItemCaseSensitive(status, "applications");
    if (cJSON_IsArray(apps) && cJSON_GetArraySize(apps) > 0) {
        cJSON *app0 = cJSON_GetArrayItem(apps, 0);
        copy_str_field(app0, "displayName", s->media.app_name, sizeof(s->media.app_name));
        const char *tid = str_field(app0, "transportId");
        if (tid && strcmp(tid, s->transport_id) != 0) {
            connect_transport(s, tid);
        }
    } else {
        // Nothing running — device is idle.
        s->media.state = CAST_PLAYER_IDLE;
        s->media.title[0] = s->media.subtitle[0] = s->media.app_name[0] = '\0';
        s->transport_id[0] = '\0';
        s->transport_connected = false;
    }
}

static void handle_media_status(cast_session_t *s, cJSON *root)
{
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (!cJSON_IsArray(arr) || cJSON_GetArraySize(arr) == 0) return;
    cJSON *st = cJSON_GetArrayItem(arr, 0);

    s->media.state = parse_player_state(str_field(st, "playerState"));

    cJSON *msid = cJSON_GetObjectItemCaseSensitive(st, "mediaSessionId");
    if (cJSON_IsNumber(msid)) s->media.media_session_id = msid->valueint;

    cJSON *cmds = cJSON_GetObjectItemCaseSensitive(st, "supportedMediaCommands");
    if (cJSON_IsNumber(cmds)) {
        int b = cmds->valueint;
        s->media.supports_pause = b & 0x01;        // PAUSE
        s->media.supports_seek  = b & 0x02;        // SEEK
        s->media.supports_next  = b & (0x40 | 0x10); // QUEUE_NEXT | SKIP_FWD
        s->media.supports_prev  = b & (0x80 | 0x20); // QUEUE_PREV | SKIP_BACK
    }

    cJSON *media = cJSON_GetObjectItemCaseSensitive(st, "media");
    cJSON *meta  = media ? cJSON_GetObjectItemCaseSensitive(media, "metadata") : NULL;
    if (meta) {
        copy_str_field(meta, "title", s->media.title, sizeof(s->media.title));
        // Prefer artist; fall back to subtitle.
        const char *artist = str_field(meta, "artist");
        copy_str_field(meta, artist ? "artist" : "subtitle",
                       s->media.subtitle, sizeof(s->media.subtitle));
    }
}

static void parse_member(cJSON *dev, cast_member_t *m)
{
    copy_str_field(dev, "deviceId", m->id, sizeof(m->id));
    copy_str_field(dev, "name", m->name, sizeof(m->name));
    cJSON *vol = cJSON_GetObjectItemCaseSensitive(dev, "volume");
    if (vol) {
        cJSON *lvl = cJSON_GetObjectItemCaseSensitive(vol, "level");
        cJSON *mut = cJSON_GetObjectItemCaseSensitive(vol, "muted");
        if (cJSON_IsNumber(lvl)) m->level = (float)lvl->valuedouble;
        if (cJSON_IsBool(mut))   m->muted = cJSON_IsTrue(mut);
    }
}

static void handle_multizone_status(cast_session_t *s, cJSON *root)
{
    cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
    cJSON *devs = status ? cJSON_GetObjectItemCaseSensitive(status, "devices") : NULL;
    if (!cJSON_IsArray(devs)) return;
    int n = cJSON_GetArraySize(devs);
    if (n > CAST_MAX_MEMBERS) n = CAST_MAX_MEMBERS;
    for (int i = 0; i < n; i++) {
        memset(&s->members[i], 0, sizeof(s->members[i]));
        parse_member(cJSON_GetArrayItem(devs, i), &s->members[i]);
    }
    s->member_count = n;
}

static void handle_device_updated(cast_session_t *s, cJSON *root)
{
    cJSON *dev = cJSON_GetObjectItemCaseSensitive(root, "device");
    if (!dev) return;
    char id[64] = {0};
    copy_str_field(dev, "deviceId", id, sizeof(id));
    for (int i = 0; i < s->member_count; i++) {
        if (strcmp(s->members[i].id, id) == 0) { parse_member(dev, &s->members[i]); return; }
    }
    if (s->member_count < CAST_MAX_MEMBERS) parse_member(dev, &s->members[s->member_count++]);
}

static void dispatch(cast_session_t *s, const cast_msg_t *msg)
{
    cJSON *root = cJSON_ParseWithLength((const char *)msg->payload, msg->payload_len);
    if (!root) return;
    const char *type = str_field(root, "type");

    if (!strcmp(msg->ns, CAST_NS_HEARTBEAT)) {
        if (type && !strcmp(type, "PING")) {
            cast_conn_send(s->conn, CAST_SRC_DEFAULT, CAST_DST_RECEIVER,
                           CAST_NS_HEARTBEAT, "{\"type\":\"PONG\"}");
        }
    } else if (!strcmp(msg->ns, CAST_NS_RECEIVER)) {
        if (type && !strcmp(type, "RECEIVER_STATUS")) handle_receiver_status(s, root);
    } else if (!strcmp(msg->ns, CAST_NS_MEDIA)) {
        if (type && !strcmp(type, "MEDIA_STATUS")) handle_media_status(s, root);
    } else if (!strcmp(msg->ns, CAST_NS_MULTIZONE)) {
        if (type && !strcmp(type, "MULTIZONE_STATUS")) handle_multizone_status(s, root);
        else if (type && !strcmp(type, "DEVICE_UPDATED")) handle_device_updated(s, root);
    }
    cJSON_Delete(root);
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
