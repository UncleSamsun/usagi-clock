#include "alarm_core.h"

#include <string.h> /* memset, memcpy */

/*
 * 알람 엔진: 1초 틱과 버튼 입력을 받아 "회차"의 상태를 바꾸고, 할 일(actions)을 돌려준다.
 *
 * 회차 = 한 번 울리는 단위. 미리 알림(PRE) → 정각 울림(RINGING) ↔ 스누즈(SNOOZED) → 끝(IDLE).
 * 회차가 진행 중일 때 다른 알람이 시작 조건을 만족하면 새 회차를 만들지 않고 구성원(members)으로 합류만 한다.
 * 회차가 끝나면 구성원 중 "한 번만"(days_mask 0) 알람을 끄고 disable_ids로 알린다.
 */

/* 알람 하나가 이번 틱에 회차를 시작(또는 합류)할 조건 */
typedef enum {
    MATCH_NONE = 0,
    MATCH_RING,   /* 정각이 지났다 → 바로 울림 */
    MATCH_PRE,    /* 미리 알림 구간 안이다 */
} match_t;

void alarm_engine_init(alarm_engine_t *e, const alarm_settings_t *s)
{
    memset(e, 0, sizeof(*e));

    e->settings = *s;

    for (int i = 0; i < ALARM_MAX; i++) {
        e->handled_ring_at[i] = -1;
    }
}

/*
 * 알람 목록을 엔진에 넣는다(사본). 웹에서 알람을 바꿀 때마다 불린다.
 * id가 같은 알람은 handled_ring_at을 물려받아, 이미 시작한 회차가 다시 시작되지 않게 한다.
 * 회차 상태(state, alarm_id, ring_at, members)는 건드리지 않는다.
 */
void alarm_engine_set_alarms(alarm_engine_t *e, const alarm_t *alarms, size_t n)
{
    if (n > ALARM_MAX) {
        n = ALARM_MAX;
    }

    /* 기존 값을 덮어쓰기 전에, 새 알람마다 물려받을 값을 tmp에 모은다 */
    int64_t tmp[ALARM_MAX];
    for (size_t i = 0; i < n; i++) {
        tmp[i] = -1;
        for (size_t j = 0; j < e->n_alarms; j++) {
            if (e->alarms[j].id == alarms[i].id) {
                tmp[i] = e->handled_ring_at[j];
                break;
            }
        }
    }

    if (n > 0) {   /* 빈 목록이면 alarms가 NULL일 수 있다. memcpy에 NULL을 주면 안 된다 */
        memcpy(e->alarms, alarms, n * sizeof(alarm_t));
    }
    for (size_t i = 0; i < n; i++) {
        e->handled_ring_at[i] = tmp[i];
    }
    e->n_alarms = n;
}

/*
 * 알람 i가 이번 틱에 시작 조건을 만족하는지 본다. 만족하면 그 회차의 정각을 *ring_at에 쓴다.
 *   - 다음 정각 T는 now가 아니라 last_tick 기준으로 구한다. 시간이 조금 건너뛰어도 그 사이의 정각을 놓치지 않는다.
 *   - 울림: T <= now 이고 건너뛴 시간 gap이 0초 초과 ALARM_JUMP_LIMIT_S 이하
 *   - 미리 알림: T > now 이고 T - pre_minutes*60 <= now (구간 안이면 언제든 시작)
 */
static match_t match_alarm(const alarm_engine_t *e, size_t i, int64_t now, int64_t gap, int64_t *ring_at)
{
    const alarm_t *a = &e->alarms[i];
    if (!a->enabled) {
        return MATCH_NONE;
    }

    int64_t T = alarm_next_ring(a, e->last_tick);
    if (T < 0 || T == e->handled_ring_at[i]) {   /* 없거나 이미 시작한 회차 */
        return MATCH_NONE;
    }

    *ring_at = T;
    if (T <= now && gap > 0 && gap <= ALARM_JUMP_LIMIT_S) {
        return MATCH_RING;
    }
    if (T > now && a->pre_minutes > 0 && (T - a->pre_minutes * 60) <= now) {
        return MATCH_PRE;
    }
    return MATCH_NONE;
}

static void add_member(alarm_engine_t *e, uint8_t id)
{
    for (size_t k = 0; k < e->n_members; k++) {
        if (e->members[k] == id) {
            return;
        }
    }
    if (e->n_members < ALARM_MAX) {
        e->members[e->n_members++] = id;
    }
}

/* 본 알람 시작. 자동 정지 시간은 여기서부터 다시 센다 */
static void start_ringing(alarm_engine_t *e, int64_t now, alarm_output_t *out)
{
    e->state = ALARM_STATE_RINGING;
    e->ring_started_at = now;
    out->actions |= ALARM_ACT_MAIN_START;
}

/*
 * 회차를 끝낸다. 구성원 중 지금 목록에 남아 있는 "한 번만" 알람을 끄고 disable_ids에 담는다.
 * 목록에서 삭제된 알람은 찾을 수 없으므로 담기지 않는다.
 */
static void end_session(alarm_engine_t *e, alarm_output_t *out)
{
    for (size_t k = 0; k < e->n_members; k++) {
        for (size_t i = 0; i < e->n_alarms; i++) {
            alarm_t *a = &e->alarms[i];
            if (a->id == e->members[k]) {
                if (a->days_mask == 0 && a->enabled) {
                    a->enabled = false;
                    out->disable_ids[out->n_disable++] = a->id;
                }
                break;
            }
        }
    }
    if (out->n_disable > 0) {
        out->actions |= ALARM_ACT_DISABLE_ONCE;
    }

    out->actions |= ALARM_ACT_AUDIO_STOP;
    e->state = ALARM_STATE_IDLE;
    e->n_members = 0;
}

