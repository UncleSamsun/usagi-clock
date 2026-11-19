#include "app_config.h"

#include <stdio.h>
#include <string.h>

app_settings_t app_settings_default(const uint8_t mac[6])
{
    app_settings_t s;
    memset(&s, 0, sizeof(s));
    s.alarm = alarm_settings_default();
    s.default_volume = 70;
    s.day_brightness = 80;
    s.night_brightness = 10;
    s.night_start_min = 23 * 60;   /* 23:00 */
    s.night_end_min = 7 * 60;      /* 07:00 */
    snprintf(s.device_name, sizeof(s.device_name), "usagi-%02x%02x", mac[4], mac[5]);
    return s;
}

bool app_device_name_ok(const char *name)
{
    size_t len = 0;
    while (len <= APP_DEVICE_NAME_MAX && name[len] != '\0') {
        len++;
    }
    if (len == 0 || len > APP_DEVICE_NAME_MAX || name[0] == '-' || name[len - 1] == '-') {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

static bool in_range(int v, int lo, int hi)
{
    return v >= lo && v <= hi;
}

bool app_settings_validate(const app_settings_t *s)
{
    return in_range(s->alarm.snooze_min, APP_SNOOZE_MIN_MIN, APP_SNOOZE_MIN_MAX) &&
           in_range(s->alarm.long_press_ms, APP_LONG_PRESS_MS_MIN, APP_LONG_PRESS_MS_MAX) &&
           in_range(s->alarm.auto_stop_min, APP_AUTO_STOP_MIN_MIN, APP_AUTO_STOP_MIN_MAX) &&
           s->default_volume <= ALARM_VOLUME_MAX &&
           in_range(s->day_brightness, APP_BRIGHTNESS_MIN, 100) &&
           in_range(s->night_brightness, APP_BRIGHTNESS_MIN, 100) &&
           s->night_start_min < APP_MINUTES_PER_DAY && s->night_end_min < APP_MINUTES_PER_DAY &&
           app_device_name_ok(s->device_name);
}

bool app_night_active(int64_t local_s, uint16_t start_min, uint16_t end_min)
{
    if (start_min == end_min) {
        return false;
    }
    alarm_civil_t c;
    alarm_civil_from_epoch(local_s, &c);
    int m = c.hour * 60 + c.minute;
    if (start_min < end_min) {
        return m >= start_min && m < end_min;      /* 같은 날 안: 01:00~02:00 */
    }
    return m >= start_min || m < end_min;          /* 자정을 넘음: 23:00~07:00 */
}

uint8_t app_brightness(const app_settings_t *s, int64_t local_s, bool ringing)
{
    if (ringing) {
        return 100;   /* 알람이 울리는 동안은 야간 밝기를 무시한다(설계 5.4) */
    }
    return app_night_active(local_s, s->night_start_min, s->night_end_min) ? s->night_brightness
                                                                             : s->day_brightness;
}
