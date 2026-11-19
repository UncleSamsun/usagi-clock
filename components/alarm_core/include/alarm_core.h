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

/* ===== 알람 엔진(Task 3~4) =====
 * 1초마다 tick()을 부르면 "지금 무엇을 해야 하는지"(actions)를 돌려준다.
 * 소리 재생, 저장 같은 실제 동작은 엔진 밖(계획 2~4의 펌웨어)이 actions를 보고 한다.
 */

typedef enum {
    ALARM_STATE_IDLE = 0,
    ALARM_STATE_PRE,         /* "우! 나!" 재생 중 */
    ALARM_STATE_PRE_MUTED,   /* 미리 알림 음소거, 정각 대기 */
    ALARM_STATE_RINGING,     /* 본 알람 */
    ALARM_STATE_SNOOZED,
} alarm_state_t;

/* 출력 동작 비트: 한 번에 여러 개가 켜질 수 있다 */
#define ALARM_ACT_PRE_CLIP     (1u << 0)  /* 이번 틱에 미리 알림 클립 1개 재생 (pre_clip_seq 번째) */
#define ALARM_ACT_MAIN_START   (1u << 1)  /* 본 알람 노래 재생 시작(반복) */
#define ALARM_ACT_AUDIO_STOP   (1u << 2)
#define ALARM_ACT_CONFIRM_BEEP (1u << 3)  /* 길게 누르기 확인음 */
#define ALARM_ACT_DISABLE_ONCE (1u << 4)  /* disable_ids 의 알람을 비활성화해 저장할 것 */

typedef struct {
    uint32_t actions;
    uint8_t  alarm_id;                   /* 회차의 대표 알람(노래·볼륨 조회용) */
    uint16_t pre_clip_seq;               /* 0부터 증가 */
    uint8_t  disable_ids[ALARM_MAX];
    uint8_t  n_disable;
} alarm_output_t;

typedef struct {
    alarm_state_t state;
    uint8_t  alarm_id;
    int64_t  ring_at;        /* 대표 알람의 정각(로컬 epoch) */
    int64_t  snooze_until;   /* SNOOZED일 때만 */
} alarm_view_t;

typedef enum { ALARM_INPUT_SHORT = 1, ALARM_INPUT_LONG = 2 } alarm_input_t;

/* 엔진 내부 상태. 밖에서는 필드를 직접 건드리지 말고 함수로만 다룬다.
 * 동적 할당(malloc) 없이 고정 크기 배열만 쓴다. 필드는 구현하면서 바꿔도 된다. */
typedef struct {
    alarm_settings_t settings;
    alarm_t  alarms[ALARM_MAX];          /* set_alarms로 받은 목록의 사본 */
    int64_t  handled_ring_at[ALARM_MAX]; /* 알람별로 이미 시작한 회차의 정각. 없으면 -1 */
    size_t   n_alarms;
    bool     has_last_tick;              /* false면 아직 첫 틱 전 */
    int64_t  last_tick;                  /* 직전 틱의 now */
    alarm_state_t state;
    uint8_t  alarm_id;                   /* 현재 회차의 대표 알람 id */
    int64_t  ring_at;                    /* 현재 회차의 정각 */
    int64_t  snooze_until;
    uint16_t pre_clip_seq;               /* 다음에 낼 클립 번호 */
} alarm_engine_t;

void           alarm_engine_init(alarm_engine_t *e, const alarm_settings_t *s);
void           alarm_engine_set_alarms(alarm_engine_t *e, const alarm_t *alarms, size_t n);
alarm_output_t alarm_engine_tick(alarm_engine_t *e, int64_t now_local_s);   /* 1초마다 호출 */
alarm_output_t alarm_engine_input(alarm_engine_t *e, alarm_input_t in, int64_t now_local_s);
alarm_view_t   alarm_engine_view(const alarm_engine_t *e);

#ifdef __cplusplus
}
#endif

#endif /* ALARM_CORE_H */
