#include "cast_status.h"

#include <string.h>

#include "cJSON.h"

// --- small JSON helpers ------------------------------------------------------

static void copy_str_field(cJSON *obj, const char *key, char *dst, size_t cap)
{
    cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(it) && it->valuestring) {
        // strlcpy isn't standard on all hosts; bound-copy by hand.
        size_t n = strlen(it->valuestring);
        if (n >= cap) n = cap - 1;
        memcpy(dst, it->valuestring, n);
        dst[n] = '\0';
    }
}

static const char *str_field(cJSON *obj, const char *key)
{
    cJSON *it = cJSON_GetObjectItemCaseSensitive(obj, key);
    return (cJSON_IsString(it) && it->valuestring) ? it->valuestring : NULL;
}

// Parse + verify the top-level "type". Returns the root (caller frees) or NULL.
static cJSON *parse_typed(const char *json, size_t len, const char *want_type)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return NULL;
    const char *t = str_field(root, "type");
    if (!t || strcmp(t, want_type) != 0) { cJSON_Delete(root); return NULL; }
    return root;
}

static void parse_member_obj(cJSON *dev, cast_member_t *m)
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

// --- public parsers ----------------------------------------------------------

bool cast_parse_type(const char *json, size_t len, char *out, size_t cap)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) return false;
    const char *t = str_field(root, "type");
    bool ok = false;
    if (t) { copy_str_field(root, "type", out, cap); ok = true; }
    cJSON_Delete(root);
    return ok;
}

cast_player_state_t cast_parse_player_state(const char *s)
{
    if (!s) return CAST_PLAYER_UNKNOWN;
    if (!strcmp(s, "PLAYING"))   return CAST_PLAYER_PLAYING;
    if (!strcmp(s, "PAUSED"))    return CAST_PLAYER_PAUSED;
    if (!strcmp(s, "BUFFERING")) return CAST_PLAYER_BUFFERING;
    if (!strcmp(s, "IDLE"))      return CAST_PLAYER_IDLE;
    return CAST_PLAYER_UNKNOWN;
}

bool cast_parse_receiver_status(const char *json, size_t len, cast_receiver_status_t *out)
{
    memset(out, 0, sizeof(*out));
    cJSON *root = parse_typed(json, len, "RECEIVER_STATUS");
    if (!root) return false;

    cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (status) {
        cJSON *vol = cJSON_GetObjectItemCaseSensitive(status, "volume");
        if (vol) {
            cJSON *level = cJSON_GetObjectItemCaseSensitive(vol, "level");
            cJSON *muted = cJSON_GetObjectItemCaseSensitive(vol, "muted");
            if (cJSON_IsNumber(level)) { out->has_level = true; out->level = (float)level->valuedouble; }
            if (cJSON_IsBool(muted))   { out->has_muted = true; out->muted = cJSON_IsTrue(muted); }
        }
        cJSON *apps = cJSON_GetObjectItemCaseSensitive(status, "applications");
        if (cJSON_IsArray(apps) && cJSON_GetArraySize(apps) > 0) {
            cJSON *app0 = cJSON_GetArrayItem(apps, 0);
            out->has_app = true;
            copy_str_field(app0, "displayName", out->app_name, sizeof(out->app_name));
            copy_str_field(app0, "transportId", out->transport_id, sizeof(out->transport_id));
        }
    }
    cJSON_Delete(root);
    return true;
}

bool cast_parse_media_status(const char *json, size_t len, cast_media_status_t *inout)
{
    cJSON *root = parse_typed(json, len, "MEDIA_STATUS");
    if (!root) return false;

    cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (!cJSON_IsArray(arr) || cJSON_GetArraySize(arr) == 0) { cJSON_Delete(root); return false; }
    cJSON *st = cJSON_GetArrayItem(arr, 0);

    inout->state = cast_parse_player_state(str_field(st, "playerState"));

    cJSON *msid = cJSON_GetObjectItemCaseSensitive(st, "mediaSessionId");
    if (cJSON_IsNumber(msid)) inout->media_session_id = msid->valueint;

    cJSON *cmds = cJSON_GetObjectItemCaseSensitive(st, "supportedMediaCommands");
    if (cJSON_IsNumber(cmds)) {
        int b = cmds->valueint;
        inout->supports_pause = b & 0x01;          // PAUSE
        inout->supports_seek  = b & 0x02;          // SEEK
        inout->supports_next  = b & (0x40 | 0x10); // QUEUE_NEXT | SKIP_FWD
        inout->supports_prev  = b & (0x80 | 0x20); // QUEUE_PREV | SKIP_BACK
    }

    cJSON *media = cJSON_GetObjectItemCaseSensitive(st, "media");
    cJSON *meta  = media ? cJSON_GetObjectItemCaseSensitive(media, "metadata") : NULL;
    inout->has_media = (meta != NULL);
    if (meta) {
        copy_str_field(meta, "title", inout->title, sizeof(inout->title));
        const char *artist = str_field(meta, "artist");   // prefer artist, else subtitle
        copy_str_field(meta, artist ? "artist" : "subtitle",
                       inout->subtitle, sizeof(inout->subtitle));

        // Album art: metadata.images[0].url (fall back to media.images[0].url).
        cJSON *images = cJSON_GetObjectItemCaseSensitive(meta, "images");
        if (!cJSON_IsArray(images) || cJSON_GetArraySize(images) == 0)
            images = cJSON_GetObjectItemCaseSensitive(media, "images");
        inout->art_url[0] = '\0';
        if (cJSON_IsArray(images) && cJSON_GetArraySize(images) > 0) {
            cJSON *img0 = cJSON_GetArrayItem(images, 0);
            copy_str_field(img0, "url", inout->art_url, sizeof(inout->art_url));
        }
    }
    cJSON_Delete(root);
    return true;
}

int cast_parse_multizone_status(const char *json, size_t len, cast_member_t *out, int max)
{
    cJSON *root = parse_typed(json, len, "MULTIZONE_STATUS");
    if (!root) return -1;

    cJSON *status = cJSON_GetObjectItemCaseSensitive(root, "status");
    cJSON *devs = status ? cJSON_GetObjectItemCaseSensitive(status, "devices") : NULL;
    int n = 0;
    if (cJSON_IsArray(devs)) {
        int count = cJSON_GetArraySize(devs);
        if (count > max) count = max;
        for (n = 0; n < count; n++) {
            memset(&out[n], 0, sizeof(out[n]));
            parse_member_obj(cJSON_GetArrayItem(devs, n), &out[n]);
        }
    }
    cJSON_Delete(root);
    return n;
}

bool cast_parse_device_updated(const char *json, size_t len, cast_member_t *out)
{
    cJSON *root = parse_typed(json, len, "DEVICE_UPDATED");
    if (!root) return false;
    cJSON *dev = cJSON_GetObjectItemCaseSensitive(root, "device");
    if (!dev) { cJSON_Delete(root); return false; }
    memset(out, 0, sizeof(*out));
    parse_member_obj(dev, out);
    cJSON_Delete(root);
    return true;
}
