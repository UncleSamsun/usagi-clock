#include "clip_cache.h"

#include <string.h>

void clip_cache_init(clip_cache_t *c, int16_t *pcm, size_t capacity)
{
    memset(c, 0, sizeof(*c));
    c->pcm = pcm;
    c->capacity = capacity;
}

void clip_cache_clear(clip_cache_t *c, const char *set_name)
{
    c->used = 0;
    c->count = 0;
    memset(c->set_name, 0, sizeof(c->set_name));
    strncpy(c->set_name, set_name, sizeof(c->set_name) - 1);
}

bool clip_cache_add(clip_cache_t *c, const clip_source_t *src)
{
    if (c->count >= CLIPSET_MAX) {
        return false;
    }

    size_t start = c->used;
    size_t len = 0;
    size_t limit = 0;   /* 1초 = rate 샘플. 첫 read 뒤에 안다 */

    for (;;) {
        size_t room = c->capacity - (start + len);
        if (limit > 0 && len >= limit) {
            break;                                   /* 1초 채움: 나머지는 버린다 */
        }
        size_t want = 1152;
        if (limit > 0 && want > limit - len) {
            want = limit - len;
        }
        if (want > room) {
            if (room == 0) {
                return false;                         /* 버퍼 부족: 이 클립은 넣지 않는다 */
            }
            want = room;
        }
        size_t got = src->read(src->ctx, c->pcm + start + len, want);
        if (limit == 0) {
            int rate = src->rate(src->ctx);
            if (rate > 0) {
                limit = (size_t)rate;
            }
        }
        if (got == 0) {
            break;
        }
        len += got;
    }

    if (len > limit && limit > 0) {
        len = limit;
    }
    if (len == 0 || src->failed(src->ctx) || limit == 0) {
        return false;
    }

    c->offset[c->count] = start;
    c->length[c->count] = len;
    c->rate[c->count] = (int)limit;
    c->count++;
    c->used = start + len;
    return true;
}
