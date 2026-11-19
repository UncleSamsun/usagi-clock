#include "unity.h"

#include "alarm_core.h"

/* 테스트 상수(로컬 epoch 초) */
#define MON_0700 1794812400LL      /* 2026-11-16(월) 07:00 */
#define SUN_1122_2357 1795391820LL /* 2026-11-22(일) 23:57 */
#define MON_1123_0002 1795392120LL /* 2026-11-23(월) 00:02 */

/* 모든 테스트가 같이 쓰는 엔진. setUp에서 매번 새로 초기화한다. */
static alarm_engine_t eng;

/* 테스트용 알람: 켜져 있고 볼륨 50 */
static alarm_t mk(uint8_t id, int h, int m, uint8_t days, uint8_t pre)
{
    alarm_t a = {0};
    a.id = id;
    a.enabled = true;
    a.hour = (uint8_t)h;
    a.minute = (uint8_t)m;
    a.days_mask = days;
    a.pre_minutes = pre;
    a.pre_volume = 50;
    a.main_volume = 50;
    return a;
}

/* 알람 하나만 엔진에 넣는 도우미 */
static void set_one(alarm_t a)
{
    alarm_engine_set_alarms(&eng, &a, 1);
}

void setUp(void)
{
    alarm_settings_t s = alarm_settings_default();
    alarm_engine_init(&eng, &s);
}

void tearDown(void) {}

/* ===== 미리 알림 시작 ===== */

/* 매일 07:00, 미리 알림 5분: 06:54:59(정각 301초 전)에는 아직 아무 일도 없다 */
void test_idle_before_pre_window(void)
{
    set_one(mk(1, 7, 0, ALARM_DAYS_EVERYDAY, 5));

    alarm_output_t out = alarm_engine_tick(&eng, MON_0700 - 301);

    TEST_ASSERT_EQUAL_UINT32(0, out.actions);
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_IDLE, alarm_engine_view(&eng).state);
}

/* 같은 알람: 06:55:00(300초 전)에 미리 알림 시작(클립 0번), 다음 초에 클립 1번 */
void test_pre_starts_at_window_and_emits_clips(void)
{
    set_one(mk(1, 7, 0, ALARM_DAYS_EVERYDAY, 5));
    alarm_engine_tick(&eng, MON_0700 - 301);   /* 첫 틱: 기준점 만들기 */

    alarm_output_t out = alarm_engine_tick(&eng, MON_0700 - 300);
    TEST_ASSERT_TRUE(out.actions & ALARM_ACT_PRE_CLIP);   /* 비트 하나가 켜졌는지 */
    TEST_ASSERT_EQUAL_UINT16(0, out.pre_clip_seq);
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_PRE, alarm_engine_view(&eng).state);

    out = alarm_engine_tick(&eng, MON_0700 - 299);
    TEST_ASSERT_TRUE(out.actions & ALARM_ACT_PRE_CLIP);
    TEST_ASSERT_EQUAL_UINT16(1, out.pre_clip_seq);
}

/* ===== 틱 판단 테스트 ===== */

/* 미리 알림 중 정각(07:00:00) 틱 → MAIN_START, 상태 RINGING, view.ring_at == MON_0700 */
void test_rings_at_exact_time(void)
{
    set_one(mk(1, 7, 0, ALARM_DAYS_EVERYDAY, 5));
    alarm_engine_tick(&eng, MON_0700 - 301);           /* 기준점 */
    for (int64_t t = MON_0700 - 300; t < MON_0700; t++) {
        alarm_engine_tick(&eng, t);                    /* 06:55:00 ~ 06:59:59 미리 알림 */
    }

    alarm_output_t out = alarm_engine_tick(&eng, MON_0700);

    TEST_ASSERT_TRUE(out.actions & ALARM_ACT_MAIN_START);
    TEST_ASSERT_FALSE(out.actions & ALARM_ACT_PRE_CLIP);
    TEST_ASSERT_EQUAL_UINT8(1, out.alarm_id);
    alarm_view_t v = alarm_engine_view(&eng);
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_RINGING, v.state);
    TEST_ASSERT_EQUAL_INT64(MON_0700, v.ring_at);
}

/* 미리 알림 0분: 06:59:59에는 아무 일 없고, 07:00:00에 MAIN_START(PRE_CLIP 없음) */
void test_pre_zero_rings_without_pre(void)
{
    set_one(mk(1, 7, 0, ALARM_DAYS_EVERYDAY, 0));

    alarm_output_t out = alarm_engine_tick(&eng, MON_0700 - 1);
    TEST_ASSERT_EQUAL_UINT32(0, out.actions);

    out = alarm_engine_tick(&eng, MON_0700);
    TEST_ASSERT_TRUE(out.actions & ALARM_ACT_MAIN_START);
    TEST_ASSERT_FALSE(out.actions & ALARM_ACT_PRE_CLIP);
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_RINGING, alarm_engine_view(&eng).state);
}

