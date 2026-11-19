#ifndef ALARM_SERVICE_ESP_H
#define ALARM_SERVICE_ESP_H

/*
 * 알람 서비스: 알람 엔진(alarm_core)을 소유하는 유일한 곳.
 *   - 1초 경계마다 엔진 틱, 버튼 입력은 큐로 받는다.
 *   - 엔진 출력을 오디오 명령과 화면 상태로 바꾼다.
 *   - 다른 작업(콘솔, 웹)은 아래 함수로만 엔진에 접근한다(뮤텍스로 보호).
 */

#include <stddef.h>

#include "alarm_core.h"
#include "esp_err.h"

/* 엔진 init, 작업 생성, 작업 워치독 등록 */
void         alarm_service_start(const alarm_settings_t *s);
/* 버튼 콜백에서 부른다. 길이 4 큐 */
void         alarm_service_post_input(alarm_input_t in);
/* 하나라도 alarm_validate 실패면 ESP_ERR_INVALID_ARG, 엔진은 그대로 */
esp_err_t    alarm_service_set_alarms(const alarm_t *a, size_t n);
size_t       alarm_service_get_alarms(alarm_t *out, size_t max);
alarm_view_t alarm_service_view(void);
/* 설정(스누즈·길게 누르기·자동 정지) 바꾸기. 진행 중 회차는 유지 */
void         alarm_service_set_settings(const alarm_settings_t *s);

/* 알람 목록이 바뀌었을 때(설정 변경, 한 번만 알람 비활성화) 부를 함수. 저장용(계획 4) */
typedef void (*alarm_service_change_cb_t)(void);
void         alarm_service_set_change_cb(alarm_service_change_cb_t cb);

/* 화면 밝기를 정하는 함수(계획 4의 야간 밝기). 없으면 울릴 때 100, 평소 80 */
typedef uint8_t (*alarm_service_brightness_fn_t)(int64_t now_local, bool ringing);
void         alarm_service_set_brightness_fn(alarm_service_brightness_fn_t fn);

/* 화면 상태에 덧붙일 정보(Wi-Fi 표시, 설정 모드 안내). 계획 4 */
typedef void (*alarm_service_ui_decorator_t)(void *ui_state);
void         alarm_service_set_ui_decorator(alarm_service_ui_decorator_t fn);

/* 알람 목록에서 지워진 알람이 울릴 때 쓸 볼륨(계획 4의 기본 볼륨). 기본 60 */
void         alarm_service_set_fallback_volume(uint8_t vol);

#endif /* ALARM_SERVICE_ESP_H */
