#include "timekeeper.h"

#include <stdatomic.h>
#include <sys/time.h>

#include "alarm_core.h"
#include "esp_log.h"

static const char *TAG = "time";

static ds3231_t *s_rtc;
static atomic_bool s_known;

static void set_system_utc(int64_t utc_s)
{
    struct timeval tv = { .tv_sec = (time_t)utc_s, .tv_usec = 0 };
    settimeofday(&tv, NULL);
}

void timekeeper_init(ds3231_t *rtc)
{
    s_rtc = rtc;
    atomic_store(&s_known, false);

    if (rtc == NULL) {
        ESP_LOGW(TAG, "no RTC: time unknown until set");
        return;
    }

    int64_t utc = 0;
    esp_err_t err = ds3231_read_utc(rtc, &utc);
    if (err == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "RTC oscillator stopped (new module or battery removed): time unknown");
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "RTC read failed: %s", esp_err_to_name(err));
        return;
    }
    if (!timekeeper_utc_plausible(utc)) {
        ESP_LOGW(TAG, "RTC time out of range (%lld): time unknown", (long long)utc);
        return;
    }

    set_system_utc(utc);
    atomic_store(&s_known, true);
    ESP_LOGI(TAG, "time from RTC: utc=%lld", (long long)utc);
}

bool timekeeper_is_known(void)
{
    return atomic_load(&s_known);
}

bool timekeeper_now_local(int64_t *local_s)
{
    if (!atomic_load(&s_known)) {
        return false;
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *local_s = alarm_local_from_utc((int64_t)tv.tv_sec);
    return true;
}

esp_err_t timekeeper_set_utc(int64_t utc_s)
{
    if (!timekeeper_utc_plausible(utc_s)) {
        return ESP_ERR_INVALID_ARG;
    }

    set_system_utc(utc_s);
    atomic_store(&s_known, true);

    if (s_rtc != NULL) {
        esp_err_t err = ds3231_write_utc(s_rtc, utc_s);
        if (err != ESP_OK) {
            /* 시스템 시간은 이미 맞았으니 알람은 돈다. 재부팅하면 다시 모름이 될 수 있다 */
            ESP_LOGW(TAG, "RTC write failed: %s", esp_err_to_name(err));
            return err;
        }
    }
    ESP_LOGI(TAG, "time set: utc=%lld", (long long)utc_s);
    return ESP_OK;
}
