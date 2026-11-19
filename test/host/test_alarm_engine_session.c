#include "unity.h"

#include "alarm_core.h"

/* 테스트 상수(로컬 epoch 초) */
#define MON_0700 1794812400LL /* 2026-11-16(월) 07:00 */
#define DAY      86400LL

/* 모든 테스트가 같이 쓰는 엔진. setUp에서 매번 새로 초기화한다(스누즈 5분, 자동 정지 10분). */
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

static void set_one(alarm_t a)
{
    alarm_engine_set_alarms(&eng, &a, 1);
}

/* id 1, 07:00, 미리 알림 0분 알람을 07:00:00에 울리게 만든다 */
static void ring_at_0700(uint8_t days)
{
    set_one(mk(1, 7, 0, days, 0));
    alarm_engine_tick(&eng, MON_0700 - 1);
    alarm_output_t out = alarm_engine_tick(&eng, MON_0700);
    TEST_ASSERT_TRUE(out.actions & ALARM_ACT_MAIN_START);
}

/* id 1, 07:00, 미리 알림 5분 알람을 06:55:00에 미리 알림 상태로 만든다 */
static void pre_at_0655(uint8_t days)
{
    set_one(mk(1, 7, 0, days, 5));
    alarm_engine_tick(&eng, MON_0700 - 301);
    alarm_output_t out = alarm_engine_tick(&eng, MON_0700 - 300);
    TEST_ASSERT_TRUE(out.actions & ALARM_ACT_PRE_CLIP);
}

void setUp(void)
{
    alarm_settings_t s = alarm_settings_default();
    alarm_engine_init(&eng, &s);
}

void tearDown(void) {}

/* ===== 짧게 누르기 ===== */

/* 미리 알림 중 SHORT → 소리만 멈추고(PRE_MUTED) 정각에는 그대로 울린다 */
void test_short_in_pre_mutes_but_rings_on_time(void)
{
    pre_at_0655(ALARM_DAYS_EVERYDAY);

    alarm_output_t out = alarm_engine_input(&eng, ALARM_INPUT_SHORT, MON_0700 - 300);
    TEST_ASSERT_EQUAL_UINT32(ALARM_ACT_AUDIO_STOP, out.actions);
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_PRE_MUTED, alarm_engine_view(&eng).state);

    for (int64_t t = MON_0700 - 299; t < MON_0700; t++) {          /* 정각 전까지 클립 없음 */
        out = alarm_engine_tick(&eng, t);
        TEST_ASSERT_EQUAL_UINT32(0, out.actions);
    }

    out = alarm_engine_tick(&eng, MON_0700);
    TEST_ASSERT_TRUE(out.actions & ALARM_ACT_MAIN_START);
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_RINGING, alarm_engine_view(&eng).state);
}

/* 울리는 중 SHORT → 스누즈 5분(snooze_until = 누른 시각 + 300초) */
void test_short_in_ringing_snoozes_5min(void)
{
    ring_at_0700(ALARM_DAYS_EVERYDAY);
    alarm_engine_tick(&eng, MON_0700 + 10);

    alarm_output_t out = alarm_engine_input(&eng, ALARM_INPUT_SHORT, MON_0700 + 10);

    TEST_ASSERT_EQUAL_UINT32(ALARM_ACT_AUDIO_STOP, out.actions);
    alarm_view_t v = alarm_engine_view(&eng);
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_SNOOZED, v.state);
    TEST_ASSERT_EQUAL_INT64(MON_0700 + 10 + 300, v.snooze_until);
}

/* 스누즈가 끝나면 미리 알림 없이 바로 본 알람 */
void test_snooze_returns_to_ringing_without_pre(void)
{
    ring_at_0700(ALARM_DAYS_EVERYDAY);
    alarm_engine_tick(&eng, MON_0700 + 10);
    alarm_engine_input(&eng, ALARM_INPUT_SHORT, MON_0700 + 10);

    alarm_output_t out = alarm_engine_tick(&eng, MON_0700 + 309);
    TEST_ASSERT_EQUAL_UINT32(0, out.actions);

    out = alarm_engine_tick(&eng, MON_0700 + 310);
    TEST_ASSERT_TRUE(out.actions & ALARM_ACT_MAIN_START);
    TEST_ASSERT_FALSE(out.actions & ALARM_ACT_PRE_CLIP);
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_RINGING, alarm_engine_view(&eng).state);
}

/* ===== 길게 누르기 ===== */

