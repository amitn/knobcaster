// Host-native unit tests for the pure CASTV2 status parsers (cast_status.c).
// cJSON only, no ESP deps — see docs/07-testing.md. Fixtures are hand-authored
// to mirror real receiver/media/multizone payload shapes.
#include <string.h>
#include <unity.h>

#include "cJSON.h"          // pull the vendored cJSON (lib/cjson) into the build
#include "cast_status.h"
#include "cast_status.c"    // compile the implementation into this test TU

void setUp(void) {}
void tearDown(void) {}

#define PARSE(fn, json, ...) fn(json, strlen(json), __VA_ARGS__)

// --- cast_parse_type ---------------------------------------------------------

void test_type_extracts(void)
{
    char t[24];
    TEST_ASSERT_TRUE(PARSE(cast_parse_type, "{\"type\":\"PING\"}", t, sizeof(t)));
    TEST_ASSERT_EQUAL_STRING("PING", t);
}

void test_type_missing_is_false(void)
{
    char t[24];
    TEST_ASSERT_FALSE(PARSE(cast_parse_type, "{\"foo\":1}", t, sizeof(t)));
}

void test_type_garbage_is_false(void)
{
    char t[24];
    TEST_ASSERT_FALSE(PARSE(cast_parse_type, "not json", t, sizeof(t)));
}

// --- cast_parse_player_state -------------------------------------------------

void test_player_state_mapping(void)
{
    TEST_ASSERT_EQUAL_INT(CAST_PLAYER_PLAYING,   cast_parse_player_state("PLAYING"));
    TEST_ASSERT_EQUAL_INT(CAST_PLAYER_PAUSED,    cast_parse_player_state("PAUSED"));
    TEST_ASSERT_EQUAL_INT(CAST_PLAYER_BUFFERING, cast_parse_player_state("BUFFERING"));
    TEST_ASSERT_EQUAL_INT(CAST_PLAYER_IDLE,      cast_parse_player_state("IDLE"));
    TEST_ASSERT_EQUAL_INT(CAST_PLAYER_UNKNOWN,   cast_parse_player_state("WAT"));
    TEST_ASSERT_EQUAL_INT(CAST_PLAYER_UNKNOWN,   cast_parse_player_state(NULL));
}

// --- cast_parse_receiver_status ----------------------------------------------

void test_receiver_running_app(void)
{
    const char *json =
        "{\"type\":\"RECEIVER_STATUS\",\"status\":{"
        "\"volume\":{\"level\":0.42,\"muted\":false},"
        "\"applications\":[{\"displayName\":\"Spotify\",\"transportId\":\"web-5\"}]}}";
    cast_receiver_status_t r;
    TEST_ASSERT_TRUE(PARSE(cast_parse_receiver_status, json, &r));
    TEST_ASSERT_TRUE(r.has_level);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.42f, r.level);
    TEST_ASSERT_TRUE(r.has_muted);
    TEST_ASSERT_FALSE(r.muted);
    TEST_ASSERT_TRUE(r.has_app);
    TEST_ASSERT_EQUAL_STRING("Spotify", r.app_name);
    TEST_ASSERT_EQUAL_STRING("web-5", r.transport_id);
}

void test_receiver_idle_no_app(void)
{
    const char *json =
        "{\"type\":\"RECEIVER_STATUS\",\"status\":{"
        "\"volume\":{\"level\":0.3,\"muted\":true}}}";
    cast_receiver_status_t r;
    TEST_ASSERT_TRUE(PARSE(cast_parse_receiver_status, json, &r));
    TEST_ASSERT_TRUE(r.has_muted);
    TEST_ASSERT_TRUE(r.muted);
    TEST_ASSERT_FALSE(r.has_app);
    TEST_ASSERT_EQUAL_STRING("", r.app_name);
}

void test_receiver_wrong_type_is_false(void)
{
    cast_receiver_status_t r;
    TEST_ASSERT_FALSE(PARSE(cast_parse_receiver_status, "{\"type\":\"PING\"}", &r));
}

// --- cast_parse_media_status -------------------------------------------------

