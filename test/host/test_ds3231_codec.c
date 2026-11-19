#include <string.h>

#include "unity.h"

#include "ds3231.h"

/* 2026-11-15 22:00:00 UTC = 2026-11-16(월) 07:00 KST */
#define UTC_1115_2200 1794780000LL
/* 2028-02-29 23:59:59 UTC (윤일) */
#define UTC_LEAP_DAY 1835481599LL

static const uint8_t REGS_1115_2200[7] = { 0x00, 0x00, 0x22, 0x07, 0x15, 0x11, 0x26 };

void setUp(void) {}
void tearDown(void) {}

void test_decode_known_time(void)
{
    int64_t utc = 0;
    TEST_ASSERT_TRUE(ds3231_decode(REGS_1115_2200, &utc));
    TEST_ASSERT_EQUAL_INT64(UTC_1115_2200, utc);
}

void test_encode_known_time(void)
{
    uint8_t regs[7] = {0};
    TEST_ASSERT_TRUE(ds3231_encode(UTC_1115_2200, regs));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(REGS_1115_2200, regs, 7);   /* 요일 7 = 일요일 */
}

void test_encode_leap_day(void)
{
    uint8_t regs[7] = {0};
    TEST_ASSERT_TRUE(ds3231_encode(UTC_LEAP_DAY, regs));
    TEST_ASSERT_EQUAL_HEX8(0x59, regs[0]);
    TEST_ASSERT_EQUAL_HEX8(0x59, regs[1]);
    TEST_ASSERT_EQUAL_HEX8(0x23, regs[2]);
    TEST_ASSERT_EQUAL_HEX8(0x29, regs[4]);
    TEST_ASSERT_EQUAL_HEX8(0x02, regs[5]);
    TEST_ASSERT_EQUAL_HEX8(0x28, regs[6]);

    int64_t utc = 0;
    TEST_ASSERT_TRUE(ds3231_decode(regs, &utc));
    TEST_ASSERT_EQUAL_INT64(UTC_LEAP_DAY, utc);
}

/* Review Focus 3: 다른 라이브러리로 12시간 모드가 설정된 모듈 */
void test_decode_12h_mode(void)
{
    uint8_t regs[7];
    int64_t utc = 0;
    memcpy(regs, REGS_1115_2200, 7);

    regs[2] = 0x70;   /* 12시간 모드, PM, 10 → 22시 */
    TEST_ASSERT_TRUE(ds3231_decode(regs, &utc));
    TEST_ASSERT_EQUAL_INT64(UTC_1115_2200, utc);

    regs[2] = 0x52;   /* 12시간 모드, AM, 12 → 0시 */
    TEST_ASSERT_TRUE(ds3231_decode(regs, &utc));
    TEST_ASSERT_EQUAL_INT64(UTC_1115_2200 - 22 * 3600, utc);

    regs[2] = 0x72;   /* 12시간 모드, PM, 12 → 12시 */
    TEST_ASSERT_TRUE(ds3231_decode(regs, &utc));
    TEST_ASSERT_EQUAL_INT64(UTC_1115_2200 - 10 * 3600, utc);
}

void test_decode_rejects_invalid_bcd(void)
{
    static const struct { int reg; uint8_t value; } bad[] = {
        { 0, 0x5A },   /* 초 A는 BCD 아님 */
        { 1, 0x60 },   /* 분 60 */
        { 2, 0x24 },   /* 24시 */
        { 2, 0x40 },   /* 12시간 모드 0시 */
        { 4, 0x00 },   /* 일 0 */
        { 4, 0x32 },   /* 일 32 */
        { 5, 0x13 },   /* 월 13 */
        { 5, 0x00 },   /* 월 0 */
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        uint8_t regs[7];
        int64_t utc = 0;
        memcpy(regs, REGS_1115_2200, 7);
        regs[bad[i].reg] = bad[i].value;
        TEST_ASSERT_FALSE_MESSAGE(ds3231_decode(regs, &utc), "invalid register accepted");
    }
}

void test_decode_rejects_impossible_date(void)
{
    uint8_t regs[7];
    int64_t utc = 0;
    memcpy(regs, REGS_1115_2200, 7);
    regs[4] = 0x31;   /* 11월 31일 */
    TEST_ASSERT_FALSE(ds3231_decode(regs, &utc));

    regs[4] = 0x29;   /* 2026-02-29 (윤년 아님) */
    regs[5] = 0x02;
    TEST_ASSERT_FALSE(ds3231_decode(regs, &utc));
}

void test_encode_rejects_out_of_range_year(void)
{
    uint8_t regs[7];
    TEST_ASSERT_FALSE(ds3231_encode(946684799LL, regs));    /* 1999-12-31 23:59:59 */
    TEST_ASSERT_FALSE(ds3231_encode(4102444800LL, regs));   /* 2100-01-01 00:00:00 */
    TEST_ASSERT_TRUE(ds3231_encode(946684800LL, regs));     /* 2000-01-01 00:00:00 */
}

/* Review Focus 1: 발진 정지 플래그가 켜져 있으면 시간을 믿지 않는다 */
void test_osf_set_means_invalid(void)
{
    TEST_ASSERT_FALSE(ds3231_status_time_valid(0x88));
    TEST_ASSERT_FALSE(ds3231_status_time_valid(0x80));
    TEST_ASSERT_TRUE(ds3231_status_time_valid(0x08));
    TEST_ASSERT_TRUE(ds3231_status_time_valid(0x00));
}

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_decode_known_time);
    RUN_TEST(test_encode_known_time);
    RUN_TEST(test_encode_leap_day);
    RUN_TEST(test_decode_12h_mode);
    RUN_TEST(test_decode_rejects_invalid_bcd);
    RUN_TEST(test_decode_rejects_impossible_date);
    RUN_TEST(test_encode_rejects_out_of_range_year);
    RUN_TEST(test_osf_set_means_invalid);

    return UNITY_END();
}
