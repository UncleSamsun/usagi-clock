#include <stdlib.h>

#include "unity.h"

#include "audio.h"

static int16_t buf[AUDIO_SAMPLE_RATE * 2];

void setUp(void) {}
void tearDown(void) {}

static int peak(const int16_t *b, size_t from, size_t to)
{
    int p = 0;
    for (size_t i = from; i < to; i++) {
        if (abs(b[i]) > p) {
            p = abs(b[i]);
        }
    }
    return p;
}

/* 짧은 비프: 80ms(1280샘플) 뒤 끝난다 */
void test_tick_length(void)
{
    beep_t b;
    beep_start(&b, BEEP_TICK, 100);
    size_t got = beep_fill(&b, buf, 4000);
    TEST_ASSERT_EQUAL_size_t(1280, got);
    TEST_ASSERT_GREATER_THAN_INT(AUDIO_PEAK_MAX / 2, peak(buf, 0, got));
    TEST_ASSERT_EQUAL_size_t(0, beep_fill(&b, buf, 100));
}

/* 확인음: 60ms 소리, 60ms 쉼, 60ms 소리 = 2880샘플 */
void test_confirm_shape(void)
{
    beep_t b;
    beep_start(&b, BEEP_CONFIRM, 100);
    size_t got = beep_fill(&b, buf, 10000);
    TEST_ASSERT_EQUAL_size_t(2880, got);
    TEST_ASSERT_GREATER_THAN_INT(0, peak(buf, 0, 960));
    TEST_ASSERT_EQUAL_INT(0, peak(buf, 960, 1920));
    TEST_ASSERT_GREATER_THAN_INT(0, peak(buf, 1920, 2880));
}

/* 알람 비프: 0.25초 소리, 0.25초 쉼을 끝없이 반복 */
void test_alarm_loop_repeats(void)
{
    beep_t b;
    beep_start(&b, BEEP_ALARM_LOOP, 100);
    TEST_ASSERT_EQUAL_size_t(AUDIO_SAMPLE_RATE * 2, beep_fill(&b, buf, AUDIO_SAMPLE_RATE * 2));
    TEST_ASSERT_GREATER_THAN_INT(0, peak(buf, 0, 4000));
    TEST_ASSERT_EQUAL_INT(0, peak(buf, 4000, 8000));
    TEST_ASSERT_GREATER_THAN_INT(0, peak(buf, 8000, 12000));
    TEST_ASSERT_EQUAL_INT(0, peak(buf, 28000, 32000));
}

/* 작게 나눠 채워도 한 번에 채운 것과 같다 */
void test_fill_in_pieces(void)
{
    static int16_t whole[2880], parts[2880];
    beep_t a, b;
    beep_start(&a, BEEP_CONFIRM, 60);
    beep_fill(&a, whole, 2880);
    beep_start(&b, BEEP_CONFIRM, 60);
    size_t pos = 0;
    while (pos < 2880) {
        size_t got = beep_fill(&b, parts + pos, 7);
        if (got == 0) {
            break;
        }
        pos += got;
    }
    TEST_ASSERT_EQUAL_size_t(2880, pos);
    TEST_ASSERT_EQUAL_INT16_ARRAY(whole, parts, 2880);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tick_length);
    RUN_TEST(test_confirm_shape);
    RUN_TEST(test_alarm_loop_repeats);
    RUN_TEST(test_fill_in_pieces);
    return UNITY_END();
}
