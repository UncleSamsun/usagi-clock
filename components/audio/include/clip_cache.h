#ifndef CLIP_CACHE_H
#define CLIP_CACHE_H

/*
 * 미리 알림 클립 캐시(순수 C).
 * "우! 나!"를 매초 경계에 맞춰 내려면 SD에서 매번 파일을 열면 늦을 수 있다.
 * 그래서 세트를 처음 쓸 때 모든 클립을 해독해 PCM으로 한 버퍼(PSRAM)에 차례로 담아 둔다.
 * 클립마다 1초까지만 담는다(설계 5.3 "1초보다 길면 다음 초에서 끊는다").
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CLIPSET_MAX        16
#define CLIP_CACHE_SAMPLES (16 * 48000)

/* 클립 PCM을 읽어 오는 곳(펌웨어는 mp3_stream, 테스트는 가짜 소스) */
typedef struct {
    size_t (*read)(void *ctx, int16_t *out, size_t max);   /* 0 = 끝 */
    int    (*rate)(void *ctx);                              /* 첫 read 뒤 유효, 실패면 0 */
    bool   (*failed)(void *ctx);                            /* 해독 오류였는지 */
    void   *ctx;
} clip_source_t;

typedef struct {
    int16_t *pcm;                  /* 바깥에서 준 버퍼 */
    size_t   capacity;             /* 샘플 수 */
    size_t   used;
    char     set_name[32];         /* 지금 담긴 세트, 빈 값이면 없음 */
    uint8_t  count;
    size_t   offset[CLIPSET_MAX];
    size_t   length[CLIPSET_MAX];  /* 샘플 수, 최대 1초(= rate) */
    int      rate[CLIPSET_MAX];
} clip_cache_t;

void clip_cache_init(clip_cache_t *c, int16_t *pcm, size_t capacity);
void clip_cache_clear(clip_cache_t *c, const char *set_name);
/* 소스를 끝까지(최대 1초) 읽어 한 클립으로 넣는다.
 * 해독 실패, 소리 없음, 클립 수 초과, 버퍼 부족이면 false(이미 넣은 클립은 유지) */
bool clip_cache_add(clip_cache_t *c, const clip_source_t *src);

#ifdef __cplusplus
}
#endif

#endif /* CLIP_CACHE_H */
