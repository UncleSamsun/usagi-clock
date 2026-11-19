#include "unity.h"

#include "alarm_core.h"

/* 테스트 상수 */
#define MON_1116_0000 1794787200LL /* 2026-11-16(월) 00:00 로컬 epoch */
#define MON_0600 1794808800LL      /* 2026-11-16(월) 06:00 */
#define MON_0700 1794812400LL      /* 2026-11-16(월) 07:00 */
#define TUE_1117_0700 1794898800LL /* 2026-11-17(화) 07:00 */
#define FRI_1120_0800 1795161600LL /* 2026-11-20(금) 08:00 */
#define MON_1123_0700 1795417200LL /* 2026-11-23(월) 07:00 */

/* 테스트용 알람을 만드는 도우미: 켜져 있고 id=1, 나머지는 0 */
static alarm_t make_alarm(uint8_t hour, uint8_t minute, uint8_t days_mask)
{
    alarm_t a = {0};          /* 구조체 전체를 0으로 초기화 */
    a.id = 1;
    a.enabled = true;
    a.hour = hour;
    a.minute = minute;
    a.days_mask = days_mask;
    return a;
}

void setUp(void) {}
void tearDown(void) {}
void test_weekday_epoch_is_thursday(void)
{
    TEST_ASSERT_EQUAL_INT(3, alarm_weekday(0)); 
}
void test_weekday_2026_11_16_is_monday(void)
{
    TEST_ASSERT_EQUAL_INT(0, alarm_weekday(MON_1116_0000));
}
void test_local_from_utc_adds_9h(void)
{
    TEST_ASSERT_EQUAL_INT64(MON_0700, alarm_local_from_utc(MON_0700 - 9*3600));
}
/* 매일 07:00 알람: 월 06:00 이후 첫 울림 → 같은 날 월 07:00 */
void test_daily_same_day(void)              
{
    /* 준비 */
    alarm_t a = make_alarm(7, 0, ALARM_DAYS_EVERYDAY);

    /* 실행 */
    int64_t next = alarm_next_ring(&a, MON_0600);

    /* 확인 */
    TEST_ASSERT_EQUAL_INT64(MON_0700, next);
}
/* 매일 07:00 알람: 기준이 정확히 월 07:00이면 그 시각은 제외(초과만) → 화 07:00 */
void test_strictly_after(void)
{
    alarm_t a = make_alarm(7, 0, ALARM_DAYS_EVERYDAY);

    int64_t next = alarm_next_ring(&a, MON_0700);

    TEST_ASSERT_EQUAL_INT64(TUE_1117_0700, next);
}
/* 평일 07:00 알람: 금 08:00 이후 → 토·일을 건너뛰고 다음 주 월(11/23) 07:00 */
void test_weekdays_skip_weekend(void)
{
    alarm_t a = make_alarm(7, 0, ALARM_DAYS_WEEKDAYS);

    int64_t next = alarm_next_ring(&a, FRI_1120_0800);

    TEST_ASSERT_EQUAL_INT64(MON_1123_0700, next);
}
/* 한 번만 알람(days_mask 0) 07:00: 월 08:00(MON_0700 + 3600) 이후 → 다음 날 화 07:00 */
void test_once_rolls_to_tomorrow(void)
{
    alarm_t a = make_alarm(7, 0, 0);

    int64_t next = alarm_next_ring(&a, MON_0700 + 3600);

    TEST_ASSERT_EQUAL_INT64(TUE_1117_0700, next);
}
/* 월요일만 07:00: 월 08:00 이후 → 화~일을 건너뛰고 7일 뒤 다음 주 월 07:00 */
void test_single_day_wraps_to_next_week(void)
{
    alarm_t a = make_alarm(7, 0, ALARM_DAY_MON);

    int64_t next = alarm_next_ring(&a, MON_0700 + 3600);

    TEST_ASSERT_EQUAL_INT64(MON_1123_0700, next);
}
/* 꺼진 알람(enabled = false)은 -1을 반환 */
void test_disabled_returns_minus1(void)
{
    alarm_t a = make_alarm(7, 0, ALARM_DAYS_EVERYDAY);
    a.enabled = false;  

    int64_t next = alarm_next_ring(&a, MON_0600);

    TEST_ASSERT_EQUAL_INT64(-1, next);
}
/* 시 24, 분 60, 미리 알림 61분, 볼륨 101, days_mask 0x80(bit7)은 각각 false */
void test_validate_rejects(void)
{
    alarm_t ok = make_alarm(7, 0, ALARM_DAYS_EVERYDAY);
    TEST_ASSERT_TRUE(alarm_validate(&ok));

    alarm_t a;

    a = ok;
    a.hour = 24;
    TEST_ASSERT_FALSE(alarm_validate(&a));

    a = ok;
    a.minute = 60;
    TEST_ASSERT_FALSE(alarm_validate(&a));

    a = ok;
    a.pre_minutes = 61;
    TEST_ASSERT_FALSE(alarm_validate(&a));

    a = ok;
    a.pre_volume = 101;
    TEST_ASSERT_FALSE(alarm_validate(&a));

    a = ok;
    a.main_volume = 101;
    TEST_ASSERT_FALSE(alarm_validate(&a));

    a = ok;
    a.days_mask = 0x80;
    TEST_ASSERT_FALSE(alarm_validate(&a));

    /* 경계값: 각 항목의 최댓값은 허용해야 한다 */
    a = ok;
    a.hour = 23;
    a.minute = 59;
    a.pre_minutes = ALARM_PRE_MINUTES_MAX;
    a.pre_volume = ALARM_VOLUME_MAX;
    a.main_volume = ALARM_VOLUME_MAX;
    TEST_ASSERT_TRUE(alarm_validate(&a));
}
/* 기본 설정값: 스누즈 5분, 길게 누르기 2000ms, 자동 정지 10분 */
void test_settings_default(void)
{
    alarm_settings_t s = alarm_settings_default();

    TEST_ASSERT_EQUAL_UINT16(5, s.snooze_min);
    TEST_ASSERT_EQUAL_UINT16(2000, s.long_press_ms);
    TEST_ASSERT_EQUAL_UINT16(10, s.auto_stop_min);
}
/* 1970-01-01 00:00 바로 1초 전은 1969-12-31(수): 음수 나눗셈 처리 확인 */
void test_weekday_before_epoch(void)
{
    TEST_ASSERT_EQUAL_INT64(2, alarm_weekday(-1));
}

