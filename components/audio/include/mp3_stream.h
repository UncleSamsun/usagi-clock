#ifndef MP3_STREAM_H
#define MP3_STREAM_H

/*
 * MP3 스트림 해독(순수 C, minimp3 감싸기).
 * 파일 전체를 메모리에 올리지 않고 read 콜백으로 4KB씩 읽어 모노 int16을 낸다.
 *
 * 판정 규칙
 *   - 처음부터 또는 마지막 정상 프레임 이후 MP3_NOFRAME_LIMIT 바이트 동안 프레임이 없으면 MP3_ERR_NO_FRAMES
 *   - 정상 프레임을 하나라도 낸 뒤 입력이 끝나면 MP3_EOF, 하나도 못 낸 채 끝나면 MP3_ERR_NO_FRAMES
 *   - 첫 읽기에서 ID3v2 태그("ID3" + 싱크세이프 크기)는 건너뛴다
 *   - 스테레오는 (L+R)/2로 섞는다(스피커가 하나)
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "minimp3.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MP3_IN_BUF        4096
#define MP3_NOFRAME_LIMIT 16384

typedef size_t (*mp3_read_fn)(void *ctx, uint8_t *buf, size_t n);   /* 0 = 파일 끝(읽기 오류 포함) */

typedef enum { MP3_OK = 0, MP3_EOF, MP3_ERR_NO_FRAMES } mp3_status_t;

typedef struct {
    mp3dec_t     dec;
    mp3_read_fn  read;
    void        *ctx;
    uint8_t      in[MP3_IN_BUF];
    size_t       in_len;
    size_t       in_pos;
    uint32_t     skip;          /* 아직 건너뛸 ID3 태그 바이트 */
    bool         id3_checked;
    bool         input_done;
    bool         any_frame;
    uint32_t     junk;          /* 마지막 정상 프레임 이후 버린 바이트 */
    int16_t      pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];   /* 모노로 섞은 한 프레임 */
    size_t       pcm_len;
    size_t       pcm_pos;
    int          rate;
    mp3_status_t status;
} mp3_stream_t;

void         mp3_stream_init(mp3_stream_t *s, mp3_read_fn read, void *ctx);
/* 모노 int16을 최대 max개 채운다. 0이면 끝났거나 오류 → mp3_stream_status로 이유를 본다 */
size_t       mp3_stream_read(mp3_stream_t *s, int16_t *out, size_t max);
int          mp3_stream_sample_rate(const mp3_stream_t *s);   /* 첫 프레임 전에는 0 */
mp3_status_t mp3_stream_status(const mp3_stream_t *s);

#ifdef __cplusplus
}
#endif

#endif /* MP3_STREAM_H */
