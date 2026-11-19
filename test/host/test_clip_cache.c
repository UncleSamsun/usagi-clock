#include <string.h>

#include "unity.h"

#include "clip_cache.h"

/* 가짜 소스: rate로 total 샘플을 내는 클립. 값은 위치(i % 1000) */
typedef struct {
    int    rate;
    size_t total;
    size_t pos;
    bool   fail;
} fake_t;

static size_t fake_read(void *ctx, int16_t *out, size_t max)
{
    fake_t *f = ctx;
    if (f->fail) {
        return 0;
    }
    size_t n = f->total - f->pos;
    if (n > max) {
        n = max;
    }
    for (size_t i = 0; i < n; i++) {
        out[i] = (int16_t)((f->pos + i) % 1000);
    }
    f->pos += n;
    return n;
}
static int fake_rate(void *ctx) { fake_t *f = ctx; return f->fail ? 0 : f->rate; }
static bool fake_failed(void *ctx) { fake_t *f = ctx; return f->fail; }

static clip_source_t src_of(fake_t *f)
{
    clip_source_t s = { .read = fake_read, .rate = fake_rate, .failed = fake_failed, .ctx = f };
    return s;
}

static int16_t buf[3 * 48000];
static clip_cache_t cache;

void setUp(void)
{
    clip_cache_init(&cache, buf, sizeof(buf) / sizeof(buf[0]));
    clip_cache_clear(&cache, "default");
}

void tearDown(void) {}

void test_add_short_clip(void)
{
    fake_t f = { .rate = 44100, .total = 30000 };
    clip_source_t s = src_of(&f);
    TEST_ASSERT_TRUE(clip_cache_add(&cache, &s));
    TEST_ASSERT_EQUAL_UINT8(1, cache.count);
    TEST_ASSERT_EQUAL_INT(44100, cache.rate[0]);
    TEST_ASSERT_EQUAL_size_t(30000, cache.length[0]);
    TEST_ASSERT_EQUAL_INT16(999, buf[999]);
}

/* Review Focus 5: 1초를 넘는 클립은 1초에서 자른다 */
void test_clip_truncated_to_one_second(void)
{
    fake_t f = { .rate = 22050, .total = 44100 };   /* 2초 */
    clip_source_t s = src_of(&f);
    TEST_ASSERT_TRUE(clip_cache_add(&cache, &s));
    TEST_ASSERT_EQUAL_size_t(22050, cache.length[0]);
}

void test_offsets_are_packed(void)
{
    fake_t a = { .rate = 16000, .total = 8000 };
    fake_t b = { .rate = 16000, .total = 12000 };
    clip_source_t sa = src_of(&a), sb = src_of(&b);
    TEST_ASSERT_TRUE(clip_cache_add(&cache, &sa));
    TEST_ASSERT_TRUE(clip_cache_add(&cache, &sb));
    TEST_ASSERT_EQUAL_size_t(0, cache.offset[0]);
    TEST_ASSERT_EQUAL_size_t(8000, cache.offset[1]);
    TEST_ASSERT_EQUAL_size_t(20000, cache.used);
}

void test_bad_clip_rejected(void)
{
    fake_t f = { .rate = 44100, .total = 1000, .fail = true };
    clip_source_t s = src_of(&f);
    TEST_ASSERT_FALSE(clip_cache_add(&cache, &s));
    TEST_ASSERT_EQUAL_UINT8(0, cache.count);
}

void test_buffer_full_rejected(void)
{
    clip_cache_init(&cache, buf, 50000);
    clip_cache_clear(&cache, "default");
    fake_t a = { .rate = 44100, .total = 44100 };
    fake_t b = { .rate = 44100, .total = 44100 };
    clip_source_t sa = src_of(&a), sb = src_of(&b);
    TEST_ASSERT_TRUE(clip_cache_add(&cache, &sa));
    TEST_ASSERT_FALSE(clip_cache_add(&cache, &sb));
    TEST_ASSERT_EQUAL_UINT8(1, cache.count);
    TEST_ASSERT_EQUAL_size_t(44100, cache.used);
}

void test_too_many_clips_rejected(void)
{
    for (int i = 0; i < CLIPSET_MAX; i++) {
        fake_t f = { .rate = 16000, .total = 100 };
        clip_source_t s = src_of(&f);
        TEST_ASSERT_TRUE(clip_cache_add(&cache, &s));
    }
    fake_t f = { .rate = 16000, .total = 100 };
    clip_source_t s = src_of(&f);
    TEST_ASSERT_FALSE(clip_cache_add(&cache, &s));
}

void test_clear_resets(void)
{
    fake_t f = { .rate = 16000, .total = 100 };
    clip_source_t s = src_of(&f);
    clip_cache_add(&cache, &s);
    clip_cache_clear(&cache, "other");
    TEST_ASSERT_EQUAL_UINT8(0, cache.count);
    TEST_ASSERT_EQUAL_size_t(0, cache.used);
    TEST_ASSERT_EQUAL_STRING("other", cache.set_name);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_add_short_clip);
    RUN_TEST(test_clip_truncated_to_one_second);
    RUN_TEST(test_offsets_are_packed);
    RUN_TEST(test_bad_clip_rejected);
    RUN_TEST(test_buffer_full_rejected);
    RUN_TEST(test_too_many_clips_rejected);
    RUN_TEST(test_clear_resets);
    return UNITY_END();
}
