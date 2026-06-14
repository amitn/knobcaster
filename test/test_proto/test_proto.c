// Host-native unit tests for the CASTV2 protobuf codec (cast_proto.c).
// Pure C, no ESP deps — see docs/07-testing.md.
#include <string.h>
#include <unity.h>

#include "cast_proto.h"
#include "cast_proto.c"   // compile the implementation into this test TU

void setUp(void) {}
void tearDown(void) {}

// Encode a STRING CastMessage, decode it back, and check the fields survive.
static void roundtrip(const char *ns, const char *payload)
{
    uint8_t buf[512];
    int n = cast_msg_encode(buf, sizeof(buf), "sender-0", "receiver-0", ns, payload);
    TEST_ASSERT_GREATER_THAN_INT(0, n);

    cast_msg_t m;
    TEST_ASSERT_TRUE(cast_msg_decode(buf, (size_t)n, &m));
    TEST_ASSERT_EQUAL_STRING(ns, m.ns);
    TEST_ASSERT_EQUAL_INT(0, m.payload_type);          // STRING
    TEST_ASSERT_EQUAL_UINT(strlen(payload), m.payload_len);
    if (m.payload_len > 0)
        TEST_ASSERT_EQUAL_MEMORY(payload, m.payload, m.payload_len);
}

void test_roundtrip_simple(void)
{
    roundtrip(CAST_NS_RECEIVER, "{\"type\":\"GET_STATUS\",\"requestId\":1}");
}

void test_roundtrip_long_payload(void)
{
    // > 127 bytes forces a multi-byte varint length field.
    char big[400];
    memset(big, 'x', sizeof(big));
    big[0] = '{'; big[sizeof(big) - 2] = '}'; big[sizeof(big) - 1] = '\0';
    roundtrip(CAST_NS_MEDIA, big);
}

void test_roundtrip_empty_payload(void)
{
    roundtrip(CAST_NS_HEARTBEAT, "");
}

void test_encode_overflow_returns_negative(void)
{
    uint8_t small[8];
    int n = cast_msg_encode(small, sizeof(small), "sender-0", "receiver-0",
                            CAST_NS_RECEIVER, "{\"type\":\"GET_STATUS\"}");
    TEST_ASSERT_LESS_THAN_INT(0, n);
}

void test_decode_truncated_returns_false(void)
{
    uint8_t buf[512];
    int n = cast_msg_encode(buf, sizeof(buf), "sender-0", "receiver-0",
                            CAST_NS_RECEIVER, "{\"type\":\"PING\"}");
    TEST_ASSERT_GREATER_THAN_INT(2, n);
    cast_msg_t m;
    // Chop the last few bytes — a length-delimited field now runs past the end.
    TEST_ASSERT_FALSE(cast_msg_decode(buf, (size_t)n - 3, &m));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_roundtrip_simple);
    RUN_TEST(test_roundtrip_long_payload);
    RUN_TEST(test_roundtrip_empty_payload);
    RUN_TEST(test_encode_overflow_returns_negative);
    RUN_TEST(test_decode_truncated_returns_false);
    return UNITY_END();
}
