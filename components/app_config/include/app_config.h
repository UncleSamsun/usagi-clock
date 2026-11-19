#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/*
 * 앱 설정(순수 C): 값, 기본값, 범위 검사, 야간 밝기.
 * 저장(NVS)은 storage_esp.c, 저장 형식은 store_codec.h.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "alarm_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_DEVICE_NAME_MAX 24

typedef struct {
    alarm_settings_t alarm;          /* 스누즈, 길게 누르기, 자동 정지 (계획 1) */
    uint8_t  default_volume;         /* 0..100, 새 알람의 기본 볼륨 */
    uint8_t  day_brightness;         /* 1..100 % */
    uint8_t  night_brightness;       /* 1..100 % */
    uint16_t night_start_min;        /* 0..1439 (자정부터 분) */
    uint16_t night_end_min;          /* 0..1439. start == end 이면 야간 없음 */
    char     device_name[APP_DEVICE_NAME_MAX + 1];
} app_settings_t;

/* 범위 [제안] */
#define APP_SNOOZE_MIN_MIN      1
#define APP_SNOOZE_MIN_MAX      30
#define APP_LONG_PRESS_MS_MIN   1000
#define APP_LONG_PRESS_MS_MAX   5000
#define APP_AUTO_STOP_MIN_MIN   1
#define APP_AUTO_STOP_MIN_MAX   60
#define APP_BRIGHTNESS_MIN      1
#define APP_MINUTES_PER_DAY     1440

/* device_name = "usagi-" + mac[4], mac[5] 소문자 hex */
app_settings_t app_settings_default(const uint8_t mac[6]);
bool    app_settings_validate(const app_settings_t *s);
/* a-z, 0-9, '-' 1..24자, '-'로 시작하거나 끝나지 않음 (mDNS 이름) */
bool    app_device_name_ok(const char *name);
bool    app_night_active(int64_t local_s, uint16_t start_min, uint16_t end_min);
/* 울리는 중이면 100, 야간이면 night, 아니면 day */
uint8_t app_brightness(const app_settings_t *s, int64_t local_s, bool ringing);

#ifdef ESP_PLATFORM
#include "esp_err.h"

esp_err_t storage_init(void);
void      storage_load_settings(app_settings_t *out);          /* 없거나 깨졌으면 기본값 + 경고 */
esp_err_t storage_save_settings(const app_settings_t *s);
size_t    storage_load_alarms(alarm_t *out, size_t max);       /* 없거나 깨졌으면 0 + 경고 */
esp_err_t storage_save_alarms(const alarm_t *a, size_t n);
/* 저장할 알람 목록을 꺼내 올 함수(알람 서비스). 처음 부를 때 저장 작업을 만든다 */
void      storage_set_alarm_source(size_t (*get)(alarm_t *out, size_t max));
/* 알람 저장 요청(저장 작업이 마지막 요청만 처리). 다른 작업에서 불러도 됨 */
void      storage_request_save_alarms(void);
esp_err_t storage_factory_reset(void);                          /* usagi 네임스페이스 지움 */

/* 현재 설정의 사본 */
app_settings_t app_settings_get(void);
/* 검사 → 저장 → 적용. 범위 밖이면 ESP_ERR_INVALID_ARG */
esp_err_t      app_settings_set(const app_settings_t *s);
/* 설정이 바뀌면 부를 함수(알람 서비스·버튼·mDNS 반영) */
typedef void (*app_settings_apply_fn_t)(const app_settings_t *s);
void           app_settings_init(const app_settings_t *loaded, app_settings_apply_fn_t apply);
#endif

#ifdef __cplusplus
}
#endif

#endif /* APP_CONFIG_H */
