#include <string.h>

#include "unity.h"

#include "store_codec.h"

static uint8_t buf[STORE_ALARMS_MAX_BYTES];
static alarm_t out[ALARM_MAX];

static alarm_t mk(uint8_t id, int h, int m)
{
    alarm_t a;
    memset(&a, 0, sizeof(a));
    a.id = id;
    a.enabled = true;
    a.hour = (uint8_t)h;
    a.minute = (uint8_t)m;
    a.days_mask = ALARM_DAYS_WEEKDAYS;
    a.pre_minutes = 5;
    a.pre_volume = 60;
    a.main_volume = 70;
    strcpy(a.label, "출근");
    strcpy(a.song, "a.mp3");
    return a;
}

void setUp(void) {}
void tearDown(void) {}

void test_crc32_known_vector(void)
{
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, store_crc32((const uint8_t *)"123456789", 9));
}

void test_alarms_round_trip(void)
{
    alarm_t in[3] = { mk(1, 7, 0), mk(2, 8, 30), mk(5, 22, 15) };
    size_t len = store_encode_alarms(in, 3, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_size_t(16 + 3 * sizeof(alarm_t), len);
    size_t n = 99, dropped = 99;
    TEST_ASSERT_TRUE(store_decode_alarms(buf, len, out, &n, &dropped));
    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_size_t(0, dropped);
    TEST_ASSERT_EQUAL_MEMORY(in, out, 3 * sizeof(alarm_t));
}

void test_alarms_round_trip_empty(void)
{
    size_t len = store_encode_alarms(NULL, 0, buf, sizeof(buf));
    size_t n = 99, dropped = 99;
    TEST_ASSERT_TRUE(store_decode_alarms(buf, len, out, &n, &dropped));
    TEST_ASSERT_EQUAL_size_t(0, n);
}

/* Review Focus 1 */
void test_alarms_decode_rejects_bad_crc(void)
{
    alarm_t in[2] = { mk(1, 7, 0), mk(2, 8, 0) };
    size_t len = store_encode_alarms(in, 2, buf, sizeof(buf));
    buf[20] ^= 0x01;
    size_t n, dropped;
    TEST_ASSERT_FALSE(store_decode_alarms(buf, len, out, &n, &dropped));
}

/* Review Focus 1 */
void test_alarms_decode_rejects_truncated(void)
{
    alarm_t in[2] = { mk(1, 7, 0), mk(2, 8, 0) };
    size_t len = store_encode_alarms(in, 2, buf, sizeof(buf));
    size_t n, dropped;
    TEST_ASSERT_FALSE(store_decode_alarms(buf, len - 1, out, &n, &dropped));
    TEST_ASSERT_FALSE(store_decode_alarms(buf, 8, out, &n, &dropped));
    TEST_ASSERT_FALSE(store_decode_alarms(buf, 0, out, &n, &dropped));
}

void test_alarms_decode_rejects_wrong_magic_and_version(void)
{
    alarm_t in[1] = { mk(1, 7, 0) };
    size_t len = store_encode_alarms(in, 1, buf, sizeof(buf));
    size_t n, dropped;
    uint8_t copy[STORE_ALARMS_MAX_BYTES];

    memcpy(copy, buf, len);
    copy[0] = 0x55; copy[1] = 0x53; copy[2] = 0x47; copy[3] = 0x53;   /* SETTINGS magic */
    TEST_ASSERT_FALSE(store_decode_alarms(copy, len, out, &n, &dropped));

    memcpy(copy, buf, len);
    copy[4] = 2;                                                       /* version 2 */
    TEST_ASSERT_FALSE(store_decode_alarms(copy, len, out, &n, &dropped));
}

/* Review Focus 1: CRC는 맞지만 값이 이상한 알람은 그 알람만 버린다 */
void test_alarms_decode_drops_invalid_alarm(void)
{
    alarm_t in[3] = { mk(1, 7, 0), mk(2, 8, 0), mk(3, 9, 0) };
    in[1].hour = 24;
    size_t len = store_encode_alarms(in, 3, buf, sizeof(buf));
    size_t n = 0, dropped = 0;
    TEST_ASSERT_TRUE(store_decode_alarms(buf, len, out, &n, &dropped));
    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_EQUAL_size_t(1, dropped);
    TEST_ASSERT_EQUAL_UINT8(1, out[0].id);
    TEST_ASSERT_EQUAL_UINT8(3, out[1].id);
}

void test_encode_buffer_too_small(void)
{
    alarm_t in[2] = { mk(1, 7, 0), mk(2, 8, 0) };
    TEST_ASSERT_EQUAL_size_t(0, store_encode_alarms(in, 2, buf, 100));
}

void test_settings_round_trip_and_invalid(void)
{
    static const uint8_t mac[6] = { 0, 0, 0, 0, 0x12, 0x34 };
    app_settings_t s = app_settings_default(mac);
    uint8_t sb[STORE_SETTINGS_BYTES];
    size_t len = store_encode_settings(&s, sb, sizeof(sb));
    app_settings_t got;
    TEST_ASSERT_TRUE(store_decode_settings(sb, len, &got));
    TEST_ASSERT_EQUAL_MEMORY(&s, &got, sizeof(s));

    s.alarm.snooze_min = 0;
    len = store_encode_settings(&s, sb, sizeof(sb));
    TEST_ASSERT_FALSE(store_decode_settings(sb, len, &got));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc32_known_vector);
    RUN_TEST(test_alarms_round_trip);
    RUN_TEST(test_alarms_round_trip_empty);
    RUN_TEST(test_alarms_decode_rejects_bad_crc);
    RUN_TEST(test_alarms_decode_rejects_truncated);
    RUN_TEST(test_alarms_decode_rejects_wrong_magic_and_version);
    RUN_TEST(test_alarms_decode_drops_invalid_alarm);
    RUN_TEST(test_encode_buffer_too_small);
    RUN_TEST(test_settings_round_trip_and_invalid);
    return UNITY_END();
}
