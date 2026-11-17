#include "alarm_core.h"

/* 하루의 초. LL을 붙여 int64_t 계산에서 넘침이 없게 한다. */
#define SECONDS_PER_DAY 86400LL

/*
 * UTC epoch 초 → 로컬(KST) epoch 초
 *
 * 할 일: utc_s에 시간대 차이(ALARM_TZ_OFFSET_S)를 더해서 돌려준다.
 * 테스트: test_local_from_utc_adds_9h
 */
int64_t alarm_local_from_utc(int64_t utc_s)
{
    return utc_s + ALARM_TZ_OFFSET_S;
}

/*
 * 로컬 epoch 초 → 요일 (0=월 .. 6=일)
 *
 * 단계
 *   1) 1970-01-01 00:00부터 며칠 지났는지 구한다: days = local_s / SECONDS_PER_DAY
 *   2) 음수 보정: C의 '/'는 0 쪽으로 버린다. 그래서 -1 / 86400 은 0이 된다.
 *      그런데 -1초는 "그 전날"(1969-12-31)이니 days가 -1이어야 맞다.
 *      → local_s가 음수이고 하루로 나누어떨어지지 않으면 days에서 1을 뺀다.
 *        (나누어떨어지는지: local_s % SECONDS_PER_DAY != 0)
 *   3) 1970-01-01은 목요일(=3)이다. days일 뒤의 요일은 (days + 3)을 7로 나눈 나머지.
 *   4) '%'도 음수가 나올 수 있다(-5 % 7 == -5). 나머지가 음수면 7을 더한다.
 *
 * 손으로 확인해 볼 값
 *   local_s = 0              → days 0      → (0 + 3) % 7 = 3 (목)
 *   local_s = -1             → days -1     → (-1 + 3) % 7 = 2 (수)
 *   MON_1116_0000(1794787200) → days 20773 → (20773 + 3) % 7 = ?
 *
 * 테스트: test_weekday_epoch_is_thursday, test_weekday_2026_11_16_is_monday,
 *         test_weekday_before_epoch
 *
 * 팁: 1~2단계("floor로 날짜 수 구하기")는 alarm_next_ring에서도 필요하다.
 *     static int64_t days_floor(int64_t local_s) 같은 작은 함수로 빼면 두 곳에서 같이 쓸 수 있다.
 *     static이 붙은 함수는 이 .c 파일 안에서만 보인다(헤더에 선언하지 않는다).
 */
/* 1970-01-01 00:00(로컬)부터 지난 날짜 수. 음수 시각도 "그 전날"로 내림(floor)한다. */
static int64_t days_floor(int64_t local_s)
{
    int64_t days = local_s / SECONDS_PER_DAY;

    if (local_s < 0 && local_s % SECONDS_PER_DAY != 0)
    {
        days -= 1;
    }

    return days;
}

int alarm_weekday(int64_t local_s)
{
    int64_t days = days_floor(local_s);

    int week = (int)((days + 3) % 7);

    if (week < 0)
    {
        week += 7;
    }

    return week;
}

/*
 * after_local_s보다 "엄격하게 큰" 가장 이른 울림 시각을 돌려준다. 울릴 수 없으면 -1.
 *
 * 아이디어: after_local_s가 속한 날의 자정부터 하루씩 넘기며(0일 뒤 ~ 7일 뒤, 8번)
 *          "그날 hour:minute"이 조건에 맞는지 본다. 처음 맞는 날이 답이다.
 *
 * 단계
 *   1) 꺼진 알람(a->enabled가 false)이면 바로 -1.
 *   2) after_local_s가 속한 날의 자정(00:00) 시각을 구한다.
 *        자정 = 날짜 수(floor) * SECONDS_PER_DAY
 *      음수 시각에서도 맞으려면 weekday와 같은 floor 처리가 필요하다(위의 팁 참고).
 *   3) for (int i = 0; i <= 7; i++) 안에서:
 *        cand = 자정 + i * SECONDS_PER_DAY + a->hour * 3600 + a->minute * 60
 *        a) cand <= after_local_s 이면 건너뛴다(continue). "같은 시각"도 제외한다.
 *        b) a->days_mask == 0(한 번만)이면 cand가 바로 답이다.
 *        c) 아니면 cand의 요일 wd = alarm_weekday(cand)를 구하고,
 *           days_mask에서 wd번 비트가 켜져 있으면 cand가 답이다.
 *             비트 확인: (a->days_mask & (1u << wd)) != 0
 *   4) 8번을 다 돌아도 없으면 -1.
 *
 * 왜 8번(0..7)? 오늘 시각이 이미 지났고 매주 오늘 요일에만 울리는 알람이면
 *              답은 정확히 7일 뒤다. 0일 뒤(오늘)부터 세면 8번이 필요하다.
 *
 * 손으로 따라가 보기: test_weekdays_skip_weekend (평일 07:00, 기준 금 08:00)
 *   i=0  금 07:00 → 08:00보다 이르다 → 건너뜀
 *   i=1  토 07:00 → 토요일 비트(bit5) 꺼짐 → 다음
 *   i=2  일 07:00 → 일요일 비트(bit6) 꺼짐 → 다음
 *   i=3  월 07:00 → 월요일 비트(bit0) 켜짐 → 답(MON_1123_0700)
 *
 * 테스트: test_daily_same_day, test_strictly_after, test_weekdays_skip_weekend,
 *         test_once_rolls_to_tomorrow, test_disabled_returns_minus1
 */
int64_t alarm_next_ring(const alarm_t *a, int64_t after_local_s)
{
    if (!a->enabled)
    {
        return -1;
    }

    int64_t midnight = days_floor(after_local_s) * SECONDS_PER_DAY;

    for (int i = 0; i <= 7; i++)
    {
        int64_t cand = midnight + i * SECONDS_PER_DAY + a->hour * 3600 + a->minute * 60;

        if (cand <= after_local_s)
        {
            continue;
        }

        if (a->days_mask == 0)
        {
            return cand;
        }
        
        int wd = alarm_weekday(cand);

        if ((a->days_mask & (1u << wd)) != 0)
        {
            return cand;
        }
    }
    
    return -1;
}
