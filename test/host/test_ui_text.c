#include <string.h>

#include "unity.h"

#include "ui_text.h"

#define MON_1116_0000 1794787200LL   /* 2026-11-16(월) 00:00 로컬 */
#define MON_0700      1794812400LL
#define SUN_1122_2357 1795391820LL

static int64_t at(int h, int m) { return MON_1116_0000 + h * 3600LL + m * 60LL; }

void setUp(void) {}
void tearDown(void) {}

static void check_clock(int h, int m, const char *text, bool pm)
{
    char out[8];
    bool got_pm = !pm;
    ui_fmt_clock(at(h, m), out, sizeof(out), &got_pm);
    TEST_ASSERT_EQUAL_STRING(text, out);
    TEST_ASSERT_EQUAL(pm, got_pm);
}

void test_clock_morning(void)
{
    check_clock(7, 30, "7:30", false);
}

/* Review Focus 4: 자정과 정오 경계 */
void test_clock_midnight_and_noon(void)
{
    check_clock(0, 5, "12:05", false);
    check_clock(0, 59, "12:59", false);
    check_clock(12, 0, "12:00", true);
    check_clock(13, 7, "1:07", true);
    check_clock(23, 59, "11:59", true);
}

void test_date_format(void)
{
    char out[16];
    ui_fmt_date(MON_0700, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("MON, NOV 16", out);
    ui_fmt_date(SUN_1122_2357, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("SUN, NOV 22", out);
}

void test_countdown(void)
{
    char out[8];
    ui_fmt_countdown(299, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("4:59", out);
    ui_fmt_countdown(3600, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("60:00", out);
    ui_fmt_countdown(59, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("0:59", out);
    ui_fmt_countdown(0, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("0:00", out);
    ui_fmt_countdown(-5, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("0:00", out);
}

void test_small_buffer_is_truncated_safely(void)
{
    char out[8];
    memset(out, 'x', sizeof(out));
    ui_fmt_date(MON_0700, out, 3);
    TEST_ASSERT_EQUAL_CHAR('\0', out[2]);
    TEST_ASSERT_EQUAL_CHAR('x', out[3]);   /* 넘쳐 쓰지 않음 */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_clock_morning);
    RUN_TEST(test_clock_midnight_and_noon);
    RUN_TEST(test_date_format);
    RUN_TEST(test_countdown);
    RUN_TEST(test_small_buffer_is_truncated_safely);
    return UNITY_END();
}
