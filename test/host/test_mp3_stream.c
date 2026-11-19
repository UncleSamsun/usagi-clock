/*
 * mp3_stream 테스트: 인코더 없이 손으로 만든 "무음 프레임"으로 틀(프레임 찾기, 끝·오류 판정,
 * ID3 건너뛰기, 스테레오 섞기, 나눠 읽기)을 확인한다. 실제 소리(주파수) 확인은 test_mp3_fixtures.c.
 *
 * MPEG-1 Layer III, 128kbps, 44.1kHz, 패딩 없음 → 프레임 길이 = 144 × 128000 / 44100 = 417바이트.
 * 헤더 4바이트 뒤 side info와 본문을 모두 0으로 두면 해독 결과는 무음 1152샘플(채널당)이다.
 */
#include <stdlib.h>
#include <string.h>

#include "unity.h"

#include "mp3_stream.h"

#define FRAME_BYTES 417

static uint8_t data[64 * 1024];
static size_t data_len;

typedef struct { const uint8_t *p; size_t len; size_t pos; } mem_t;

static size_t mem_read(void *ctx, uint8_t *buf, size_t n)
{
    mem_t *m = ctx;
    size_t left = m->len - m->pos;
    if (n > left) {
        n = left;
    }
    memcpy(buf, m->p + m->pos, n);
    m->pos += n;
    return n;
}

/* mono: 채널 모드 11, stereo: 00 */
static void add_frames(int count, bool stereo)
{
    for (int i = 0; i < count; i++) {
        uint8_t *f = data + data_len;
        memset(f, 0, FRAME_BYTES);
        f[0] = 0xFF;
        f[1] = 0xFB;                    /* MPEG-1, Layer III, CRC 없음 */
        f[2] = 0x90;                    /* 128kbps, 44.1kHz, 패딩 없음 */
        f[3] = stereo ? 0x00 : 0xC0;
        data_len += FRAME_BYTES;
    }
}

static void add_bytes(uint8_t value, size_t n)
{
    memset(data + data_len, value, n);
    data_len += n;
}

/* ID3v2.3 태그: "ID3" 03 00 플래그 + 싱크세이프 크기 + 본문 */
static void add_id3(size_t body)
{
    uint8_t *h = data + data_len;
    memcpy(h, "ID3", 3);
    h[3] = 3;
    h[4] = 0;
    h[5] = 0;
    h[6] = (uint8_t)((body >> 21) & 0x7F);
    h[7] = (uint8_t)((body >> 14) & 0x7F);
    h[8] = (uint8_t)((body >> 7) & 0x7F);
    h[9] = (uint8_t)(body & 0x7F);
    data_len += 10;
    add_bytes(0xFF, body);   /* 일부러 동기처럼 보이는 0xFF로 채움 */
}

static mp3_stream_t s;
static mem_t m;
static int16_t out[16384];

static size_t read_all(size_t chunk, size_t *calls)
{
    m = (mem_t){ .p = data, .len = data_len };
    mp3_stream_init(&s, mem_read, &m);
    size_t total = 0, n = 0;
    *calls = 0;
    do {
        size_t want = chunk;
        if (want > sizeof(out) / sizeof(out[0])) {
            want = sizeof(out) / sizeof(out[0]);
        }
        n = mp3_stream_read(&s, out, want);
        total += n;
        (*calls)++;
    } while (n > 0 && *calls < 100000);
    return total;
}

void setUp(void)
{
    data_len = 0;
}

void tearDown(void) {}

void test_mono_frames_decode(void)
{
    add_frames(20, false);
    size_t calls;
    size_t total = read_all(4096, &calls);
    TEST_ASSERT_EQUAL_INT(44100, mp3_stream_sample_rate(&s));
    TEST_ASSERT_EQUAL_size_t(20 * 1152, total);
    TEST_ASSERT_EQUAL_INT(MP3_EOF, mp3_stream_status(&s));
}

void test_stereo_downmixed_to_mono(void)
{
    add_frames(10, true);
    size_t calls;
    TEST_ASSERT_EQUAL_size_t(10 * 1152, read_all(4096, &calls));   /* 채널당 1152 → 모노 1152 */
}

