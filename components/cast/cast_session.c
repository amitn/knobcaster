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
        if (cJSON_IsNumber(level)) s->volume.level = (float)level->valuedouble;
        if (cJSON_IsBool(muted))   s->volume.muted = cJSON_IsTrue(muted);
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

    cast_conn_send(s->conn, CAST_SRC_DEFAULT, CAST_DST_RECEIVER,
                   CAST_NS_CONNECTION, "{\"type\":\"CONNECT\"}");
    send_get_status_receiver(s);
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
    // without waiting for the device's RECEIVER_STATUS echo.
    s->volume.level += delta;
    if (s->volume.level < 0.0f) s->volume.level = 0.0f;
    if (s->volume.level > 1.0f) s->volume.level = 1.0f;
    return cast_session_set_volume(s, s->volume.level);
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
