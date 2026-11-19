#ifndef AUDIO_H
#define AUDIO_H

/*
 * 오디오.
 *   순수 부분: 사인파 생성, 볼륨 적용, 명령 형식 (PC 테스트)
 *   ESP 부분:  I2S 출력 작업(audio_esp.c)
 *
 * 스피커 보호: 볼륨 100일 때 최대 진폭은 AUDIO_PEAK_MAX(int16 최대의 50%).
 * [실물 확인] 소리가 너무 크거나 찌그러지면 이 값을 낮춘다.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_SAMPLE_RATE 16000      /* 비프음 샘플레이트 */
#define AUDIO_PEAK_MAX    16383      /* 볼륨 100일 때 최대 진폭 */
#define AUDIO_NAME_MAX    64         /* alarm_t.song 크기와 같음 */

typedef struct { uint32_t phase; } audio_tone_t;   /* 32비트 위상 누산기 */

/* freq_hz 사인파(AUDIO_SAMPLE_RATE 기준)를 n개 채운다. volume 0..100(초과는 100). 호출 사이에 위상이 이어진다 */
void audio_tone_fill(audio_tone_t *t, int16_t *out, size_t n, uint32_t freq_hz, uint8_t volume);

/* buf의 샘플을 volume(0..100, 초과는 100)에 맞춰 줄인다. 볼륨 100에서 최대 진폭은 AUDIO_PEAK_MAX */
void audio_apply_volume(int16_t *buf, size_t n, uint8_t volume);

/* 비프 무늬(모두 AUDIO_SAMPLE_RATE 기준). SD나 MP3가 실패해도 알람이 울리게 하는 대체 소리다(설계 5.3) */
typedef enum {
    BEEP_ALARM_LOOP = 0,   /* 2kHz 0.25초 소리 / 0.25초 쉼, 끝없이 반복 */
    BEEP_TICK,             /* 1kHz 80ms 한 번 */
    BEEP_CONFIRM,          /* 3kHz 60ms, 쉼 60ms, 3kHz 60ms */
} beep_kind_t;

typedef struct {
    beep_kind_t  kind;
    uint8_t      volume;
    uint32_t     pos;      /* 시작부터 지난 샘플 수 */
    audio_tone_t tone;
} beep_t;

void   beep_start(beep_t *b, beep_kind_t kind, uint8_t volume);
/* 최대 n샘플을 채우고 채운 수를 돌려준다. n보다 작으면 무늬가 끝난 것이다(반복 무늬는 항상 n) */
size_t beep_fill(beep_t *b, int16_t *out, size_t n);

typedef enum {
    AUDIO_OP_STOP = 0,
    AUDIO_OP_ALARM_LOOP,   /* 본 알람: 노래 반복(계획 3), 실패하면 비프 반복 */
    AUDIO_OP_PRE_TICK,     /* 미리 알림 1회: 클립(계획 3), 없으면 짧은 비프 */
    AUDIO_OP_CONFIRM,      /* 길게 누르기 확인음 */
    AUDIO_OP_PREVIEW,      /* 노래 미리 듣기 10초(계획 4) */
} audio_op_t;

typedef struct {
    audio_op_t op;
    uint8_t    volume;               /* 0..100 */
    char       name[AUDIO_NAME_MAX]; /* ALARM_LOOP·PREVIEW: 노래 이름, PRE_TICK: 클립 세트 이름, 그 외 무시 */
} audio_cmd_t;

#ifdef ESP_PLATFORM
#include "esp_err.h"

/* I2S0 std 모드, 16비트 모노(왼쪽 슬롯), 출력 작업 시작 */
esp_err_t audio_start(int bclk_gpio, int ws_gpio, int dout_gpio);
/* 길이 8 큐. 꽉 차면 이번 명령을 버리고 경고 로그 */
void      audio_post(const audio_cmd_t *cmd);
#endif

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_H */