/*
 * 1초마다 불린다.
 *   1) 첫 틱이면 last_tick = now - 1 (전원이 꺼져 있던 사이의 알람은 뒤늦게 울리지 않는다)
 *   2) 진행 중인 회차의 시간 조건 처리(정각 도달, 자동 정지, 스누즈 끝)
 *   3) 알람마다 시작 조건 확인 → IDLE이면 새 회차 시작, 진행 중이면 합류
 *   4) last_tick = now (시간이 뒤로 가도 갱신. 이중 울림은 handled_ring_at이 막는다)
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
    int64_t gap = now - e->last_tick;   /* 직전 틱 이후 흐른 시간(초) */

    /* 2) 진행 중인 회차 */
    switch (e->state) {
    case ALARM_STATE_PRE:
        if (now >= e->ring_at) {
            start_ringing(e, now, &out);
        } else {
            out.actions |= ALARM_ACT_PRE_CLIP;
            out.pre_clip_seq = e->pre_clip_seq;
            e->pre_clip_seq++;
        }
        break;
    case ALARM_STATE_PRE_MUTED:
        if (now >= e->ring_at) {
            start_ringing(e, now, &out);
        }
        break;
    case ALARM_STATE_RINGING:
        if (now - e->ring_started_at >= (int64_t)e->settings.auto_stop_min * 60) {
            end_session(e, &out);
        }
        break;
    case ALARM_STATE_SNOOZED:
        if (now >= e->snooze_until) {
            start_ringing(e, now, &out);
        }
        break;
    case ALARM_STATE_IDLE:
        break;
    }

    /* 3) 시작 조건 확인 */
    match_t kind[ALARM_MAX];
    int64_t ring_at[ALARM_MAX];
    for (size_t i = 0; i < e->n_alarms; i++) {
        ring_at[i] = -1;
        kind[i] = match_alarm(e, i, now, gap, &ring_at[i]);
    }

    if (e->state == ALARM_STATE_IDLE) {
        /* 대표 고르기: 울림 조건 알람이 있으면 그것을, 없으면 미리 알림 조건 알람을 쓴다.
         * 정각이 지난 알람이 다른 알람의 미리 알림 때문에 늦게 울리지 않게 하려는 것이다. */
        size_t starter = e->n_alarms;   /* 못 찾음 */
        for (size_t i = 0; i < e->n_alarms && starter == e->n_alarms; i++) {
            if (kind[i] == MATCH_RING) {
                starter = i;
            }
        }
        for (size_t i = 0; i < e->n_alarms && starter == e->n_alarms; i++) {
            if (kind[i] == MATCH_PRE) {
                starter = i;
            }
        }

        if (starter < e->n_alarms) {
            e->n_members = 0;
            e->alarm_id = e->alarms[starter].id;
            e->ring_at = ring_at[starter];
            if (kind[starter] == MATCH_RING) {
                start_ringing(e, now, &out);
            } else {
                e->state = ALARM_STATE_PRE;
                out.actions |= ALARM_ACT_PRE_CLIP;
                out.pre_clip_seq = 0;
                e->pre_clip_seq = 1;
            }
        }
    }

    /* 회차가 진행 중이면(방금 시작한 것 포함) 조건을 만족한 알람을 모두 구성원으로 넣는다.
     * 상태와 소리는 바꾸지 않는다. handled_ring_at을 기록해 같은 회차로 다시 잡히지 않게 한다. */
    if (e->state != ALARM_STATE_IDLE) {
        for (size_t i = 0; i < e->n_alarms; i++) {
            if (kind[i] != MATCH_NONE) {
                e->handled_ring_at[i] = ring_at[i];
                add_member(e, e->alarms[i].id);
            }
        }
    }

    /* 4) */
    e->last_tick = now;
    out.alarm_id = e->alarm_id;
    return out;
}

/*
 * 버튼 입력.
 *   SHORT: PRE → PRE_MUTED(미리 알림만 멈춤), RINGING → SNOOZED(snooze_min분 뒤 다시)
 *   LONG:  진행 중인 회차를 끝낸다 + 확인음
 *   IDLE에서는 아무것도 하지 않는다.
 */
alarm_output_t alarm_engine_input(alarm_engine_t *e, alarm_input_t in, int64_t now_local_s)
{
    alarm_output_t out = {0};

    if (e->state == ALARM_STATE_IDLE) {
        return out;
    }

    if (in == ALARM_INPUT_LONG) {
        end_session(e, &out);
        out.actions |= ALARM_ACT_CONFIRM_BEEP;
    } else if (in == ALARM_INPUT_SHORT) {
        if (e->state == ALARM_STATE_PRE) {
            e->state = ALARM_STATE_PRE_MUTED;
            out.actions |= ALARM_ACT_AUDIO_STOP;
        } else if (e->state == ALARM_STATE_RINGING) {
            e->state = ALARM_STATE_SNOOZED;
            e->snooze_until = now_local_s + (int64_t)e->settings.snooze_min * 60;
            out.actions |= ALARM_ACT_AUDIO_STOP;
        }
    }

    out.alarm_id = e->alarm_id;
    return out;
}

/* 화면 표시용으로 현재 상태를 복사해 돌려준다 */
alarm_view_t alarm_engine_view(const alarm_engine_t *e)
{
    alarm_view_t v = {0};

    v.state = e->state;
    v.alarm_id = e->alarm_id;
    v.ring_at = e->ring_at;
    v.snooze_until = e->snooze_until;

    return v;
}

void alarm_engine_set_settings(alarm_engine_t *e, const alarm_settings_t *s)
{
    e->settings = *s;
}