/* ===== 시간 건너뜀과 부팅 ===== */

/* 미리 알림 0분: 06:59:30 다음 틱이 07:01:00(90초 건너뜀, 허용 120초 이내) → 그래도 MAIN_START */
void test_small_forward_jump_still_rings(void)
{
    set_one(mk(1, 7, 0, ALARM_DAYS_EVERYDAY, 0));
    alarm_engine_tick(&eng, MON_0700 - 30);

    alarm_output_t out = alarm_engine_tick(&eng, MON_0700 + 60);

    TEST_ASSERT_TRUE(out.actions & ALARM_ACT_MAIN_START);
    TEST_ASSERT_EQUAL_INT64(MON_0700, alarm_engine_view(&eng).ring_at);
}

/* 미리 알림 0분: 06:59:30 다음 틱이 07:02:01(151초 건너뜀) → 울리지 않음(actions 0, IDLE) */
void test_large_forward_jump_skips(void)
{
    set_one(mk(1, 7, 0, ALARM_DAYS_EVERYDAY, 0));
    alarm_engine_tick(&eng, MON_0700 - 30);

    alarm_output_t out = alarm_engine_tick(&eng, MON_0700 + 121);

    TEST_ASSERT_EQUAL_UINT32(0, out.actions);
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_IDLE, alarm_engine_view(&eng).state);
}

/* 첫 틱이 07:00:05(전원이 그 사이에 켜짐) → 울리지 않음(부팅 후 따라잡기 없음) */
void test_first_tick_after_alarm_time_does_not_ring(void)
{
    set_one(mk(1, 7, 0, ALARM_DAYS_EVERYDAY, 0));

    alarm_output_t out = alarm_engine_tick(&eng, MON_0700 + 5);

    TEST_ASSERT_EQUAL_UINT32(0, out.actions);
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_IDLE, alarm_engine_view(&eng).state);
}

/* ===== Review Focus ===== */

/* 1: 06:57에 07:00 알람(미리 알림 5분)을 새로 만들면 다음 틱에 바로 "우! 나!" */
void test_alarm_created_inside_pre_window_starts_pre(void)
{
    alarm_output_t out = alarm_engine_tick(&eng, MON_0700 - 180);   /* 알람 0개 */
    TEST_ASSERT_EQUAL_UINT32(0, out.actions);

    set_one(mk(1, 7, 0, ALARM_DAYS_EVERYDAY, 5));                  /* 06:57:00에 새로 만듦 */
    out = alarm_engine_tick(&eng, MON_0700 - 179);

    TEST_ASSERT_TRUE(out.actions & ALARM_ACT_PRE_CLIP);
    TEST_ASSERT_EQUAL_UINT16(0, out.pre_clip_seq);
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_PRE, alarm_engine_view(&eng).state);
}

/* 2: 울린 뒤 SNTP가 시간을 뒤로 보정해도(07:00:10 → 06:59:50) 같은 회차가 두 번 울리지 않는다 */
void test_backward_jump_does_not_ring_twice(void)
{
    set_one(mk(1, 7, 0, ALARM_DAYS_EVERYDAY, 0));
    alarm_engine_tick(&eng, MON_0700 - 1);
    alarm_output_t out = alarm_engine_tick(&eng, MON_0700);
    TEST_ASSERT_TRUE(out.actions & ALARM_ACT_MAIN_START);

    alarm_engine_input(&eng, ALARM_INPUT_LONG, MON_0700);          /* 길게 눌러 끄기 */
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_IDLE, alarm_engine_view(&eng).state);
    alarm_engine_tick(&eng, MON_0700 + 10);

    for (int64_t t = MON_0700 - 10; t <= MON_0700; t++) {         /* 뒤로 보정된 뒤 다시 07:00을 지남 */
        out = alarm_engine_tick(&eng, t);
        TEST_ASSERT_FALSE(out.actions & ALARM_ACT_MAIN_START);
    }
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_IDLE, alarm_engine_view(&eng).state);
}

