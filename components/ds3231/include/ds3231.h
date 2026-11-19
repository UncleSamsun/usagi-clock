#ifndef DS3231_H
#define DS3231_H

/*
 * DS3231 RTC 드라이버.
 *
 * 순수 부분(ds3231_codec.c): BCD 레지스터 ↔ UTC epoch 초. PC에서 테스트한다.
 * ESP 부분(ds3231_esp.c):     I2C로 레지스터를 읽고 쓴다.
 *
 * RTC에는 항상 UTC를 저장한다(로컬 시각 변환은 alarm_local_from_utc).
 * DS3231은 2000~2099년만 표현한다. 월 레지스터의 century 비트는 쓰지 않는다.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DS3231_I2C_ADDR   0x68
#define DS3231_REG_TIME   0x00   /* 0x00..0x06: 초, 분, 시, 요일, 일, 월, 년 */
#define DS3231_REG_STATUS 0x0F
#define DS3231_STATUS_OSF 0x80   /* 발진이 멈춘 적이 있음 → 시각을 믿을 수 없음 */

/* 레지스터 7바이트 → UTC epoch 초. BCD나 날짜가 범위 밖이면 false. 12시간 모드도 읽는다 */
bool ds3231_decode(const uint8_t regs[7], int64_t *utc_s);
/* UTC epoch 초 → 레지스터 7바이트. 2000..2099년 밖이면 false. 항상 24시간 모드, 요일 = weekday+1(월=1) */
bool ds3231_encode(int64_t utc_s, uint8_t regs[7]);
/* 상태 레지스터의 OSF가 꺼져 있으면 true */
bool ds3231_status_time_valid(uint8_t status_reg);

#ifdef ESP_PLATFORM
#include "esp_err.h"

typedef struct ds3231 ds3231_t;

/* I2C0 마스터(100kHz)를 만들고 장치를 붙인다. 장치가 응답하지 않아도 핸들은 돌려준다 */
esp_err_t ds3231_init(int sda_gpio, int scl_gpio, ds3231_t **out);
/* OSF 켜짐 → ESP_ERR_INVALID_STATE, 레지스터 값이 이상하면 ESP_ERR_INVALID_RESPONSE */
esp_err_t ds3231_read_utc(ds3231_t *d, int64_t *utc_s);
/* 시각을 쓰고 OSF를 지운다 */
esp_err_t ds3231_write_utc(ds3231_t *d, int64_t utc_s);
#endif

#ifdef __cplusplus
}
#endif

#endif /* DS3231_H */