/* 울리는 중 LONG → 회차 끝, 확인음 */
void test_long_ends_session_with_beep(void)
{
    ring_at_0700(ALARM_DAYS_EVERYDAY);

    alarm_output_t out = alarm_engine_input(&eng, ALARM_INPUT_LONG, MON_0700 + 5);

    TEST_ASSERT_EQUAL_UINT32(ALARM_ACT_AUDIO_STOP | ALARM_ACT_CONFIRM_BEEP, out.actions);
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_IDLE, alarm_engine_view(&eng).state);
}

/* 미리 알림 중 LONG → 이번 회차는 정각에도 울리지 않고, 다음 날 같은 시각에는 다시 시작 */
void test_long_in_pre_cancels_occurrence(void)
{
    pre_at_0655(ALARM_DAYS_EVERYDAY);
    alarm_engine_input(&eng, ALARM_INPUT_LONG, MON_0700 - 290);
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_IDLE, alarm_engine_view(&eng).state);

    for (int64_t t = MON_0700 - 290; t <= MON_0700 + 10; t++) {
        alarm_output_t out = alarm_engine_tick(&eng, t);
        TEST_ASSERT_EQUAL_UINT32(0, out.actions);
    }

    alarm_engine_tick(&eng, MON_0700 + DAY - 301);
    alarm_output_t out = alarm_engine_tick(&eng, MON_0700 + DAY - 300);
    TEST_ASSERT_TRUE(out.actions & ALARM_ACT_PRE_CLIP);
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_PRE, alarm_engine_view(&eng).state);
}

/* ===== 자동 정지 ===== */

/* 10분 동안 계속 울리면 자동으로 멈춘다(확인음 없음) */
void test_auto_stop_after_10min_continuous(void)
{
    ring_at_0700(ALARM_DAYS_EVERYDAY);

    alarm_output_t out = alarm_engine_tick(&eng, MON_0700 + 599);
    TEST_ASSERT_EQUAL_UINT32(0, out.actions);
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_RINGING, alarm_engine_view(&eng).state);

    out = alarm_engine_tick(&eng, MON_0700 + 600);
    TEST_ASSERT_EQUAL_UINT32(ALARM_ACT_AUDIO_STOP, out.actions);
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_IDLE, alarm_engine_view(&eng).state);
}

/* 스누즈 뒤 다시 울리면 10분을 처음부터 다시 센다 */
void test_snooze_resets_auto_stop_timer(void)
{
    ring_at_0700(ALARM_DAYS_EVERYDAY);
    alarm_engine_tick(&eng, MON_0700 + 590);
    alarm_engine_input(&eng, ALARM_INPUT_SHORT, MON_0700 + 590);     /* 07:14:50까지 스누즈 */

    alarm_output_t out = alarm_engine_tick(&eng, MON_0700 + 890);
    TEST_ASSERT_TRUE(out.actions & ALARM_ACT_MAIN_START);

    out = alarm_engine_tick(&eng, MON_0700 + 890 + 599);
    TEST_ASSERT_EQUAL_UINT32(0, out.actions);
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_RINGING, alarm_engine_view(&eng).state);

    out = alarm_engine_tick(&eng, MON_0700 + 890 + 600);
    TEST_ASSERT_TRUE(out.actions & ALARM_ACT_AUDIO_STOP);
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_IDLE, alarm_engine_view(&eng).state);
}

/* ===== 한 번만 울리는 알람 ===== */

/* 한 번만(days 0) 알람은 회차가 끝나면 꺼달라고 알리고, 다음 날 울리지 않는다 */
void test_once_alarm_disabled_after_session(void)
{
    ring_at_0700(0);

    alarm_output_t out = alarm_engine_input(&eng, ALARM_INPUT_LONG, MON_0700 + 5);
    TEST_ASSERT_TRUE(out.actions & ALARM_ACT_DISABLE_ONCE);
    TEST_ASSERT_EQUAL_UINT8(1, out.n_disable);
    TEST_ASSERT_EQUAL_UINT8(1, out.disable_ids[0]);

    alarm_engine_tick(&eng, MON_0700 + DAY - 1);
    out = alarm_engine_tick(&eng, MON_0700 + DAY);
    TEST_ASSERT_EQUAL_UINT32(0, out.actions);
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_IDLE, alarm_engine_view(&eng).state);
}

