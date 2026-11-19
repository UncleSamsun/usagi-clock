#ifndef WEB_API_H
#define WEB_API_H

/*
 * 웹 API.
 *   순수 부분: JSON ↔ 구조체와 값 검사(web_json.c), 업로드 파일 이름 정리(upload_name.c)
 *   ESP 부분:  esp_http_server 라우팅·정적 파일(web_server_esp.c), 소리 파일(web_sounds_esp.c), OTA(web_ota_esp.c)
 *
 * 웹에는 인증이 없으므로(설계 5.5 MVP) 들어오는 값은 모두 여기서 엄격히 검사한다.
 *
 * 알람 JSON: {"id":1,"enabled":true,"hour":7,"minute":0,"days":127,"pre_minutes":5,
 *            "pre_volume":70,"main_volume":70,"label":"출근","clipset":"default","song":"a.mp3"}
 *   hour는 0..23(12시간 표시는 웹 페이지가 한다), days는 비트마스크(bit0=월).
 *   문자열 길이는 UTF-8 바이트: label 23, clipset 31, song 63 이하.
 * 설정 JSON: {"snooze_min":5,"long_press_ms":2000,"auto_stop_min":10,"default_volume":70,
 *            "day_brightness":80,"night_brightness":10,"night_start_min":1380,"night_end_min":420,
 *            "device_name":"usagi-mj"}
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "alarm_core.h"
#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WEB_JSON_BODY_MAX 4096
#define WEB_UPLOAD_MAX    (20 * 1024 * 1024)   /* 설계 5.5: 파일당 20MB */

/* 알람 JSON(모든 필드 필요, POST에서는 id 무시) → 구조체. 실패하면 false, err에 영어 한 줄 */
bool    web_alarm_from_json(const char *json, alarm_t *out, char *err, size_t err_n);
/* 설정 JSON의 있는 필드만 base에 덮어써 out에 담는다(부분 수정) */
bool    web_settings_from_json(const char *json, const app_settings_t *base, app_settings_t *out,
                               char *err, size_t err_n);
size_t  web_alarm_to_json(const alarm_t *a, char *out, size_t n);                    /* 쓴 길이, 모자라면 0 */
size_t  web_alarms_to_json(const alarm_t *a, size_t count, char *out, size_t n);     /* {"alarms":[…]} */
size_t  web_settings_to_json(const app_settings_t *s, char *out, size_t n);
uint8_t web_pick_alarm_id(const alarm_t *a, size_t count);                            /* 안 쓰는 가장 작은 1..255, 없으면 0 */

/* {"ssid":"..","pass":".."} → 길이 검사(ssid 1..32, pass 0 또는 8..63 바이트) */
bool    web_wifi_from_json(const char *json, char ssid[33], char pass[65], char *err, size_t err_n);
/* {"utc":<초>} */
bool    web_time_from_json(const char *json, int64_t *utc, char *err, size_t err_n);

/* 업로드 파일 이름을 FAT에 안전한 이름으로: \ / : * ? " < > | 와 제어 문자 → '_',
 * 앞뒤 공백·점 제거, 확장자 .mp3로 맞춤(없으면 붙임), 63바이트 이하(UTF-8 글자 경계에서 자름).
 * 결과가 audio_name_ok가 아니면 false */
bool    web_sanitize_upload_name(const char *in, char *out, size_t n);

/* URL 퍼센트 인코딩 풀기(%XX, '+'는 공백). 잘못된 인코딩이면 false */
bool    web_url_decode(const char *in, char *out, size_t n);

#ifdef ESP_PLATFORM
#include "esp_err.h"

/* 알람 목록 저장 요청 등 웹이 부르는 바깥 동작 */
typedef struct {
    esp_err_t (*set_alarms)(const alarm_t *a, size_t n);   /* 검사 → 엔진 → 저장 */
    size_t    (*get_alarms)(alarm_t *out, size_t max);
    bool      (*session_active)(void);                       /* 회차 진행 중이면 미리 듣기·OTA 거절 */
} web_hooks_t;

esp_err_t web_server_start(const web_hooks_t *hooks);
/* 설정 모드: 알 수 없는 경로를 http://192.168.4.1/ 로 보낸다(캡티브 포털) */
void      web_server_set_captive(bool on);
/* 새 펌웨어가 정상인지 확인되면 부른다(롤백 취소) */
void      web_ota_mark_valid_if_pending(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* WEB_API_H */
