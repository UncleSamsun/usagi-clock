#ifndef ALARM_CORE_H
#define ALARM_CORE_H

/*
 * 알람 판단 로직의 공개 인터페이스.
 *
 * 시간은 모두 "로컬 epoch 초"(int64_t)로 다룬다.
 * 로컬 epoch 초 = UTC epoch 초 + ALARM_TZ_OFFSET_S (KST 고정, 서머타임 없음)
 *
 * 타입과 함수는 계획 1의 Task 2~4에서 채운다.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 시간대: KST(UTC+9) 고정 */
#define ALARM_TZ_OFFSET_S (9 * 3600)

/* 저장할 수 있는 알람 최대 개수 */
#define ALARM_MAX 32

/* 이보다 크게 앞으로 건너뛴 시간 사이의 알람은 울리지 않는다(초) */
#define ALARM_JUMP_LIMIT_S 120

/* 기본 설정값 */
#define ALARM_DEFAULT_SNOOZE_MIN 5
#define ALARM_DEFAULT_LONG_PRESS_MS 2000
#define ALARM_DEFAULT_AUTO_STOP_MIN 10
#define ALARM_DEFAULT_PRE_MINUTES 5

/* 값 범위 */
#define ALARM_PRE_MINUTES_MAX 60
#define ALARM_VOLUME_MAX 100

/* days_mask: bit0=월 ... bit6=일, 0 = 한 번만 */
#define ALARM_DAY_MON  (1u << 0)
#define ALARM_DAY_TUE  (1u << 1)
#define ALARM_DAY_WED  (1u << 2)
#define ALARM_DAY_THU  (1u << 3)
#define ALARM_DAY_FRI  (1u << 4)
#define ALARM_DAY_SAT  (1u << 5)
#define ALARM_DAY_SUN  (1u << 6)
#define ALARM_DAYS_WEEKDAYS 0x1Fu
#define ALARM_DAYS_EVERYDAY 0x7Fu

typedef struct {
    uint8_t id;            /* 1..255, 0은 미사용 */
    bool    enabled;
    uint8_t hour;          /* 0..23 (UI가 12시간제로 변환) */
    uint8_t minute;        /* 0..59 */
    uint8_t days_mask;
    uint8_t pre_minutes;   /* 0..60 */
    uint8_t pre_volume;    /* 0..100 */
    uint8_t main_volume;   /* 0..100 */
    char    label[24];
    char    clipset[32];   /* SD /clips/ 아래 세트 이름 */
    char    song[64];      /* SD /sounds/ 아래 파일 이름 */
} alarm_t;

typedef struct {
    uint16_t snooze_min;     /* 기본 5 */
    uint16_t long_press_ms;  /* 기본 2000 */
    uint16_t auto_stop_min;  /* 기본 10 */
} alarm_settings_t;

int64_t alarm_local_from_utc(int64_t utc_s);
int     alarm_weekday(int64_t local_s);            /* 0=월 .. 6=일 */
int64_t alarm_next_ring(const alarm_t *a, int64_t after_local_s); /* after보다 큰 가장 이른 울림 시각, 비활성이면 -1 */
bool    alarm_validate(const alarm_t *a);
alarm_settings_t alarm_settings_default(void);

#ifdef __cplusplus
}
#endif

#endif /* ALARM_CORE_H */