void test_media_playing_full_metadata(void)
{
    const char *json =
        "{\"type\":\"MEDIA_STATUS\",\"status\":[{"
        "\"playerState\":\"PLAYING\",\"mediaSessionId\":7,"
        "\"currentTime\":42.5,"
        "\"supportedMediaCommands\":51,"
        "\"media\":{\"duration\":210.0,\"metadata\":{"
        "\"title\":\"Song A\",\"artist\":\"Artist B\","
        "\"images\":[{\"url\":\"https://i.scdn.co/image/abc\"}]}}}]}";
    cast_media_status_t m;
    memset(&m, 0, sizeof(m));
    TEST_ASSERT_TRUE(PARSE(cast_parse_media_status, json, &m));
    TEST_ASSERT_EQUAL_INT(CAST_PLAYER_PLAYING, m.state);
    TEST_ASSERT_EQUAL_INT(7, m.media_session_id);
    TEST_ASSERT_EQUAL_STRING("Song A", m.title);
    TEST_ASSERT_EQUAL_STRING("Artist B", m.subtitle);
    TEST_ASSERT_TRUE(m.has_media);
    TEST_ASSERT_EQUAL_STRING("https://i.scdn.co/image/abc", m.art_url);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 42.5, m.current_time);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 210.0, m.duration);
    // supportedMediaCommands = 51 (0x33) sets pause|seek|skipfwd|skipback bits.
    TEST_ASSERT_TRUE(m.supports_pause);
    TEST_ASSERT_TRUE(m.supports_seek);
    TEST_ASSERT_TRUE(m.supports_next);
    TEST_ASSERT_TRUE(m.supports_prev);
}

void test_media_commands_none(void)
{
    const char *json =
        "{\"type\":\"MEDIA_STATUS\",\"status\":[{"
        "\"playerState\":\"PAUSED\",\"supportedMediaCommands\":0}]}";
    cast_media_status_t m;
    memset(&m, 0, sizeof(m));
    TEST_ASSERT_TRUE(PARSE(cast_parse_media_status, json, &m));
    TEST_ASSERT_EQUAL_INT(CAST_PLAYER_PAUSED, m.state);
    TEST_ASSERT_FALSE(m.supports_pause);
    TEST_ASSERT_FALSE(m.supports_next);
    TEST_ASSERT_FALSE(m.supports_prev);
    TEST_ASSERT_FALSE(m.supports_seek);
}

// A track-change push often omits the `media` block: has_media must be false and
// the previous title must be preserved (the session re-requests a full status).
void test_media_partial_keeps_title(void)
{
    cast_media_status_t m;
    memset(&m, 0, sizeof(m));
    strcpy(m.title, "Previous Song");
    const char *json =
        "{\"type\":\"MEDIA_STATUS\",\"status\":[{\"playerState\":\"PLAYING\"}]}";
    TEST_ASSERT_TRUE(PARSE(cast_parse_media_status, json, &m));
    TEST_ASSERT_EQUAL_INT(CAST_PLAYER_PLAYING, m.state);
    TEST_ASSERT_FALSE(m.has_media);
    TEST_ASSERT_EQUAL_STRING("Previous Song", m.title);   // preserved
}

// Live streams have currentTime but no media.duration -> duration stays 0.
void test_media_live_has_position_no_duration(void)
{
    cast_media_status_t m;
    memset(&m, 0, sizeof(m));
    const char *json =
        "{\"type\":\"MEDIA_STATUS\",\"status\":[{"
        "\"playerState\":\"PLAYING\",\"currentTime\":99.0,"
        "\"media\":{\"metadata\":{\"title\":\"Live\"}}}]}";
    TEST_ASSERT_TRUE(PARSE(cast_parse_media_status, json, &m));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 99.0, m.current_time);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 0.0, m.duration);   // unknown / live
}

// A position-only push (no media block) updates currentTime but keeps duration.
void test_media_position_update_keeps_duration(void)
{
    cast_media_status_t m;
    memset(&m, 0, sizeof(m));
    m.duration = 210.0f;            // from a prior full status
    m.current_time = 42.5f;
    const char *json =
        "{\"type\":\"MEDIA_STATUS\",\"status\":[{"
        "\"playerState\":\"PLAYING\",\"currentTime\":55.0}]}";
    TEST_ASSERT_TRUE(PARSE(cast_parse_media_status, json, &m));
    TEST_ASSERT_FLOAT_WITHIN(0.01, 55.0, m.current_time);   // advanced
    TEST_ASSERT_FLOAT_WITHIN(0.01, 210.0, m.duration);      // preserved
}

void test_media_empty_status_array_is_false(void)
{
    cast_media_status_t m;
    memset(&m, 0, sizeof(m));
    TEST_ASSERT_FALSE(PARSE(cast_parse_media_status,
                            "{\"type\":\"MEDIA_STATUS\",\"status\":[]}", &m));
}

void test_media_artist_falls_back_to_subtitle(void)
{
    const char *json =
        "{\"type\":\"MEDIA_STATUS\",\"status\":[{\"playerState\":\"PLAYING\","
        "\"media\":{\"metadata\":{\"title\":\"T\",\"subtitle\":\"Sub\"}}}]}";
    cast_media_status_t m;
    memset(&m, 0, sizeof(m));
    TEST_ASSERT_TRUE(PARSE(cast_parse_media_status, json, &m));
    TEST_ASSERT_EQUAL_STRING("Sub", m.subtitle);
    TEST_ASSERT_TRUE(m.has_media);
    TEST_ASSERT_EQUAL_STRING("", m.art_url);   // no images
}

