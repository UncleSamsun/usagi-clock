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

#ifdef __cplusplus
}
#endif

#endif /* ALARM_CORE_H */
