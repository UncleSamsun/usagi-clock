#ifndef BUTTON_FSM_H
#define BUTTON_FSM_H

/*
 * 버튼 판별 상태 머신(디바운스, 짧게/길게 누르기).
 *
 * GPIO를 직접 읽지 않는다. 호출하는 쪽(계획 2의 main)이 10ms마다 원시 입력과 현재 시각(ms)을 넣어 준다.
 * alarm_core에 의존하지 않는다. main이 BUTTON_EV_SHORT/LONG을 ALARM_INPUT_SHORT/LONG으로 바꿔 넘긴다.
 *
 * 판별 규칙
 *   - 원시 입력이 debounce_ms 동안 같은 값을 유지해야 상태(눌림/뗌)가 확정된다.
 *   - 눌림이 확정된 뒤 long_ms가 지나는 순간, 손을 떼기 전에 LONG을 한 번만 낸다.
 *   - LONG 없이 떼면 SHORT, LONG 뒤에 떼면 아무것도 내지 않는다.
 *   - 처음부터 눌린 채 시작하면(버튼을 누른 채 부팅) 한 번 뗄 때까지 아무 이벤트도 내지 않는다.
 *   - now_ms는 uint32_t가 넘쳐 0으로 돌아가도 동작한다(항상 차이로 계산).
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 기본 디바운스 시간(ms) */
#define BUTTON_DEBOUNCE_MS 30

typedef enum { BUTTON_EV_NONE = 0, BUTTON_EV_SHORT, BUTTON_EV_LONG } button_event_t;

/* 내부 상태. 필드를 직접 건드리지 말고 함수로만 다룬다. */
typedef struct {
    uint32_t debounce_ms;
    uint32_t long_ms;
    bool     started;      /* 첫 update를 받았는지 */
    bool     raw_last;     /* 직전 원시 입력 */
    uint32_t raw_since;    /* raw_last 값이 시작된 시각 */
    bool     pressed;      /* 디바운스로 확정된 상태 */
    uint32_t pressed_at;   /* 눌림이 확정된 시각 */
    bool     long_sent;    /* 이번 누름에서 LONG을 이미 냈는지 */
    bool     armed;        /* 뗀 상태를 한 번 확인했는지(false면 이벤트를 내지 않는다) */
} button_fsm_t;

void           button_fsm_init(button_fsm_t *b, uint32_t debounce_ms, uint32_t long_ms);
button_event_t button_fsm_update(button_fsm_t *b, bool raw_pressed, uint32_t now_ms); /* 10ms마다 호출 */

#ifdef ESP_PLATFORM
#include "esp_err.h"

typedef void (*button_cb_t)(button_event_t ev, void *ctx);

/* GPIO를 입력+내부 풀업으로 두고 10ms 주기 작업에서 button_fsm_update를 부른다. LOW = 눌림.
 * 인터럽트 대신 폴링: button_fsm이 10ms 샘플을 전제로 하고, 스위치 하나라 부담이 없다 */
esp_err_t button_gpio_start(int gpio, uint32_t long_ms, button_cb_t cb, void *ctx);
/* 길게 누르기 기준을 바꾼다(다음 누름부터) */
void      button_gpio_set_long_ms(uint32_t long_ms);
/* 지금 눌려 있는지(디바운스 전 원시 값). 부팅 때 설정 모드 판단용 */
bool      button_gpio_raw_pressed(int gpio);
#endif

#ifdef __cplusplus
}
#endif

#endif /* BUTTON_FSM_H */
