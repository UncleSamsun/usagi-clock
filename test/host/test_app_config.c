#include <string.h>

#include "unity.h"

#include "app_config.h"

#define MON_1116_0000 1794787200LL
static int64_t at(int h, int m) { return MON_1116_0000 + h * 3600LL + m * 60LL; }

static const uint8_t MAC[6] = { 0x24, 0x0a, 0xc4, 0x11, 0xab, 0xcd };
static app_settings_t s;

void setUp(void)
{
    s = app_settings_default(MAC);
}

void tearDown(void) {}

void test_defaults_are_valid(void)
{
    TEST_ASSERT_EQUAL_STRING("usagi-abcd", s.device_name);
    TEST_ASSERT_TRUE(app_settings_validate(&s));
    TEST_ASSERT_EQUAL_UINT8(70, s.default_volume);
    TEST_ASSERT_EQUAL_UINT16(1380, s.night_start_min);
    TEST_ASSERT_EQUAL_UINT16(420, s.night_end_min);
}

static void expect_invalid(app_settings_t bad)
{
    TEST_ASSERT_FALSE(app_settings_validate(&bad));
}

void test_validate_ranges(void)
{
    app_settings_t t;
    t = s; t.alarm.snooze_min = 0; expect_invalid(t);
    t = s; t.alarm.snooze_min = 31; expect_invalid(t);
    t = s; t.alarm.long_press_ms = 999; expect_invalid(t);
    t = s; t.alarm.long_press_ms = 5001; expect_invalid(t);
    t = s; t.alarm.auto_stop_min = 0; expect_invalid(t);
    t = s; t.alarm.auto_stop_min = 61; expect_invalid(t);
    t = s; t.day_brightness = 0; expect_invalid(t);
    t = s; t.night_brightness = 101; expect_invalid(t);
    t = s; t.default_volume = 101; expect_invalid(t);
    t = s; t.night_start_min = 1440; expect_invalid(t);
    t = s; t.night_end_min = 1440; expect_invalid(t);

    t = s; t.alarm.snooze_min = 1; t.alarm.long_press_ms = 1000; t.alarm.auto_stop_min = 1;
    TEST_ASSERT_TRUE(app_settings_validate(&t));
    t = s; t.alarm.snooze_min = 30; t.alarm.long_press_ms = 5000; t.alarm.auto_stop_min = 60;
    TEST_ASSERT_TRUE(app_settings_validate(&t));
}

void test_device_name_rules(void)
{
    TEST_ASSERT_TRUE(app_device_name_ok("usagi-mj"));
    TEST_ASSERT_TRUE(app_device_name_ok("a"));
    TEST_ASSERT_FALSE(app_device_name_ok(""));
    TEST_ASSERT_FALSE(app_device_name_ok("-a"));
    TEST_ASSERT_FALSE(app_device_name_ok("a-"));
    TEST_ASSERT_FALSE(app_device_name_ok("Usagi"));
    TEST_ASSERT_FALSE(app_device_name_ok("usagi_mj"));
    TEST_ASSERT_FALSE(app_device_name_ok("abcdefghijklmnopqrstuvwxy"));   /* 25자 */
    TEST_ASSERT_TRUE(app_device_name_ok("abcdefghijklmnopqrstuvwx"));     /* 24자 */
}

/* Review Focus 3 */
void test_night_window_wraps_midnight(void)
{
    TEST_ASSERT_FALSE(app_night_active(at(22, 59), 1380, 420));
    TEST_ASSERT_TRUE(app_night_active(at(23, 0), 1380, 420));
    TEST_ASSERT_TRUE(app_night_active(at(3, 0), 1380, 420));
    TEST_ASSERT_TRUE(app_night_active(at(6, 59), 1380, 420));
    TEST_ASSERT_FALSE(app_night_active(at(7, 0), 1380, 420));
}

void test_night_window_same_day_and_disabled(void)
{
    TEST_ASSERT_TRUE(app_night_active(at(1, 0), 60, 120));
    TEST_ASSERT_FALSE(app_night_active(at(2, 0), 60, 120));
    TEST_ASSERT_FALSE(app_night_active(at(0, 0), 0, 0));
    TEST_ASSERT_FALSE(app_night_active(at(12, 0), 0, 0));
}

/* Review Focus 3 */
void test_ringing_overrides_night(void)
{
    TEST_ASSERT_EQUAL_UINT8(10, app_brightness(&s, at(3, 0), false));
    TEST_ASSERT_EQUAL_UINT8(100, app_brightness(&s, at(3, 0), true));
    TEST_ASSERT_EQUAL_UINT8(80, app_brightness(&s, at(12, 0), false));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_defaults_are_valid);
    RUN_TEST(test_validate_ranges);
    RUN_TEST(test_device_name_rules);
    RUN_TEST(test_night_window_wraps_midnight);
    RUN_TEST(test_night_window_same_day_and_disabled);
    RUN_TEST(test_ringing_overrides_night);
    return UNITY_END();
}
