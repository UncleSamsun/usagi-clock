#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

/*
 * 화면: ILI9341(320×240 가로) + LVGL. 다른 작업은 display_ui_post로 상태만 넘기고,
 * 그리기는 화면 작업 하나가 LVGL 잠금 안에서 한다.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { UI_MODE_CLOCK = 0, UI_MODE_PRE, UI_MODE_RINGING, UI_MODE_SNOOZED, UI_MODE_SETUP } ui_mode_t;

typedef enum { UI_WIFI_NONE = 0, UI_WIFI_CONNECTING, UI_WIFI_CONNECTED, UI_WIFI_AP } ui_wifi_t;

typedef struct {
    bool      time_known;
    int64_t   now_local;
    int64_t   next_alarm_local;   /* 없으면 -1 */
    ui_mode_t mode;
    int64_t   ring_at;            /* PRE: 남은 시간 계산용 */
    int64_t   snooze_until;       /* SNOOZED */
    char      label[24];          /* 울리는 알람 이름 */
    ui_wifi_t wifi;
    char      line1[40];          /* SETUP: 안내 문구(SSID 등) */
    char      line2[40];
} ui_state_t;

typedef struct { int sclk, mosi, miso, cs, dc, rst, bl; } display_ui_pins_t;

esp_err_t display_ui_start(const display_ui_pins_t *pins);   /* LCD·LVGL·백라이트 초기화, 화면 작업 시작 */
void      display_ui_post(const ui_state_t *s);              /* 최신 상태 하나만 보관(덮어쓰기). 어느 작업에서 불러도 됨 */
void      display_ui_set_brightness(uint8_t pct);            /* 0..100 */

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_UI_H */