/* 3: 월요일만 00:02, 미리 알림 5분 → 일요일 23:57에 시작해야 한다(자정과 요일을 넘음) */
void test_pre_window_crosses_midnight_and_weekday(void)
{
    set_one(mk(1, 0, 2, ALARM_DAY_MON, 5));

    alarm_output_t out = alarm_engine_tick(&eng, SUN_1122_2357 - 1);
    TEST_ASSERT_EQUAL_UINT32(0, out.actions);

    out = alarm_engine_tick(&eng, SUN_1122_2357);
    TEST_ASSERT_TRUE(out.actions & ALARM_ACT_PRE_CLIP);
    TEST_ASSERT_EQUAL_INT64(MON_1123_0002, alarm_engine_view(&eng).ring_at);

    for (int64_t t = SUN_1122_2357 + 1; t < MON_1123_0002; t++) {  /* 자정을 지나는 동안 계속 미리 알림 */
        out = alarm_engine_tick(&eng, t);
        TEST_ASSERT_TRUE(out.actions & ALARM_ACT_PRE_CLIP);
        TEST_ASSERT_FALSE(out.actions & ALARM_ACT_MAIN_START);
    }

    out = alarm_engine_tick(&eng, MON_1123_0002);
    TEST_ASSERT_TRUE(out.actions & ALARM_ACT_MAIN_START);
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_RINGING, alarm_engine_view(&eng).state);
}

/* 미리 알림 중 길게 눌러 끈 뒤, 웹에서 다른 알람을 고쳐 목록이 다시 와도(순서도 바뀜)
 * 끈 회차의 미리 알림이 다시 시작되지 않는다 → set_alarms가 id로 handled_ring_at을 물려받는다 */
void test_alarm_list_change_keeps_handled_occurrence(void)
{
    alarm_t list[2] = { mk(1, 7, 0, ALARM_DAYS_EVERYDAY, 5), mk(2, 9, 0, ALARM_DAYS_EVERYDAY, 0) };
    alarm_engine_set_alarms(&eng, list, 2);
    alarm_engine_tick(&eng, MON_0700 - 301);                       /* 기준점 */
    alarm_output_t out = alarm_engine_tick(&eng, MON_0700 - 300);  /* 06:55 "우! 나!" 시작 */
    TEST_ASSERT_TRUE(out.actions & ALARM_ACT_PRE_CLIP);

    alarm_engine_input(&eng, ALARM_INPUT_LONG, MON_0700 - 300);    /* 길게 눌러 이번 회차 끄기 */
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_IDLE, alarm_engine_view(&eng).state);

    /* id 2를 09:30으로 고침 + 목록 순서가 바뀜(id 1이 0번 → 1번 칸) */
    alarm_t changed[2] = { mk(2, 9, 30, ALARM_DAYS_EVERYDAY, 0), list[0] };
    alarm_engine_set_alarms(&eng, changed, 2);

    for (int64_t t = MON_0700 - 299; t <= MON_0700; t++) {         /* 06:55:01 ~ 07:00:00 */
        out = alarm_engine_tick(&eng, t);
        TEST_ASSERT_EQUAL_UINT32(0, out.actions);
    }
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_IDLE, alarm_engine_view(&eng).state);
}

/* 꺼진 알람(enabled = false)은 미리 알림도 울림도 없다(미리 알림 구간과 정각 모두 틱해 본다) */
void test_disabled_alarm_never_fires(void)
{
    alarm_t a = mk(1, 7, 0, ALARM_DAYS_EVERYDAY, 5);
    a.enabled = false;
    set_one(a);

    for (int64_t t = MON_0700 - 301; t <= MON_0700 + 10; t++) {
        alarm_output_t out = alarm_engine_tick(&eng, t);
        TEST_ASSERT_EQUAL_UINT32(0, out.actions);
    }
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_IDLE, alarm_engine_view(&eng).state);
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_idle_before_pre_window);
    RUN_TEST(test_pre_starts_at_window_and_emits_clips);
    RUN_TEST(test_rings_at_exact_time);
    RUN_TEST(test_pre_zero_rings_without_pre);

    /* 시간 건너뜀과 부팅 */
    RUN_TEST(test_small_forward_jump_still_rings);
    RUN_TEST(test_large_forward_jump_skips);
    RUN_TEST(test_first_tick_after_alarm_time_does_not_ring);

    /* Review Focus */
    RUN_TEST(test_alarm_created_inside_pre_window_starts_pre);
    RUN_TEST(test_backward_jump_does_not_ring_twice);
    RUN_TEST(test_pre_window_crosses_midnight_and_weekday);

    RUN_TEST(test_alarm_list_change_keeps_handled_occurrence);
    RUN_TEST(test_disabled_alarm_never_fires);

    return UNITY_END();
}
