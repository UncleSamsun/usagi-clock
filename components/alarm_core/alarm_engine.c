#include "alarm_core.h"

#include <string.h> /* memset, memcpy */

/*
 * 엔진을 처음 상태로 만든다.
 *
 * 단계
 *   1) 구조체 전체를 0으로 지운다: memset(e, 0, sizeof(*e));
 *      → state는 IDLE(=0), n_alarms 0, has_last_tick false가 된다
 *   2) 설정을 복사한다: e->settings = *s;   (s는 포인터라서 *s가 구조체 자체)
 *   3) handled_ring_at은 0이 아니라 "없음"(-1)으로 채운다(ALARM_MAX개 모두)
 */
void alarm_engine_init(alarm_engine_t *e, const alarm_settings_t *s)
{
    memset(e, 0, sizeof(*e));

    e->settings = *s;

    for (int i = 0; i < ALARM_MAX; i++) {
        e->handled_ring_at[i] = -1;
    }
}

/*
 * 알람 목록을 엔진에 넣는다(사본을 만든다). 웹에서 알람을 바꿀 때마다 불린다.
 *
 * 단계
 *   1) n이 ALARM_MAX보다 크면 ALARM_MAX로 자른다
 *   2) 새 목록의 각 알람 i에 대해 handled_ring_at을 정한다:
 *        기존 목록(e->alarms[0..e->n_alarms-1])에 id가 같은 알람이 있으면 그 handled_ring_at을 물려받고,
 *        없으면 -1.
 *      왜? 울리는 도중 다른 알람을 수정해서 목록이 다시 와도, 이미 울린 회차를 또 울리면 안 되니까.
 *      주의: 새 값을 계산하는 동안 기존 값을 덮어쓰면 안 된다 → 임시 배열 int64_t tmp[ALARM_MAX]에 먼저 모은다.
 *   3) memcpy(e->alarms, alarms, n * sizeof(alarm_t)); 로 목록을 복사하고
 *      tmp를 e->handled_ring_at에 복사, e->n_alarms = n
 *   회차 상태(state, alarm_id, ring_at)는 건드리지 않는다.
 */
void alarm_engine_set_alarms(alarm_engine_t *e, const alarm_t *alarms, size_t n)
{
    /* 1) 32개 넘으면 자르기 */
    if (n > ALARM_MAX) {
        n = ALARM_MAX;
    }

    /* 2) 새 알람 i마다, 기존 목록에서 같은 id를 찾아 값을 물려받는다 */
    int64_t tmp[ALARM_MAX];
    for (size_t i = 0; i < n; i++) {
        tmp[i] = -1;                                  /* 못 찾으면 "없음" */
        for (size_t j = 0; j < e->n_alarms; j++) {    /* 기존 목록 개수만큼 */
            if (e->alarms[j].id == alarms[i].id) {    /* 기존 j번째 id == 새 i번째 id */
                tmp[i] = e->handled_ring_at[j];       /* 기존 j번째의 handled 값 */
                break;
            }
        }
    }

    /* 3) 다 찾았으니 이제 덮어써도 된다 */
    memcpy(e->alarms, alarms, n * sizeof(alarm_t));
    for (size_t i = 0; i < n; i++) {
        e->handled_ring_at[i] = tmp[i];
    }
    e->n_alarms = n;
}

/*
 * 1초마다 불린다. 이번 틱에 할 일을 돌려준다.
 *
 * 단계
 *   0) alarm_output_t out = {0}; 으로 시작한다(actions 0 = 할 일 없음)
 *   1) 첫 틱이면(has_last_tick가 false) last_tick = now - 1, has_last_tick = true
 *      → "직전 1초 동안"만 본다. 전원이 꺼져 있던 사이의 알람을 뒤늦게 울리지 않는다.
 *
 *   2) 진행 중인 회차 처리
 *      - PRE:       now >= ring_at 이면 RINGING으로 가고 actions |= MAIN_START
 *                   아니면 actions |= PRE_CLIP, out.pre_clip_seq = e->pre_clip_seq, 그리고 e->pre_clip_seq++
 *      - PRE_MUTED: now >= ring_at 이면 RINGING + MAIN_START   (PRE_MUTED로 가는 건 Task 4)
 *      - RINGING, SNOOZED: Task 4에서 채운다
 *
 *   3) IDLE이면 새로 시작할 알람을 찾는다. 알람마다(i = 0..n_alarms-1):
 *        a) 꺼져 있으면 건너뛴다
 *        b) T = alarm_next_ring(a, e->last_tick)      ← now가 아니라 last_tick 기준!
 *           T < 0 이거나 T == handled_ring_at[i] 이면 건너뛴다(이미 시작한 회차)
 *        c) 울림: T <= now 이고 0 < now - last_tick <= ALARM_JUMP_LIMIT_S
 *             → handled_ring_at[i] = T, state RINGING, alarm_id, ring_at = T, actions |= MAIN_START, 반복 종료(break)
 *        d) 미리 알림: T > now 이고 pre_minutes > 0 이고 T - pre_minutes * 60 <= now
 *             → handled_ring_at[i] = T, state PRE, alarm_id, ring_at = T,
 *               첫 클립을 바로 낸다: actions |= PRE_CLIP, out.pre_clip_seq = 0, e->pre_clip_seq = 1, break
 *
 *   4) e->last_tick = now   (시간이 뒤로 가도 그대로 갱신한다)
 *   5) out.alarm_id = e->alarm_id; return out;
 *
 * 왜 last_tick 기준인가?
 *   시간이 06:59:30 → 07:01:00으로 건너뛰면 now(07:01) 기준으로는 07:00이 이미 지나서 내일 07:00이 나온다.
 *   last_tick(06:59:30) 기준이면 07:00이 나오고, T <= now 이므로 "건너뛴 사이에 정각이 있었다"를 알 수 있다.
 *
 * 비트 켜기: out.actions |= ALARM_ACT_MAIN_START;   (|= 는 다른 비트를 지우지 않고 그 비트만 켠다)
 */
