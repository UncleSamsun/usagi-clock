#include "mp3_stream.h"

#include <string.h>

void mp3_stream_init(mp3_stream_t *s, mp3_read_fn read, void *ctx)
{
    memset(s, 0, sizeof(*s));
    mp3dec_init(&s->dec);
    s->read = read;
    s->ctx = ctx;
    s->status = MP3_OK;
}

int mp3_stream_sample_rate(const mp3_stream_t *s)
{
    return s->rate;
}

mp3_status_t mp3_stream_status(const mp3_stream_t *s)
{
    return s->status;
}

/* 남은 입력이 버퍼 절반보다 적으면 앞으로 당기고 더 읽는다. ID3 건너뛰기도 여기서 처리 */
static void refill(mp3_stream_t *s)
{
    for (;;) {
        size_t avail = s->in_len - s->in_pos;

        if (s->skip > 0 && avail > 0) {
            size_t drop = avail < s->skip ? avail : s->skip;
            s->in_pos += drop;
            s->skip -= (uint32_t)drop;
            continue;
        }
        if (s->input_done || avail >= MP3_IN_BUF / 2) {
            return;
        }

        memmove(s->in, s->in + s->in_pos, avail);
        s->in_len = avail;
        s->in_pos = 0;
        size_t got = s->read(s->ctx, s->in + s->in_len, MP3_IN_BUF - s->in_len);
        if (got == 0) {
            s->input_done = true;
        }
        s->in_len += got;

        if (!s->id3_checked && s->in_len >= 10) {
            s->id3_checked = true;
            if (memcmp(s->in, "ID3", 3) == 0) {
                /* 크기 4바이트는 바이트마다 7비트만 쓰는 "싱크세이프" 정수, footer가 있으면 10바이트 더 */
                uint32_t size = ((uint32_t)(s->in[6] & 0x7F) << 21) | ((uint32_t)(s->in[7] & 0x7F) << 14) |
                                ((uint32_t)(s->in[8] & 0x7F) << 7) | (uint32_t)(s->in[9] & 0x7F);
                s->skip = size + 10 + ((s->in[5] & 0x10) ? 10 : 0);
            }
        }
    }
}

/* 다음 프레임 하나를 해독해 pcm에 넣는다. 더 낼 것이 없으면 status를 정하고 false */
static bool decode_next(mp3_stream_t *s)
{
    static mp3d_sample_t frame[MINIMP3_MAX_SAMPLES_PER_FRAME];   /* 해독 버퍼(스테레오 가능) */

    for (;;) {
        refill(s);
        size_t avail = s->in_len - s->in_pos;
        if (avail == 0 && s->input_done) {
            s->status = s->any_frame ? MP3_EOF : MP3_ERR_NO_FRAMES;
            return false;
        }

        mp3dec_frame_info_t info;
        int samples = mp3dec_decode_frame(&s->dec, s->in + s->in_pos, (int)avail, frame, &info);

        if (samples > 0) {
            s->in_pos += (size_t)info.frame_bytes;
            s->junk = 0;
            s->any_frame = true;
            s->rate = info.hz;
            if (info.channels == 2) {
                for (int i = 0; i < samples; i++) {
                    s->pcm[i] = (int16_t)(((int32_t)frame[2 * i] + frame[2 * i + 1]) / 2);
                }
            } else {
                memcpy(s->pcm, frame, (size_t)samples * sizeof(int16_t));
            }
            s->pcm_len = (size_t)samples;
            s->pcm_pos = 0;
            return true;
        }

        /* 프레임이 아님: 건너뛴 만큼(또는 동기를 못 찾은 앞부분) 버린다 */
        size_t drop = (size_t)info.frame_bytes;
        if (drop == 0) {
            /* 버퍼에서 동기를 못 찾았다. 끝까지 읽었으면 전부, 아니면 앞 절반을 버리고 더 읽는다 */
            drop = s->input_done ? avail : avail / 2;
            if (drop == 0) {
                drop = avail;
            }
        }
        s->in_pos += drop;
        s->junk += (uint32_t)drop;
        if (s->junk >= MP3_NOFRAME_LIMIT) {
            s->status = MP3_ERR_NO_FRAMES;
            return false;
        }
    }
}

size_t mp3_stream_read(mp3_stream_t *s, int16_t *out, size_t max)
{
    size_t produced = 0;
    while (produced < max) {
        if (s->pcm_pos < s->pcm_len) {
            size_t take = s->pcm_len - s->pcm_pos;
            if (take > max - produced) {
                take = max - produced;
            }
            memcpy(out + produced, s->pcm + s->pcm_pos, take * sizeof(int16_t));
            s->pcm_pos += take;
            produced += take;
            continue;
        }
        if (s->status != MP3_OK || !decode_next(s)) {
            break;
        }
    }
    return produced;
}
