/*
 * 실제 MP3(ffmpeg로 만든 사인파)로 해독 결과의 소리 자체를 확인한다.
 * fixtures 폴더에 MP3가 없으면 CMake가 이 테스트를 빼고 경고를 낸다(make_fixtures.sh 참고).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"

#include "mp3_stream.h"

static size_t file_read(void *ctx, uint8_t *buf, size_t n)
{
    return fread(buf, 1, n, (FILE *)ctx);
}

static int16_t pcm[96000];
static mp3_stream_t s;

static size_t decode(const char *name, int *rate)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", FIXTURE_DIR, name);
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    mp3_stream_init(&s, file_read, f);
    size_t total = 0, n;
    while ((n = mp3_stream_read(&s, pcm + total, sizeof(pcm) / sizeof(pcm[0]) - total)) > 0) {
        total += n;
    }
    fclose(f);
    *rate = mp3_stream_sample_rate(&s);
    return total;
}

static int crossings(const int16_t *b, size_t n)
{
    int c = 0;
    for (size_t i = 1; i < n; i++) {
        if (b[i - 1] < 0 && b[i] >= 0) {
            c++;
        }
    }
    return c;
}

static int peak(const int16_t *b, size_t n)
{
    int p = 0;
    for (size_t i = 0; i < n; i++) {
        if (abs(b[i]) > p) {
            p = abs(b[i]);
        }
    }
    return p;
}

void setUp(void) {}
void tearDown(void) {}

void test_mono_44k_one_second(void)
{
    int rate = 0;
    size_t n = decode("tone440_44k_mono.mp3", &rate);
    TEST_ASSERT_EQUAL_INT(44100, rate);
    TEST_ASSERT_TRUE(n >= 44100 && n <= 47000);
    TEST_ASSERT_EQUAL_INT(MP3_EOF, mp3_stream_status(&s));
    TEST_ASSERT_INT_WITHIN(5, 440, crossings(pcm, 44100));
}

void test_stereo_with_id3_downmixed(void)
{
    int rate = 0;
    decode("tone440_44k_mono.mp3", &rate);
    int mono_peak = peak(pcm, 44100);
    size_t n = decode("tone440_44k_stereo_id3.mp3", &rate);
    TEST_ASSERT_EQUAL_INT(44100, rate);
    TEST_ASSERT_TRUE(n >= 44100 && n <= 47000);
    int p = peak(pcm, 44100);
    TEST_ASSERT_TRUE(p >= mono_peak * 8 / 10 && p <= mono_peak * 12 / 10);
}

void test_other_sample_rate(void)
{
    int rate = 0;
    size_t n = decode("tone1k_22k_mono_2s.mp3", &rate);
    TEST_ASSERT_EQUAL_INT(22050, rate);
    TEST_ASSERT_TRUE(n >= 44100 && n <= 47000);
    TEST_ASSERT_INT_WITHIN(5, 1000, crossings(pcm, 22050));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mono_44k_one_second);
    RUN_TEST(test_stereo_with_id3_downmixed);
    RUN_TEST(test_other_sample_rate);
    return UNITY_END();
}
