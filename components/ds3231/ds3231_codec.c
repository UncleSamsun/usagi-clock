#include "ds3231.h"

#include "alarm_core.h"

/* BCD 1바이트 → 0..99. 어느 자리든 9를 넘으면 -1 */
static int bcd_to_int(uint8_t v)
{
    int hi = v >> 4;
    int lo = v & 0x0F;
    if (hi > 9 || lo > 9) {
        return -1;
    }
    return hi * 10 + lo;
}

static uint8_t int_to_bcd(int v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

/*
 * 시 레지스터(0x02)
 *   bit6 = 1: 12시간 모드. bit5 = PM, bit4..0 = BCD 1..12
 *   bit6 = 0: 24시간 모드. bit5..0 = BCD 0..23
 * 12시간 모드에서 12 AM은 0시, 12 PM은 12시다.
 */
static int decode_hour(uint8_t v)
{
    if (v & 0x40) {
        int h12 = bcd_to_int(v & 0x1F);
        if (h12 < 1 || h12 > 12) {
            return -1;
        }
        bool pm = (v & 0x20) != 0;
        return (h12 % 12) + (pm ? 12 : 0);
    }
    int h = bcd_to_int(v & 0x3F);
    return (h >= 0 && h <= 23) ? h : -1;
}

bool ds3231_decode(const uint8_t regs[7], int64_t *utc_s)
{
    int sec = bcd_to_int(regs[0] & 0x7F);
    int min = bcd_to_int(regs[1] & 0x7F);
    int hour = decode_hour(regs[2]);
    int day = bcd_to_int(regs[4] & 0x3F);
    int month = bcd_to_int(regs[5] & 0x1F);   /* bit7 century는 무시 */
    int year = bcd_to_int(regs[6]);

    if (sec < 0 || sec > 59 || min < 0 || min > 59 || hour < 0 || year < 0) {
        return false;
    }
    if (month < 1 || month > 12) {
        return false;
    }
    if (day < 1 || day > alarm_days_in_month(2000 + year, month)) {
        return false;
    }

    alarm_civil_t c = {
        .year = 2000 + year,
        .month = (uint8_t)month,
        .day = (uint8_t)day,
        .hour = (uint8_t)hour,
        .minute = (uint8_t)min,
        .second = (uint8_t)sec,
    };
    *utc_s = alarm_epoch_from_civil(&c);
    return true;
}

bool ds3231_encode(int64_t utc_s, uint8_t regs[7])
{
    alarm_civil_t c;
    alarm_civil_from_epoch(utc_s, &c);
    if (c.year < 2000 || c.year > 2099) {
        return false;
    }

    regs[0] = int_to_bcd(c.second);
    regs[1] = int_to_bcd(c.minute);
    regs[2] = int_to_bcd(c.hour);          /* bit6 = 0: 24시간 모드 */
    regs[3] = (uint8_t)(c.weekday + 1);    /* 1=월 .. 7=일 */
    regs[4] = int_to_bcd(c.day);
    regs[5] = int_to_bcd(c.month);
    regs[6] = int_to_bcd(c.year - 2000);
    return true;
}

bool ds3231_status_time_valid(uint8_t status_reg)
{
    return (status_reg & DS3231_STATUS_OSF) == 0;
}