// --- cast_parse_multizone_status ---------------------------------------------

void test_multizone_members(void)
{
    const char *json =
        "{\"type\":\"MULTIZONE_STATUS\",\"status\":{\"devices\":["
        "{\"deviceId\":\"id-1\",\"name\":\"Kitchen\",\"volume\":{\"level\":0.5,\"muted\":false}},"
        "{\"deviceId\":\"id-2\",\"name\":\"Living\",\"volume\":{\"level\":0.8,\"muted\":true}}]}}";
    cast_member_t mem[CAST_MAX_MEMBERS];
    int n = PARSE(cast_parse_multizone_status, json, mem, CAST_MAX_MEMBERS);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_STRING("id-1", mem[0].id);
    TEST_ASSERT_EQUAL_STRING("Kitchen", mem[0].name);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, mem[0].level);
    TEST_ASSERT_FALSE(mem[0].muted);
    TEST_ASSERT_EQUAL_STRING("Living", mem[1].name);
    TEST_ASSERT_TRUE(mem[1].muted);
}

void test_multizone_wrong_type_is_negative(void)
{
    cast_member_t mem[CAST_MAX_MEMBERS];
    TEST_ASSERT_EQUAL_INT(-1, PARSE(cast_parse_multizone_status,
                                    "{\"type\":\"PING\"}", mem, CAST_MAX_MEMBERS));
}

void test_multizone_caps_at_max(void)
{
    // Three devices but max=2 -> only 2 parsed.
    const char *json =
        "{\"type\":\"MULTIZONE_STATUS\",\"status\":{\"devices\":["
        "{\"deviceId\":\"a\"},{\"deviceId\":\"b\"},{\"deviceId\":\"c\"}]}}";
    cast_member_t mem[2];
    TEST_ASSERT_EQUAL_INT(2, PARSE(cast_parse_multizone_status, json, mem, 2));
}

// --- cast_parse_device_updated -----------------------------------------------

void test_device_updated(void)
{
    const char *json =
        "{\"type\":\"DEVICE_UPDATED\",\"device\":{"
        "\"deviceId\":\"id-9\",\"name\":\"Bedroom\",\"volume\":{\"level\":0.25,\"muted\":false}}}";
    cast_member_t d;
    TEST_ASSERT_TRUE(PARSE(cast_parse_device_updated, json, &d));
    TEST_ASSERT_EQUAL_STRING("id-9", d.id);
    TEST_ASSERT_EQUAL_STRING("Bedroom", d.name);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.25f, d.level);
}

void test_device_updated_missing_device_is_false(void)
{
    cast_member_t d;
    TEST_ASSERT_FALSE(PARSE(cast_parse_device_updated,
                            "{\"type\":\"DEVICE_UPDATED\"}", &d));
}

// --- edge cases --------------------------------------------------------------

void test_empty_and_malformed(void)
{
    cast_media_status_t m; memset(&m, 0, sizeof(m));
    cast_receiver_status_t r;
    char t[8];
    TEST_ASSERT_FALSE(PARSE(cast_parse_media_status, "", &m));
    TEST_ASSERT_FALSE(PARSE(cast_parse_receiver_status, "{", &r));
    TEST_ASSERT_FALSE(PARSE(cast_parse_type, "", t, sizeof(t)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_type_extracts);
    RUN_TEST(test_type_missing_is_false);
    RUN_TEST(test_type_garbage_is_false);
    RUN_TEST(test_player_state_mapping);
    RUN_TEST(test_receiver_running_app);
    RUN_TEST(test_receiver_idle_no_app);
    RUN_TEST(test_receiver_wrong_type_is_false);
    RUN_TEST(test_media_playing_full_metadata);
    RUN_TEST(test_media_commands_none);
    RUN_TEST(test_media_partial_keeps_title);
    RUN_TEST(test_media_live_has_position_no_duration);
    RUN_TEST(test_media_position_update_keeps_duration);
    RUN_TEST(test_media_empty_status_array_is_false);
    RUN_TEST(test_media_artist_falls_back_to_subtitle);
    RUN_TEST(test_multizone_members);
    RUN_TEST(test_multizone_wrong_type_is_negative);
    RUN_TEST(test_multizone_caps_at_max);
    RUN_TEST(test_device_updated);
    RUN_TEST(test_device_updated_missing_device_is_false);
    RUN_TEST(test_empty_and_malformed);
    return UNITY_END();
}
