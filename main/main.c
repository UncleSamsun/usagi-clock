#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "alarm_core.h"
#include "alarm_service_esp.h"
#include "app_config.h"
#include "audio.h"
#include "board_pins.h"
#include "button_fsm.h"
#include "dev_console_esp.h"
#include "display_ui.h"
#include "ds3231.h"
#include "net.h"
#include "sdcard.h"
#include "timekeeper.h"
#include "web_api.h"

static const char *TAG = "usagi";

#define OTA_CONFIRM_US (60 * 1000000LL)   /* 새 펌웨어가 60초 동안 살아 있으면 롤백 취소 */

static void log_pins(void)
{
    ESP_LOGI(TAG, "pins: LCD sclk=%d mosi=%d miso=%d cs=%d dc=%d rst=%d bl=%d",
             PIN_LCD_SCLK, PIN_LCD_MOSI, PIN_LCD_MISO, PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST, PIN_LCD_BL);
    ESP_LOGI(TAG, "pins: SD sck=%d mosi=%d miso=%d cs=%d", PIN_SD_SCK, PIN_SD_MOSI, PIN_SD_MISO, PIN_SD_CS);
    ESP_LOGI(TAG, "pins: I2C sda=%d scl=%d", PIN_I2C_SDA, PIN_I2C_SCL);
    ESP_LOGI(TAG, "pins: I2S bclk=%d ws=%d dout=%d", PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT);
    ESP_LOGI(TAG, "pins: button=%d", PIN_BUTTON);
}

/* ===== 연결 함수들 ===== */

static void on_button(button_event_t ev, void *ctx)
{
    (void)ctx;
    alarm_service_post_input(ev == BUTTON_EV_LONG ? ALARM_INPUT_LONG : ALARM_INPUT_SHORT);
}

static uint8_t brightness_fn(int64_t now_local, bool ringing)
{
    app_settings_t s = app_settings_get();
    return app_brightness(&s, now_local, ringing);
}

/* 화면 상태에 Wi-Fi 표시와 설정 모드 안내를 더한다 */
static void ui_decorate(void *p)
{
    ui_state_t *ui = p;
    net_status_t ns = net_status();
    web_server_set_captive(ns.mode != NET_MODE_STA);   /* 설정 모드일 때만 모르는 경로를 설정 페이지로 */
    if (ns.mode != NET_MODE_STA) {
        ui->wifi = UI_WIFI_AP;
        if (ui->mode == UI_MODE_CLOCK) {
            ui->mode = UI_MODE_SETUP;
            snprintf(ui->line1, sizeof(ui->line1), "%s", ns.ap_ssid);
            snprintf(ui->line2, sizeof(ui->line2), "PW %s", ns.ap_pass);
        }
    } else {
        ui->wifi = ns.sta_connected ? UI_WIFI_CONNECTED : UI_WIFI_CONNECTING;
    }
}

static void on_alarms_changed(void)
{
    storage_request_save_alarms();
}

static void apply_settings(const app_settings_t *s)
{
    alarm_service_set_settings(&s->alarm);
    alarm_service_set_fallback_volume(s->default_volume);
    button_gpio_set_long_ms(s->alarm.long_press_ms);
    net_set_hostname(s->device_name);
}

static bool session_active(void)
{
    return alarm_service_view().state != ALARM_STATE_IDLE;
}

static void ota_confirm_cb(void *arg)
{
    (void)arg;
    web_ota_mark_valid_if_pending();
}

/* 부팅 때 버튼이 눌려 있으면 최대 10초 기다리며 남은 초를 보여 준다(설계 5.5) */
static boot_hold_t check_boot_hold(void)
{
    if (!button_gpio_raw_pressed(PIN_BUTTON)) {
        return BOOT_HOLD_NONE;
    }
    int64_t t0 = esp_timer_get_time();
    uint32_t held = 0;
    while (button_gpio_raw_pressed(PIN_BUTTON) && held < BOOT_HOLD_FACTORY_MS) {
        ui_state_t ui = { .time_known = false, .mode = UI_MODE_SETUP, .next_alarm_local = -1 };
        snprintf(ui.line1, sizeof(ui.line1), "RELEASE: SETUP MODE");
        snprintf(ui.line2, sizeof(ui.line2), "HOLD %lus: FACTORY RESET",
                 (unsigned long)((BOOT_HOLD_FACTORY_MS - held + 999) / 1000));
        display_ui_post(&ui);
        vTaskDelay(pdMS_TO_TICKS(100));
        held = (uint32_t)((esp_timer_get_time() - t0) / 1000);
    }
    return boot_hold_classify(true, held);
}