/* 자동 정지로 끝나도 한 번만 알람은 꺼진다 */
void test_auto_stop_disables_once_alarm(void)
{
    ring_at_0700(0);

    alarm_output_t out = alarm_engine_tick(&eng, MON_0700 + 600);

    TEST_ASSERT_EQUAL_UINT32(ALARM_ACT_AUDIO_STOP | ALARM_ACT_DISABLE_ONCE, out.actions);
    TEST_ASSERT_EQUAL_UINT8(1, out.n_disable);
    TEST_ASSERT_EQUAL_UINT8(1, out.disable_ids[0]);
}

/* 반복 알람은 끝나도 꺼지지 않는다 */
void test_repeating_alarm_not_disabled(void)
{
    ring_at_0700(ALARM_DAYS_EVERYDAY);

    alarm_output_t out = alarm_engine_input(&eng, ALARM_INPUT_LONG, MON_0700 + 5);

    TEST_ASSERT_FALSE(out.actions & ALARM_ACT_DISABLE_ONCE);
    TEST_ASSERT_EQUAL_UINT8(0, out.n_disable);
}

/* ===== Review Focus ===== */

/* 4: 07:00과 07:02 알람(둘 다 미리 알림 5분, 한 번만)이 겹치면 하나의 회차로 합쳐지고 함께 끝난다 */
void test_overlapping_alarms_merge_and_end_together(void)
{
    alarm_t list[2] = { mk(1, 7, 0, 0, 5), mk(2, 7, 2, 0, 5) };
    alarm_engine_set_alarms(&eng, list, 2);
    alarm_engine_tick(&eng, MON_0700 - 301);
    alarm_output_t out = alarm_engine_tick(&eng, MON_0700 - 300);    /* 06:55 id 1 미리 알림 */
    TEST_ASSERT_TRUE(out.actions & ALARM_ACT_PRE_CLIP);

    for (int64_t t = MON_0700 - 299; t < MON_0700; t++) {           /* 06:57에 id 2가 합류해도 그대로 */
        out = alarm_engine_tick(&eng, t);
        TEST_ASSERT_TRUE(out.actions & ALARM_ACT_PRE_CLIP);
        TEST_ASSERT_FALSE(out.actions & ALARM_ACT_MAIN_START);
        TEST_ASSERT_EQUAL_UINT8(1, out.alarm_id);
    }
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_PRE, alarm_engine_view(&eng).state);

    out = alarm_engine_tick(&eng, MON_0700);
    TEST_ASSERT_TRUE(out.actions & ALARM_ACT_MAIN_START);
    TEST_ASSERT_EQUAL_UINT8(1, out.alarm_id);

    out = alarm_engine_input(&eng, ALARM_INPUT_LONG, MON_0700 + 5);
    TEST_ASSERT_TRUE(out.actions & ALARM_ACT_DISABLE_ONCE);
    TEST_ASSERT_EQUAL_UINT8(2, out.n_disable);
    TEST_ASSERT_EQUAL_UINT8(1, out.disable_ids[0]);
    TEST_ASSERT_EQUAL_UINT8(2, out.disable_ids[1]);

    for (int64_t t = MON_0700 + 6; t <= MON_0700 + 200; t++) {      /* 07:02를 지나도 조용 */
        out = alarm_engine_tick(&eng, t);
        TEST_ASSERT_EQUAL_UINT32(0, out.actions);
    }
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_IDLE, alarm_engine_view(&eng).state);
}

/* 5: 울리는 중에 알람 목록이 바뀌어도(여기서는 전부 삭제) 회차는 유지되고,
 *    삭제된 알람은 끝날 때 끄라고 알리지 않는다 */
void test_session_survives_alarm_list_change(void)
{
    ring_at_0700(0);

    alarm_engine_set_alarms(&eng, NULL, 0);
    alarm_engine_tick(&eng, MON_0700 + 1);
    alarm_view_t v = alarm_engine_view(&eng);
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_RINGING, v.state);
    TEST_ASSERT_EQUAL_UINT8(1, v.alarm_id);

    alarm_output_t out = alarm_engine_input(&eng, ALARM_INPUT_LONG, MON_0700 + 5);
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_IDLE, alarm_engine_view(&eng).state);
    TEST_ASSERT_FALSE(out.actions & ALARM_ACT_DISABLE_ONCE);
    TEST_ASSERT_EQUAL_UINT8(0, out.n_disable);
}

/* 같은 틱에 "정각 알람"과 "미리 알림 구간에 막 들어온 알람"이 함께 시작되면
 * 정각 알람이 대표가 되어 바로 울리고, 다른 하나는 합류한다 */
