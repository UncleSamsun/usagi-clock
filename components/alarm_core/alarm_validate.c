#include "alarm_core.h"

/*
 * 알람 값이 허용 범위 안인지 확인한다. 하나라도 벗어나면 false.
 *
 * 단계 (항목마다 if 하나, 벗어나면 바로 return false)
 *   1) hour         0..23  → 23보다 크면 false
 *   2) minute       0..59
 *   3) pre_minutes  0..ALARM_PRE_MINUTES_MAX(60)
 *   4) pre_volume   0..ALARM_VOLUME_MAX(100)
 *   5) main_volume  0..ALARM_VOLUME_MAX(100)
 *   6) days_mask    bit0..bit6만 쓸 수 있다. ALARM_DAYS_EVERYDAY(0x7F) 밖의 비트가 켜져 있으면 false
 *        밖의 비트 확인: (a->days_mask & ~ALARM_DAYS_EVERYDAY) != 0
 *   7) 모두 통과하면 true
 *
 * 숫자(23, 60, 100)를 직접 쓰기보다 헤더 상수를 쓰는 게 좋다. 범위가 바뀌면 헤더만 고치면 된다.
 * uint8_t는 음수가 없으니 "0 이상"은 검사하지 않아도 된다.
 *
 * 테스트: test_validate_rejects
 */
bool alarm_validate(const alarm_t *a)
{
    if (a->hour > 23)
    {
        return false;
    }

    if (a->minute > 59)
    {
        return false;
    }

    if (a->pre_minutes > ALARM_PRE_MINUTES_MAX)
    {
        return false;
    }

    if (a->pre_volume > ALARM_VOLUME_MAX)
    {
        return false;
    }

    if (a->main_volume > ALARM_VOLUME_MAX)
    {
        return false;
    }

    if ((a->days_mask & ~ALARM_DAYS_EVERYDAY) != 0)
    {
        return false;
    }

    return true;
}

/*
 * 기본 설정값을 담은 구조체를 돌려준다.
 *
 * 단계
 *   1) alarm_settings_t s; 를 만든다
 *   2) s.snooze_min, s.long_press_ms, s.auto_stop_min에 헤더의 ALARM_DEFAULT_* 상수를 넣는다
 *   3) return s;   ← 구조체는 값으로 통째로 돌려줄 수 있다(복사되어 나간다)
 *
 * 테스트: test_settings_default
 */
alarm_settings_t alarm_settings_default(void)
{
    alarm_settings_t s = {0};
    s.snooze_min = ALARM_DEFAULT_SNOOZE_MIN;
    s.long_press_ms = ALARM_DEFAULT_LONG_PRESS_MS;
    s.auto_stop_min = ALARM_DEFAULT_AUTO_STOP_MIN;

    return s;
}
