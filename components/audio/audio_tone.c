#include "audio.h"

#include <math.h>

/*
 * 사인파 생성.
 * 위상 누산기(32비트)의 위쪽 8비트로 256칸 사인 표를 찾는다.
 * 한 샘플마다 위상을 freq * 2^32 / rate 만큼 더하면 32비트가 넘쳐 돌아가는 것이 곧 한 주기다.
 */

#define SINE_TABLE_LEN 256

static int16_t s_sine[SINE_TABLE_LEN];
static bool s_sine_ready;

static void sine_init(void)
{
    const double two_pi = 6.283185307179586;
    for (int i = 0; i < SINE_TABLE_LEN; i++) {
        s_sine[i] = (int16_t)lrint(32767.0 * sin(two_pi * i / SINE_TABLE_LEN));
    }
    s_sine_ready = true;
}

void audio_tone_fill(audio_tone_t *t, int16_t *out, size_t n, uint32_t freq_hz, uint8_t volume)
{
    if (!s_sine_ready) {
        sine_init();
    }
    if (volume > 100) {
        volume = 100;
    }

    uint32_t step = (uint32_t)(((uint64_t)freq_hz << 32) / AUDIO_SAMPLE_RATE);
    /* 표 값(최대 32767) × PEAK_MAX × volume / (32767 × 100) — 정수 연산만 */
    int64_t num = (int64_t)AUDIO_PEAK_MAX * volume;
    int64_t den = 32767LL * 100;

    for (size_t i = 0; i < n; i++) {
        int16_t s = s_sine[t->phase >> 24];
        out[i] = (int16_t)((int64_t)s * num / den);
        t->phase += step;
    }
}

void audio_apply_volume(int16_t *buf, size_t n, uint8_t volume)
{
    if (volume > 100) {
        volume = 100;
    }
    int64_t num = (int64_t)AUDIO_PEAK_MAX * volume;
    int64_t den = 32767LL * 100;
    for (size_t i = 0; i < n; i++) {
        int64_t v = (int64_t)buf[i] * num / den;
        if (v > AUDIO_PEAK_MAX) {
            v = AUDIO_PEAK_MAX;          /* -32768은 32767보다 1 크다 */
        } else if (v < -AUDIO_PEAK_MAX) {
            v = -AUDIO_PEAK_MAX;
        }
        buf[i] = (int16_t)v;
    }
}
