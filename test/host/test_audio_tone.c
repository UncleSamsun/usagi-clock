#include <stdlib.h>
#include <string.h>

#include "unity.h"

#include "audio.h"

static int16_t buf[AUDIO_SAMPLE_RATE];

void setUp(void) {}
void tearDown(void) {}

static int peak(const int16_t *b, size_t n)
{
    int p = 0;
    for (size_t i = 0; i < n; i++) {
        int v = abs(b[i]);
        if (v > p) {
            p = v;
        }
    }
    return p;
}

void test_volume_zero_is_silent(void)
{
    audio_tone_t t = {0};
    audio_tone_fill(&t, buf, 160, 1000, 0);
    TEST_ASSERT_EQUAL_INT(0, peak(buf, 160));
}

/* Review Focus 5: 볼륨 100(또는 그 이상)이어도 AUDIO_PEAK_MAX를 넘지 않는다 */
void test_tone_peak_capped_at_volume_100(void)
{
    audio_tone_t t = {0};
    audio_tone_fill(&t, buf, 1600, 1000, 100);
    int p100 = peak(buf, 1600);
    TEST_ASSERT_LESS_OR_EQUAL_INT(AUDIO_PEAK_MAX, p100);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(AUDIO_PEAK_MAX * 95 / 100, p100);

    audio_tone_t t2 = {0};
    audio_tone_fill(&t2, buf, 1600, 1000, 255);
    TEST_ASSERT_EQUAL_INT(p100, peak(buf, 1600));
}

void test_volume_scales_linearly(void)
{
    audio_tone_t t = {0};
    audio_tone_fill(&t, buf, 1600, 1000, 100);
    int p100 = peak(buf, 1600);
    audio_tone_t t2 = {0};
    audio_tone_fill(&t2, buf, 1600, 1000, 50);
    int p50 = peak(buf, 1600);
    TEST_ASSERT_INT_WITHIN(p100 * 5 / 100, p100 / 2, p50);
}

void test_frequency_by_zero_crossings(void)
{
    audio_tone_t t = {0};
    audio_tone_fill(&t, buf, AUDIO_SAMPLE_RATE, 1000, 100);
    int crossings = 0;
    for (size_t i = 1; i < AUDIO_SAMPLE_RATE; i++) {
        if (buf[i - 1] < 0 && buf[i] >= 0) {
            crossings++;
        }
    }
    TEST_ASSERT_INT_WITHIN(1, 1000, crossings);
}

void test_phase_continues_between_calls(void)
{
    static int16_t once[160], twice[160];
    audio_tone_t a = {0}, b = {0};
    audio_tone_fill(&a, once, 160, 440, 80);
    audio_tone_fill(&b, twice, 80, 440, 80);
    audio_tone_fill(&b, twice + 80, 80, 440, 80);
    TEST_ASSERT_EQUAL_INT16_ARRAY(once, twice, 160);
}

void test_apply_volume(void)
{
    int16_t a[3] = { 32767, -32768, 1000 };
    audio_apply_volume(a, 3, 100);
    TEST_ASSERT_LESS_OR_EQUAL_INT(AUDIO_PEAK_MAX, abs(a[0]));
    TEST_ASSERT_LESS_OR_EQUAL_INT(AUDIO_PEAK_MAX, abs(a[1]));
    TEST_ASSERT_GREATER_OR_EQUAL_INT(AUDIO_PEAK_MAX * 99 / 100, a[0]);

    int16_t z[3] = { 32767, -32768, 1000 };
    audio_apply_volume(z, 3, 0);
    TEST_ASSERT_EQUAL_INT16(0, z[0]);
    TEST_ASSERT_EQUAL_INT16(0, z[1]);

    int16_t b[3] = { 32767, -32768, 1000 };
    audio_apply_volume(b, 3, 200);
    TEST_ASSERT_EQUAL_INT16_ARRAY(a, b, 3);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_volume_zero_is_silent);
    RUN_TEST(test_tone_peak_capped_at_volume_100);
    RUN_TEST(test_volume_scales_linearly);
    RUN_TEST(test_frequency_by_zero_crossings);
    RUN_TEST(test_phase_continues_between_calls);
    RUN_TEST(test_apply_volume);
    return UNITY_END();
}