/* 스모크 테스트(Task 1): 빌드와 테스트 실행 경로가 살아 있는지만 확인한다. */
static void test_smoke(void)
{
    TEST_ASSERT_EQUAL_INT(9 * 3600, ALARM_TZ_OFFSET_S);
}

/* ===== 날짜 ↔ epoch (계획 2 Task 2) ===== */

void test_civil_from_epoch_monday_0700(void)
{
    alarm_civil_t c;
    alarm_civil_from_epoch(MON_0700, &c);
    TEST_ASSERT_EQUAL_INT(2026, c.year);
    TEST_ASSERT_EQUAL_UINT8(11, c.month);
    TEST_ASSERT_EQUAL_UINT8(16, c.day);
    TEST_ASSERT_EQUAL_UINT8(7, c.hour);
    TEST_ASSERT_EQUAL_UINT8(0, c.minute);
    TEST_ASSERT_EQUAL_UINT8(0, c.second);
    TEST_ASSERT_EQUAL_UINT8(0, c.weekday);   /* 월 */
}

void test_epoch_from_civil_leap_day(void)
{
    alarm_civil_t c = { .year = 2028, .month = 2, .day = 29, .hour = 23, .minute = 59, .second = 59 };
    TEST_ASSERT_EQUAL_INT64(1835481599LL, alarm_epoch_from_civil(&c));
}

void test_civil_epoch_zero(void)
{
    alarm_civil_t c;
    alarm_civil_from_epoch(0, &c);
    TEST_ASSERT_EQUAL_INT(1970, c.year);
    TEST_ASSERT_EQUAL_UINT8(1, c.month);
    TEST_ASSERT_EQUAL_UINT8(1, c.day);
    TEST_ASSERT_EQUAL_UINT8(3, c.weekday);   /* 목 */
    TEST_ASSERT_EQUAL_INT64(0, alarm_epoch_from_civil(&c));
}

/* 2026-01-01부터 2099-12-31 23:59:59까지 86399초 간격으로 왕복이 같다 */
void test_civil_round_trip_range(void)
{
    for (int64_t s = 1767225600LL; s <= 4102444799LL; s += 86399) {
        alarm_civil_t c;
        alarm_civil_from_epoch(s, &c);
        TEST_ASSERT_EQUAL_INT64(s, alarm_epoch_from_civil(&c));
        TEST_ASSERT_EQUAL_INT(alarm_weekday(s), c.weekday);
    }
}

/* 여러 알람 중 가장 이른 다음 울림(화면의 NEXT 표시용) */
void test_next_any_picks_earliest(void)
{
    alarm_t list[3] = { make_alarm(8, 0, ALARM_DAYS_EVERYDAY), make_alarm(7, 0, ALARM_DAYS_EVERYDAY),
                        make_alarm(6, 0, ALARM_DAYS_EVERYDAY) };
    list[2].enabled = false;   /* 꺼진 6시는 무시 */
    size_t idx = 99;
    TEST_ASSERT_EQUAL_INT64(MON_0700, alarm_next_any(list, 3, MON_0600, &idx));
    TEST_ASSERT_EQUAL_size_t(1, idx);
}

void test_next_any_none(void)
{
    alarm_t a = make_alarm(7, 0, 0);
    a.enabled = false;
    size_t idx = 99;
    TEST_ASSERT_EQUAL_INT64(-1, alarm_next_any(&a, 1, MON_0600, &idx));
    TEST_ASSERT_EQUAL_INT64(-1, alarm_next_any(NULL, 0, MON_0600, NULL));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_smoke);

    /* 요일과 시간대 */
    RUN_TEST(test_weekday_epoch_is_thursday);
    RUN_TEST(test_weekday_2026_11_16_is_monday);
    RUN_TEST(test_weekday_before_epoch);
    RUN_TEST(test_local_from_utc_adds_9h);

    /* 다음 울림 시각 */
    RUN_TEST(test_daily_same_day);
    RUN_TEST(test_strictly_after);
    RUN_TEST(test_weekdays_skip_weekend);
    RUN_TEST(test_once_rolls_to_tomorrow);
    RUN_TEST(test_single_day_wraps_to_next_week);
    RUN_TEST(test_disabled_returns_minus1);

    /* 값 검증과 기본 설정 */
    RUN_TEST(test_validate_rejects);
    RUN_TEST(test_settings_default);

    /* 날짜 ↔ epoch */
    RUN_TEST(test_civil_from_epoch_monday_0700);
    RUN_TEST(test_epoch_from_civil_leap_day);
    RUN_TEST(test_civil_epoch_zero);
    RUN_TEST(test_civil_round_trip_range);
    RUN_TEST(test_next_any_picks_earliest);
    RUN_TEST(test_next_any_none);

    return UNITY_END();
}
