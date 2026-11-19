#include "unity.h"

#include "net.h"

static net_policy_t p;

void setUp(void) {}
void tearDown(void) {}

void test_no_creds_starts_ap(void)
{
    net_policy_init(&p, false, false, 0);
    TEST_ASSERT_EQUAL_INT(NET_MODE_AP, net_policy_mode(&p, 0));
    TEST_ASSERT_EQUAL_INT(NET_WHY_NO_CREDS, net_policy_why(&p));
}

void test_creds_start_sta(void)
{
    net_policy_init(&p, true, false, 0);
    TEST_ASSERT_EQUAL_INT(NET_MODE_STA, net_policy_mode(&p, 0));
    TEST_ASSERT_EQUAL_INT(NET_WHY_NORMAL, net_policy_why(&p));
}

/* Review Focus 4 */
void test_sta_failure_5min_enters_ap(void)
{
    net_policy_init(&p, true, false, 0);
    net_policy_on_connected(&p, 10);
    net_policy_on_disconnected(&p, 100);
    TEST_ASSERT_EQUAL_INT(NET_MODE_STA, net_policy_mode(&p, 399));
    TEST_ASSERT_EQUAL_INT(NET_MODE_APSTA, net_policy_mode(&p, 400));
    TEST_ASSERT_EQUAL_INT(NET_WHY_STA_FAILED, net_policy_why(&p));
}

void test_repeated_disconnects_do_not_reset_timer(void)
{
    net_policy_init(&p, true, false, 0);
    net_policy_on_connected(&p, 10);
    net_policy_on_disconnected(&p, 100);
    net_policy_on_disconnected(&p, 250);   /* 재시도 실패가 또 와도 처음 끊긴 시각부터 센다 */
    TEST_ASSERT_EQUAL_INT(NET_MODE_APSTA, net_policy_mode(&p, 400));
}

void test_never_connected_counts_from_boot(void)
{
    net_policy_init(&p, true, false, 0);
    TEST_ASSERT_EQUAL_INT(NET_MODE_STA, net_policy_mode(&p, 299));
    TEST_ASSERT_EQUAL_INT(NET_MODE_APSTA, net_policy_mode(&p, 300));
}

/* Review Focus 4 */
void test_sta_recovers_leaves_ap(void)
{
    net_policy_init(&p, true, false, 0);
    net_policy_on_connected(&p, 10);
    net_policy_on_disconnected(&p, 100);
    TEST_ASSERT_EQUAL_INT(NET_MODE_APSTA, net_policy_mode(&p, 400));
    net_policy_on_connected(&p, 500);
    TEST_ASSERT_EQUAL_INT(NET_MODE_STA, net_policy_mode(&p, 500));
}

void test_button_setup_stays_until_reboot(void)
{
    net_policy_init(&p, true, true, 0);
    TEST_ASSERT_EQUAL_INT(NET_MODE_APSTA, net_policy_mode(&p, 0));
    TEST_ASSERT_EQUAL_INT(NET_WHY_BUTTON, net_policy_why(&p));
    net_policy_on_connected(&p, 5);
    TEST_ASSERT_EQUAL_INT(NET_MODE_APSTA, net_policy_mode(&p, 1000));

    net_policy_init(&p, false, true, 0);
    TEST_ASSERT_EQUAL_INT(NET_MODE_AP, net_policy_mode(&p, 0));
}

void test_creds_saved_from_ap(void)
{
    net_policy_init(&p, false, false, 0);
    TEST_ASSERT_EQUAL_INT(NET_MODE_AP, net_policy_mode(&p, 0));
    net_policy_on_creds_saved(&p, 10);
    TEST_ASSERT_EQUAL_INT(NET_MODE_APSTA, net_policy_mode(&p, 20));   /* 휴대폰 연결 유지 */
    net_policy_on_connected(&p, 20);
    TEST_ASSERT_EQUAL_INT(NET_MODE_STA, net_policy_mode(&p, 21));
}

void test_creds_changed_while_connected(void)
{
    net_policy_init(&p, true, false, 0);
    net_policy_on_connected(&p, 1);
    net_policy_on_creds_saved(&p, 100);
    TEST_ASSERT_EQUAL_INT(NET_MODE_STA, net_policy_mode(&p, 399));
    TEST_ASSERT_EQUAL_INT(NET_MODE_APSTA, net_policy_mode(&p, 400));   /* 새 정보로도 5분 실패 */
}

void test_boot_hold_classify(void)
{
    TEST_ASSERT_EQUAL_INT(BOOT_HOLD_NONE, boot_hold_classify(false, 0));
    TEST_ASSERT_EQUAL_INT(BOOT_HOLD_SETUP, boot_hold_classify(true, 500));
    TEST_ASSERT_EQUAL_INT(BOOT_HOLD_SETUP, boot_hold_classify(true, 9999));
    TEST_ASSERT_EQUAL_INT(BOOT_HOLD_FACTORY_RESET, boot_hold_classify(true, 10000));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_creds_starts_ap);
    RUN_TEST(test_creds_start_sta);
    RUN_TEST(test_sta_failure_5min_enters_ap);
    RUN_TEST(test_repeated_disconnects_do_not_reset_timer);
    RUN_TEST(test_never_connected_counts_from_boot);
    RUN_TEST(test_sta_recovers_leaves_ap);
    RUN_TEST(test_button_setup_stays_until_reboot);
    RUN_TEST(test_creds_saved_from_ap);
    RUN_TEST(test_creds_changed_while_connected);
    RUN_TEST(test_boot_hold_classify);
    return UNITY_END();
}
