#ifndef TIMEKEEPER_H
#define TIMEKEEPER_H

/*
 * 시간 관리: RTC(DS3231) ↔ 시스템 시간(UTC) ↔ 수동 설정·SNTP.
 *
 * "시간을 안다" = 부팅 때 RTC 값이 유효했거나, 그 뒤 timekeeper_set_utc가 성공했다.
 * 시간을 모르면 화면은 --:--를 보이고 알람 틱을 돌리지 않는다(설계 5.1).
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TIME_VALID_MIN_UTC 1767225600LL   /* 2026-01-01 00:00:00 UTC */
#define TIME_VALID_END_UTC 4102444800LL   /* 2100-01-01 00:00:00 UTC (미만) */

/* 알람을 돌려도 될 만한 시각인지(위 범위 안) */
bool timekeeper_utc_plausible(int64_t utc_s);

#ifdef ESP_PLATFORM
#include "esp_err.h"
#include "ds3231.h"

/* 부팅 때 1회: RTC 읽기 → 유효하면 시스템 시간 설정. rtc가 NULL이면 RTC 없이 동작 */
void      timekeeper_init(ds3231_t *rtc);
bool      timekeeper_is_known(void);
/* 지금 로컬(KST) epoch 초. 시간을 모르면 false */
bool      timekeeper_now_local(int64_t *local_s);
/* 범위 검사 → 시스템 시간 설정 → RTC에 기록. 범위 밖이면 ESP_ERR_INVALID_ARG */
esp_err_t timekeeper_set_utc(int64_t utc_s);
#endif

#ifdef __cplusplus
}
#endif

#endif /* TIMEKEEPER_H */
