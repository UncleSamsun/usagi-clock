#include "unity.h"

#include "timekeeper.h"

void setUp(void) {}
void tearDown(void) {}

void test_plausible_accepts_range(void)
{
    TEST_ASSERT_TRUE(timekeeper_utc_plausible(1767225600LL));   /* 2026-01-01 00:00:00 */
    TEST_ASSERT_TRUE(timekeeper_utc_plausible(1794780000LL));   /* 2026-11-15 22:00:00 */
    TEST_ASSERT_TRUE(timekeeper_utc_plausible(4102444799LL));   /* 2099-12-31 23:59:59 */
}

/* Review Focus 2: RTC 기본값(2000년)이나 쓰레기 값으로 알람을 돌리지 않는다 */
void test_plausible_rejects_before_2026(void)
{
    TEST_ASSERT_FALSE(timekeeper_utc_plausible(946684800LL));   /* 2000-01-01 */
    TEST_ASSERT_FALSE(timekeeper_utc_plausible(1767225599LL));
    TEST_ASSERT_FALSE(timekeeper_utc_plausible(0));
    TEST_ASSERT_FALSE(timekeeper_utc_plausible(-1));
}

void test_plausible_rejects_2100(void)
{
    TEST_ASSERT_FALSE(timekeeper_utc_plausible(4102444800LL));  /* 2100-01-01 */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_plausible_accepts_range);
    RUN_TEST(test_plausible_rejects_before_2026);
    RUN_TEST(test_plausible_rejects_2100);
    return UNITY_END();
}