void test_id3_tag_skipped(void)
{
    add_id3(3000);
    add_frames(10, false);
    size_t calls;
    TEST_ASSERT_EQUAL_size_t(10 * 1152, read_all(4096, &calls));
    TEST_ASSERT_EQUAL_INT(MP3_EOF, mp3_stream_status(&s));
}

void test_small_reads_equal_big_read(void)
{
    add_frames(5, false);
    size_t calls_small, calls_big;
    size_t small = read_all(7, &calls_small);
    size_t big = read_all(4096, &calls_big);
    TEST_ASSERT_EQUAL_size_t(big, small);
    TEST_ASSERT_GREATER_THAN_size_t(calls_big, calls_small);
}

/* Review Focus 1: MP3가 아닌 파일 → 오류(소리 없이 멈춰 있지 않다) */
void test_not_mp3_reports_error(void)
{
    for (int i = 0; i < 1000; i++) {
        memcpy(data + data_len, "this is not an mp3 file\n", 24);
        data_len += 24;
    }
    size_t calls;
    TEST_ASSERT_EQUAL_size_t(0, read_all(4096, &calls));
    TEST_ASSERT_EQUAL_INT(MP3_ERR_NO_FRAMES, mp3_stream_status(&s));
    TEST_ASSERT_EQUAL_INT(0, mp3_stream_sample_rate(&s));
}

/* 짧은 쓰레기 파일도 끝나면 오류다(EOF가 아니다) */
void test_short_garbage_is_error(void)
{
    add_bytes('Z', 500);
    size_t calls;
    TEST_ASSERT_EQUAL_size_t(0, read_all(4096, &calls));
    TEST_ASSERT_EQUAL_INT(MP3_ERR_NO_FRAMES, mp3_stream_status(&s));
}

/* Review Focus 1: 정상 프레임 뒤가 잘리고 쓰레기가 붙어도 멈추지 않고 끝난다.
 * minimp3는 동기를 잡을 때 뒤따르는 프레임 헤더 여러 개를 확인하므로, 쓰레기 바로 앞 몇 프레임은
 * 버려질 수 있다(곡 끝 0.3초 이내). 그래서 앞쪽 절반 이상이 나오는지만 본다 */
void test_truncated_file_ends_without_hang(void)
{
    add_frames(30, false);
    data_len -= 200;            /* 마지막 프레임 일부 잘림 */
    add_bytes('Z', 3000);
    size_t calls;
    size_t total = read_all(4096, &calls);
    TEST_ASSERT_LESS_THAN_size_t(1000, calls);
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(15 * 1152, total);
    TEST_ASSERT_EQUAL_INT(MP3_EOF, mp3_stream_status(&s));
}

/* 프레임 사이에 16KB 넘는 쓰레기가 끼면 오류로 본다 */
void test_long_garbage_after_frames_is_error(void)
{
    add_frames(4, false);
    add_bytes('Z', MP3_NOFRAME_LIMIT + 4096);
    add_frames(4, false);
    size_t calls;
    size_t total = read_all(4096, &calls);
    TEST_ASSERT_EQUAL_INT(MP3_ERR_NO_FRAMES, mp3_stream_status(&s));
    TEST_ASSERT_LESS_THAN_size_t(8 * 1152, total);
}

void test_empty_input_is_error(void)
{
    size_t calls;
    TEST_ASSERT_EQUAL_size_t(0, read_all(4096, &calls));
    TEST_ASSERT_EQUAL_INT(MP3_ERR_NO_FRAMES, mp3_stream_status(&s));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mono_frames_decode);
    RUN_TEST(test_stereo_downmixed_to_mono);
    RUN_TEST(test_id3_tag_skipped);
    RUN_TEST(test_small_reads_equal_big_read);
    RUN_TEST(test_not_mp3_reports_error);
    RUN_TEST(test_short_garbage_is_error);
    RUN_TEST(test_truncated_file_ends_without_hang);
    RUN_TEST(test_long_garbage_after_frames_is_error);
    RUN_TEST(test_empty_input_is_error);
    return UNITY_END();
}
