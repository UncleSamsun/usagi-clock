#include "button_fsm.h"

#include <string.h> /* memset */

void button_fsm_init(button_fsm_t *b, uint32_t debounce_ms, uint32_t long_ms)
{
    memset(b, 0, sizeof(*b));
    b->debounce_ms = debounce_ms;
    b->long_ms = long_ms;
}

/*
 * 시각 비교는 항상 (uint32_t)(now - 과거시각)으로 한다.
 * uint32_t 뺄셈은 넘침이 있어도 "흐른 시간"을 맞게 돌려준다. 예: 0x00000010 - 0xFFFFFFF0 = 0x20(32ms)
 * 반대로 now >= 과거시각 + long_ms 처럼 더해서 비교하면, 더한 값이 넘쳐 0 근처가 되면서 틀린다.
 */
button_event_t button_fsm_update(button_fsm_t *b, bool raw_pressed, uint32_t now_ms)
{
    /* 첫 입력: 지금 상태를 그대로 확정 상태로 삼는다.
     * 눌린 채 시작했다면 armed = false로 두어, 한 번 뗄 때까지 이벤트를 막는다. */
    if (!b->started) {
        b->started = true;
        b->raw_last = raw_pressed;
        b->raw_since = now_ms;
        b->pressed = raw_pressed;
        b->pressed_at = now_ms;
        b->long_sent = false;
        b->armed = !raw_pressed;
        return BUTTON_EV_NONE;
    }

    /* 원시 입력이 바뀌면 "같은 값 유지" 시간을 처음부터 다시 센다 */
    if (raw_pressed != b->raw_last) {
        b->raw_last = raw_pressed;
        b->raw_since = now_ms;
    }

    button_event_t ev = BUTTON_EV_NONE;

    /* 디바운스: 확정 상태와 다른 값이 debounce_ms 동안 유지되면 상태를 바꾼다 */
    if (b->raw_last != b->pressed && (uint32_t)(now_ms - b->raw_since) >= b->debounce_ms) {
        b->pressed = b->raw_last;
        if (b->pressed) {
            b->pressed_at = now_ms;
            b->long_sent = false;
        } else {
            if (b->armed && !b->long_sent) {
                ev = BUTTON_EV_SHORT;
            }
            b->armed = true;
        }
    }

    /* 길게: 눌림 확정 후 long_ms가 지나는 순간 한 번.
     * 원시 입력이 이미 떨어져 있으면(뗌 확정을 기다리는 30ms 사이) 내지 않는다.
     * 진짜 뗀 것이면 곧 SHORT가 되고, 잠깐 끊긴 것이면 다시 눌린 틱에 LONG이 나온다. */
    if (b->pressed && b->raw_last && b->armed && !b->long_sent &&
        (uint32_t)(now_ms - b->pressed_at) >= b->long_ms) {
        b->long_sent = true;
        ev = BUTTON_EV_LONG;
    }

    return ev;
}
