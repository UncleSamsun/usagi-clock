#include "unity.h"

#include "alarm_core.h"

void setUp(void) {}
void tearDown(void) {}

/* Task 1 스모크 테스트: 빌드와 테스트 실행 경로가 살아 있는지만 확인한다. */
static void test_smoke(void)
{
    TEST_ASSERT_EQUAL_INT(9 * 3600, ALARM_TZ_OFFSET_S);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_smoke);
    return UNITY_END();
}