alarm_output_t alarm_engine_tick(alarm_engine_t *e, int64_t now_local_s)
{
    alarm_output_t out = {0};
    int64_t now = now_local_s;

    /* 1) 첫 틱 */
    if (!e->has_last_tick) {
        e->last_tick = now - 1;
        e->has_last_tick = true;
    }

    /* 2) 진행 중인 회차 */
    if (e->state == ALARM_STATE_PRE) {
        if (now >= e->ring_at) {
            e->state = ALARM_STATE_RINGING;
            out.actions |= ALARM_ACT_MAIN_START;
        } else {
            out.actions |= ALARM_ACT_PRE_CLIP;
            out.pre_clip_seq = e->pre_clip_seq;
            e->pre_clip_seq++;
        }
    } else if (e->state == ALARM_STATE_PRE_MUTED) {
        if (now >= e->ring_at) {
            e->state = ALARM_STATE_RINGING;
            out.actions |= ALARM_ACT_MAIN_START;
        }
    }

    /* 3) IDLE이면 새 알람 찾기 */
    int64_t gap = now - e->last_tick;   /* 직전 틱 이후 흐른 시간(초). 반복 중에는 바뀌지 않는다 */

    if (e->state == ALARM_STATE_IDLE) {
        for (size_t i = 0; i < e->n_alarms; i++) {
            const alarm_t *a = &e->alarms[i];

            /* a) 꺼진 알람 */
            if (!a->enabled) {
                continue;
            }

            /* b) 다음 정각(last_tick 기준). 없거나 이미 시작한 회차면 건너뜀 */
            int64_t T = alarm_next_ring(a, e->last_tick);
            if (T < 0 || T == e->handled_ring_at[i]) {
                continue;
            }

            /* c) 울림: 정각이 지났고, 건너뛴 시간이 허용 범위 안 */
            if (T <= now && gap > 0 && gap <= ALARM_JUMP_LIMIT_S) {
                e->handled_ring_at[i] = T;
                e->state = ALARM_STATE_RINGING;
                e->alarm_id = a->id;
                e->ring_at = T;
                out.actions |= ALARM_ACT_MAIN_START;
                break;
            }

            /* d) 미리 알림: 정각 전이고, 미리 알림 구간 안 */
            if (T > now && a->pre_minutes > 0 && (T - a->pre_minutes * 60) <= now) {
                e->handled_ring_at[i] = T;
                e->state = ALARM_STATE_PRE;
                e->alarm_id = a->id;
                e->ring_at = T;
                out.actions |= ALARM_ACT_PRE_CLIP;
                out.pre_clip_seq = 0;
                e->pre_clip_seq = 1;
                break;
            }
        }
    }

    /* 4) 5) */
    e->last_tick = now;
    out.alarm_id = e->alarm_id;
    return out;
}

/*
 * 버튼 입력을 처리한다. Task 3에서는 LONG만 최소로 만든다(나머지는 Task 4).
 *
 * 단계 (Task 3 범위)
 *   1) IDLE이 아니고 in == ALARM_INPUT_LONG 이면
 *        state = IDLE, actions = AUDIO_STOP | CONFIRM_BEEP
 *   2) 그 밖에는 아무것도 하지 않는다(actions 0)
 */
alarm_output_t alarm_engine_input(alarm_engine_t *e, alarm_input_t in, int64_t now_local_s)
{
    alarm_output_t out = {0};
    (void)now_local_s;

    if (e->state != ALARM_STATE_IDLE && in == ALARM_INPUT_LONG) {
        e->state = ALARM_STATE_IDLE;
        out.actions = ALARM_ACT_AUDIO_STOP | ALARM_ACT_CONFIRM_BEEP;
    }

    return out;
}

/*
 * 화면 표시용으로 현재 상태를 복사해 돌려준다.
 */
alarm_view_t alarm_engine_view(const alarm_engine_t *e)
{
    alarm_view_t v = {0};

    v.state = e->state;
    v.alarm_id = e->alarm_id;
    v.ring_at = e->ring_at;
    v.snooze_until = e->snooze_until;

    return v;
}
