#include "audio.h"

#include <string.h>

#define MS(x) ((uint32_t)(x) * AUDIO_SAMPLE_RATE / 1000u)

/* 무늬 한 구간: len 샘플 동안 freq(0이면 쉼) */
typedef struct { uint32_t freq; uint32_t len; } seg_t;

static const seg_t ALARM_SEGS[] = { { 2000, MS(250) }, { 0, MS(250) } };
static const seg_t TICK_SEGS[] = { { 1000, MS(80) } };
static const seg_t CONFIRM_SEGS[] = { { 3000, MS(60) }, { 0, MS(60) }, { 3000, MS(60) } };

static const seg_t *segs_of(beep_kind_t k, size_t *count, bool *loop)
{
    switch (k) {
    case BEEP_ALARM_LOOP:
        *count = sizeof(ALARM_SEGS) / sizeof(ALARM_SEGS[0]);
        *loop = true;
        return ALARM_SEGS;
    case BEEP_TICK:
        *count = 1;
        *loop = false;
        return TICK_SEGS;
    case BEEP_CONFIRM:
    default:
        *count = sizeof(CONFIRM_SEGS) / sizeof(CONFIRM_SEGS[0]);
        *loop = false;
        return CONFIRM_SEGS;
    }
}

void beep_start(beep_t *b, beep_kind_t kind, uint8_t volume)
{
    memset(b, 0, sizeof(*b));
    b->kind = kind;
    b->volume = volume;
}

size_t beep_fill(beep_t *b, int16_t *out, size_t n)
{
    size_t count = 0;
    bool loop = false;
    const seg_t *segs = segs_of(b->kind, &count, &loop);

    uint32_t total = 0;
    for (size_t i = 0; i < count; i++) {
        total += segs[i].len;
    }

    size_t done = 0;
    while (done < n) {
        if (b->pos >= total) {
            if (!loop) {
                break;
            }
            b->pos = 0;
        }
        /* 지금 위치가 속한 구간과 그 구간에서 남은 샘플 수 */
        uint32_t start = 0;
        size_t s = 0;
        while (b->pos >= start + segs[s].len) {
            start += segs[s].len;
            s++;
        }
        size_t take = start + segs[s].len - b->pos;
        if (take > n - done) {
            take = n - done;
        }
        if (segs[s].freq == 0) {
            memset(out + done, 0, take * sizeof(int16_t));
            b->tone.phase = 0;   /* 다음 소리 구간을 같은 위상에서 시작 */
        } else {
            audio_tone_fill(&b->tone, out + done, take, segs[s].freq, b->volume);
        }
        b->pos += (uint32_t)take;
        done += take;
    }
    return done;
}