void app_main(void)
{
    ESP_LOGI(TAG, "boot (max alarms: %d)", ALARM_MAX);
    log_pins();

    /* 1) 저장소와 설정 */
    ESP_ERROR_CHECK(storage_init());
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    app_settings_t settings = app_settings_default(mac);
    storage_load_settings(&settings);
    app_settings_init(&settings, apply_settings);   /* 바꿀 때 적용 함수는 아래 서비스들이 시작된 뒤에만 불린다 */

    /* 2) 시간 */
    ds3231_t *rtc = NULL;
    if (ds3231_init(PIN_I2C_SDA, PIN_I2C_SCL, &rtc) != ESP_OK) {
        ESP_LOGE(TAG, "RTC init failed");
        rtc = NULL;
    }
    timekeeper_init(rtc);

    /* 3) 화면, 부팅 버튼(설정 모드·공장 초기화) */
    const display_ui_pins_t lcd = {
        .sclk = PIN_LCD_SCLK, .mosi = PIN_LCD_MOSI, .miso = PIN_LCD_MISO,
        .cs = PIN_LCD_CS, .dc = PIN_LCD_DC, .rst = PIN_LCD_RST, .bl = PIN_LCD_BL,
    };
    if (display_ui_start(&lcd) != ESP_OK) {
        ESP_LOGE(TAG, "display init failed (alarms still work)");
    }
    boot_hold_t hold = check_boot_hold();
    if (hold == BOOT_HOLD_FACTORY_RESET) {
        ESP_LOGW(TAG, "factory reset: clearing settings, alarms and Wi-Fi (SD files kept)");
        storage_factory_reset();
        esp_restart();
    }

    /* 4) 소리 */
    const sdcard_pins_t sd = { .sck = PIN_SD_SCK, .mosi = PIN_SD_MOSI, .miso = PIN_SD_MISO, .cs = PIN_SD_CS };
    sdcard_start(&sd);
    if (audio_start(PIN_I2S_BCLK, PIN_I2S_WS, PIN_I2S_DOUT) != ESP_OK) {
        ESP_LOGE(TAG, "audio init failed");
    }

    /* 5) 알람: 저장된 목록으로 시작. 네트워크와 무관하게 먼저 돈다 */
    alarm_service_start(&settings.alarm);
    alarm_service_set_fallback_volume(settings.default_volume);
    alarm_service_set_brightness_fn(brightness_fn);
    static alarm_t saved[ALARM_MAX];
    size_t n = storage_load_alarms(saved, ALARM_MAX);
    if (n > 0 && alarm_service_set_alarms(saved, n) != ESP_OK) {
        ESP_LOGW(TAG, "saved alarms rejected");
    }
    storage_set_alarm_source(alarm_service_get_alarms);
    alarm_service_set_change_cb(on_alarms_changed);
    button_gpio_start(PIN_BUTTON, settings.alarm.long_press_ms, on_button, NULL);

    /* 6) 네트워크와 웹 */
    if (net_start(hold == BOOT_HOLD_SETUP, settings.device_name) != ESP_OK) {
        ESP_LOGE(TAG, "network start failed (alarms still work)");
    }
    alarm_service_set_ui_decorator(ui_decorate);
    static const web_hooks_t hooks = {
        .set_alarms = alarm_service_set_alarms,
        .get_alarms = alarm_service_get_alarms,
        .session_active = session_active,
    };
    web_server_start(&hooks);

    /* 7) 웹 업데이트 뒤 첫 부팅이면 60초 뒤 정상 확인 */
    const esp_timer_create_args_t t = { .callback = ota_confirm_cb, .name = "ota_ok" };
    esp_timer_handle_t timer;
    if (esp_timer_create(&t, &timer) == ESP_OK) {
        esp_timer_start_once(timer, OTA_CONFIRM_US);
    }

    dev_console_start();
}
