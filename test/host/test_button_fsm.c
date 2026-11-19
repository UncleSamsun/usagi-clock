#include "unity.h"

#include "button_fsm.h"

#define LONG_MS 2000u

static button_fsm_t btn;
static int n_short;
static int n_long;
static button_event_t last_ev;

/* from_ms부터 to_ms까지(둘 다 포함) 10ms 간격으로 같은 원시 입력을 넣고, 나온 이벤트를 센다.
 * 시각은 uint32_t로 더해 가므로 0xFFFFFFFF를 넘어 0으로 돌아가는 구간도 그대로 넣을 수 있다. */
static void feed(bool pressed, uint32_t from_ms, uint32_t to_ms)
{
    uint32_t steps = (to_ms - from_ms) / 10u;
    for (uint32_t k = 0; k <= steps; k++) {
        button_event_t ev = button_fsm_update(&btn, pressed, from_ms + k * 10u);
        if (ev == BUTTON_EV_SHORT) {
            n_short++;
        } else if (ev == BUTTON_EV_LONG) {
            n_long++;
        }
        if (ev != BUTTON_EV_NONE) {
            last_ev = ev;
        }
    }
}

void setUp(void)
{
    button_fsm_init(&btn, BUTTON_DEBOUNCE_MS, LONG_MS);
    n_short = 0;
    n_long = 0;
    last_ev = BUTTON_EV_NONE;
}

void tearDown(void) {}

/* 뗀 상태로 시작 → 100~300ms 누름 → 310ms에 뗌: SHORT 1개 */
void test_short_press(void)
{
    feed(false, 0, 90);
    feed(true, 100, 300);
    TEST_ASSERT_EQUAL_INT(0, n_short);   /* 떼기 전에는 아직 없음 */

    feed(false, 310, 400);

    TEST_ASSERT_EQUAL_INT(1, n_short);
    TEST_ASSERT_EQUAL_INT(0, n_long);
    TEST_ASSERT_EQUAL_INT(BUTTON_EV_SHORT, last_ev);
}

/* 접점이 10ms마다 눌림/뗌을 오가는 떨림(5회)은 30ms를 못 버티므로 무시 */
void test_bounce_is_ignored(void)
{
    feed(false, 0, 90);
    for (uint32_t k = 0; k < 5; k++) {
        feed(true, 100 + k * 20, 100 + k * 20);
        feed(false, 110 + k * 20, 110 + k * 20);
    }
    feed(false, 200, 500);

    TEST_ASSERT_EQUAL_INT(0, n_short);
    TEST_ASSERT_EQUAL_INT(0, n_long);
}

/* 누른 채로 있어도 눌림 확정(130ms) + 2000ms = 2130ms에 LONG이 나온다(떼기 전) */
void test_long_fires_while_held(void)
{
    feed(false, 0, 90);
    feed(true, 100, 2120);
    TEST_ASSERT_EQUAL_INT(0, n_long);

    feed(true, 2130, 2130);

    TEST_ASSERT_EQUAL_INT(1, n_long);
    TEST_ASSERT_EQUAL_INT(BUTTON_EV_LONG, last_ev);
}

/* 5초 동안 계속 눌러도 LONG은 한 번만 */
void test_long_fires_once(void)
{
    feed(false, 0, 90);
    feed(true, 100, 5100);

    TEST_ASSERT_EQUAL_INT(1, n_long);
    TEST_ASSERT_EQUAL_INT(0, n_short);
}

/* LONG 뒤에 손을 떼도 SHORT가 따라 나오지 않는다 */
void test_release_after_long_is_silent(void)
{
    feed(false, 0, 90);
    feed(true, 100, 5100);
    feed(false, 5110, 5400);

    TEST_ASSERT_EQUAL_INT(1, n_long);
    TEST_ASSERT_EQUAL_INT(0, n_short);
}

/* LONG이 나올 시각(2130ms)에 손이 이미 떨어져 있으면 SHORT.
 * 뗌은 30ms 뒤(2160ms)에야 확정되지만, 그 사이에 LONG을 내면 안 된다 */
void test_just_under_long_is_short(void)
{
    feed(false, 0, 90);
    feed(true, 100, 2120);
    feed(false, 2130, 2300);

    TEST_ASSERT_EQUAL_INT(1, n_short);
    TEST_ASSERT_EQUAL_INT(0, n_long);
}

/* 누르고 있는 중 10ms짜리 끊김(접점 떨림)은 뗀 것으로 보지 않는다 */
void test_release_glitch_while_held_is_ignored(void)
{
    feed(false, 0, 90);
    feed(true, 100, 1000);
    feed(false, 1010, 1010);
    feed(true, 1020, 1500);
    TEST_ASSERT_EQUAL_INT(0, n_short);

    feed(false, 1510, 1600);

    TEST_ASSERT_EQUAL_INT(1, n_short);
    TEST_ASSERT_EQUAL_INT(0, n_long);
}

/* 시각이 0xFFFFFF00에서 시작해 누르는 동안 0으로 넘어가도 LONG이 정확히 한 번 */
void test_millis_wraparound(void)
{
    const uint32_t s = 0xFFFFFF00u;
    feed(false, s, s + 90);
    feed(true, s + 100, s + 2120);
    TEST_ASSERT_EQUAL_INT(0, n_long);   /* 넘어간 직후 "아주 오래 눌렀다"로 착각하지 않음 */

    feed(true, s + 2130, s + 2200);

    TEST_ASSERT_EQUAL_INT(1, n_long);
    TEST_ASSERT_EQUAL_INT(0, n_short);
}

/* 누른 채 부팅하면(설정 모드 진입용) 그 누름은 알람 입력이 되지 않고, 뗀 다음 누름부터 판별한다 */
void test_pressed_at_init_needs_release_first(void)
{
    feed(true, 0, 3000);
    feed(false, 3010, 3100);
    TEST_ASSERT_EQUAL_INT(0, n_short);
    TEST_ASSERT_EQUAL_INT(0, n_long);

    feed(true, 3200, 3400);
    feed(false, 3410, 3500);

    TEST_ASSERT_EQUAL_INT(1, n_short);
    TEST_ASSERT_EQUAL_INT(0, n_long);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_short_press);
    RUN_TEST(test_bounce_is_ignored);
    RUN_TEST(test_long_fires_while_held);
    RUN_TEST(test_long_fires_once);
    RUN_TEST(test_release_after_long_is_silent);
    RUN_TEST(test_just_under_long_is_short);
    RUN_TEST(test_release_glitch_while_held_is_ignored);
    RUN_TEST(test_millis_wraparound);
    RUN_TEST(test_pressed_at_init_needs_release_first);

    return UNITY_END();
}
