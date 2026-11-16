#ifndef BUTTON_FSM_H
#define BUTTON_FSM_H

/*
 * 버튼 판별 상태 머신(디바운스, 짧게/길게 누르기).
 * 타입과 함수는 계획 1의 Task 5에서 채운다.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 기본 디바운스 시간(ms) */
#define BUTTON_DEBOUNCE_MS 30

#ifdef __cplusplus
}
#endif

#endif /* BUTTON_FSM_H */