void test_simultaneous_start_prefers_ringing_alarm(void)
{
    alarm_t list[2] = { mk(1, 7, 5, ALARM_DAYS_EVERYDAY, 5), mk(2, 7, 0, ALARM_DAYS_EVERYDAY, 0) };
    alarm_engine_set_alarms(&eng, list, 2);
    alarm_engine_tick(&eng, MON_0700 - 1);

    alarm_output_t out = alarm_engine_tick(&eng, MON_0700);
    TEST_ASSERT_TRUE(out.actions & ALARM_ACT_MAIN_START);
    TEST_ASSERT_FALSE(out.actions & ALARM_ACT_PRE_CLIP);
    TEST_ASSERT_EQUAL_UINT8(2, out.alarm_id);
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_RINGING, alarm_engine_view(&eng).state);

    alarm_engine_input(&eng, ALARM_INPUT_LONG, MON_0700 + 5);         /* 둘 다 끝 */
    for (int64_t t = MON_0700 + 6; t <= MON_0700 + 310; t++) {        /* 07:05를 지나도 조용 */
        out = alarm_engine_tick(&eng, t);
        TEST_ASSERT_EQUAL_UINT32(0, out.actions);
    }
}

/* 아무 회차도 없을 때 버튼은 아무 일도 하지 않는다 */
void test_input_in_idle_does_nothing(void)
{
    set_one(mk(1, 7, 0, ALARM_DAYS_EVERYDAY, 5));
    alarm_engine_tick(&eng, MON_0700 - 1000);

    alarm_output_t out = alarm_engine_input(&eng, ALARM_INPUT_SHORT, MON_0700 - 1000);
    TEST_ASSERT_EQUAL_UINT32(0, out.actions);
    out = alarm_engine_input(&eng, ALARM_INPUT_LONG, MON_0700 - 1000);
    TEST_ASSERT_EQUAL_UINT32(0, out.actions);
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_IDLE, alarm_engine_view(&eng).state);
}

/* 설정 변경은 다음 스누즈부터 적용된다 */
void test_settings_change_applies_to_next_snooze(void)
{
    ring_at_0700(ALARM_DAYS_EVERYDAY);
    alarm_settings_t s = alarm_settings_default();
    s.snooze_min = 7;
    alarm_engine_set_settings(&eng, &s);

    alarm_engine_input(&eng, ALARM_INPUT_SHORT, MON_0700 + 10);
    TEST_ASSERT_EQUAL_INT64(MON_0700 + 10 + 420, alarm_engine_view(&eng).snooze_until);
}

/* 이미 진행 중인 스누즈는 바뀌지 않는다 */
void test_settings_change_keeps_current_snooze(void)
{
    ring_at_0700(ALARM_DAYS_EVERYDAY);
    alarm_engine_input(&eng, ALARM_INPUT_SHORT, MON_0700 + 10);
    alarm_settings_t s = alarm_settings_default();
    s.snooze_min = 7;
    alarm_engine_set_settings(&eng, &s);

    alarm_view_t v = alarm_engine_view(&eng);
    TEST_ASSERT_EQUAL_INT(ALARM_STATE_SNOOZED, v.state);
    TEST_ASSERT_EQUAL_INT64(MON_0700 + 10 + 300, v.snooze_until);
}

int main(void)
{
    UNITY_BEGIN();

    /* 짧게 누르기 */
    RUN_TEST(test_short_in_pre_mutes_but_rings_on_time);
    RUN_TEST(test_short_in_ringing_snoozes_5min);
    RUN_TEST(test_snooze_returns_to_ringing_without_pre);

    /* 길게 누르기 */
    RUN_TEST(test_long_ends_session_with_beep);
    RUN_TEST(test_long_in_pre_cancels_occurrence);

    /* 자동 정지 */
    RUN_TEST(test_auto_stop_after_10min_continuous);
    RUN_TEST(test_snooze_resets_auto_stop_timer);

    /* 한 번만 울리는 알람 */
    RUN_TEST(test_once_alarm_disabled_after_session);
    RUN_TEST(test_auto_stop_disables_once_alarm);
    RUN_TEST(test_repeating_alarm_not_disabled);

    /* Review Focus */
    RUN_TEST(test_overlapping_alarms_merge_and_end_together);
    RUN_TEST(test_session_survives_alarm_list_change);

    RUN_TEST(test_simultaneous_start_prefers_ringing_alarm);
    RUN_TEST(test_input_in_idle_does_nothing);
    RUN_TEST(test_settings_change_applies_to_next_snooze);
    RUN_TEST(test_settings_change_keeps_current_snooze);

    return UNITY_END();
}
