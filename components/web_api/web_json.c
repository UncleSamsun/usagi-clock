#include "web_api.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"

static void set_err(char *err, size_t n, const char *msg)
{
    if (err != NULL && n > 0) {
        snprintf(err, n, "%s", msg);
    }
}

typedef enum { FIELD_OK = 0, FIELD_MISSING, FIELD_BAD } field_t;

/* 정수 필드: 숫자이고 소수 부분이 없어야 한다 */
static field_t get_int(const cJSON *o, const char *key, long lo, long hi, long *out)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, key);
    if (it == NULL) {
        return FIELD_MISSING;
    }
    if (!cJSON_IsNumber(it)) {
        return FIELD_BAD;
    }
    double d = it->valuedouble;
    if (d != floor(d) || d < (double)lo || d > (double)hi) {
        return FIELD_BAD;
    }
    *out = (long)d;
    return FIELD_OK;
}

static field_t get_bool(const cJSON *o, const char *key, bool *out)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, key);
    if (it == NULL) {
        return FIELD_MISSING;
    }
    if (!cJSON_IsBool(it)) {
        return FIELD_BAD;
    }
    *out = cJSON_IsTrue(it);
    return FIELD_OK;
}

/* 문자열 필드: UTF-8 바이트 수가 cap-1 이하 */
static field_t get_str(const cJSON *o, const char *key, char *out, size_t cap)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, key);
    if (it == NULL) {
        return FIELD_MISSING;
    }
    if (!cJSON_IsString(it) || it->valuestring == NULL || strlen(it->valuestring) >= cap) {
        return FIELD_BAD;
    }
    memset(out, 0, cap);
    memcpy(out, it->valuestring, strlen(it->valuestring));
    return FIELD_OK;
}

/* 필수 정수 필드를 읽고 실패하면 "<key> missing" / "<key> out of range" */
#define REQ_INT(obj, key, lo, hi, dst)                                       \
    do {                                                                     \
        long v_ = 0;                                                         \
        field_t f_ = get_int((obj), (key), (lo), (hi), &v_);                 \
        if (f_ != FIELD_OK) {                                                \
            snprintf(msg, sizeof(msg), "%s %s", (key),                       \
                     f_ == FIELD_MISSING ? "missing" : "out of range");      \
            goto fail;                                                       \
        }                                                                    \
        (dst) = (__typeof__(dst))v_;                                         \
    } while (0)

bool web_alarm_from_json(const char *json, alarm_t *out, char *err, size_t err_n)
{
    char msg[64] = "invalid json";
    cJSON *o = cJSON_Parse(json);
    alarm_t a;
    memset(&a, 0, sizeof(a));

    if (!cJSON_IsObject(o)) {
        goto fail;
    }

    long id = 0;
    if (get_int(o, "id", 0, 255, &id) == FIELD_BAD) {
        snprintf(msg, sizeof(msg), "id out of range");
        goto fail;
    }
    a.id = (uint8_t)id;

    field_t f = get_bool(o, "enabled", &a.enabled);
    if (f != FIELD_OK) {
        snprintf(msg, sizeof(msg), "enabled %s", f == FIELD_MISSING ? "missing" : "must be true or false");
        goto fail;
    }
    REQ_INT(o, "hour", 0, 23, a.hour);
    REQ_INT(o, "minute", 0, 59, a.minute);
    REQ_INT(o, "days", 0, ALARM_DAYS_EVERYDAY, a.days_mask);
    REQ_INT(o, "pre_minutes", 0, ALARM_PRE_MINUTES_MAX, a.pre_minutes);
    REQ_INT(o, "pre_volume", 0, ALARM_VOLUME_MAX, a.pre_volume);
    REQ_INT(o, "main_volume", 0, ALARM_VOLUME_MAX, a.main_volume);

    static const struct { const char *key; size_t off; size_t cap; } strs[] = {
        { "label", offsetof(alarm_t, label), sizeof(((alarm_t *)0)->label) },
        { "clipset", offsetof(alarm_t, clipset), sizeof(((alarm_t *)0)->clipset) },
        { "song", offsetof(alarm_t, song), sizeof(((alarm_t *)0)->song) },
    };
    for (size_t i = 0; i < sizeof(strs) / sizeof(strs[0]); i++) {
        f = get_str(o, strs[i].key, (char *)&a + strs[i].off, strs[i].cap);
        if (f != FIELD_OK) {
            snprintf(msg, sizeof(msg), "%s %s", strs[i].key, f == FIELD_MISSING ? "missing" : "too long or not a string");
            goto fail;
        }
    }
    if (!alarm_validate(&a)) {
        snprintf(msg, sizeof(msg), "invalid alarm");
        goto fail;
    }

    cJSON_Delete(o);
    *out = a;
    return true;

fail:
    cJSON_Delete(o);
    set_err(err, err_n, msg);
    return false;
}

/* 설정의 선택 정수 필드: 있으면 범위 검사 후 덮어쓴다 */
#define OPT_INT(obj, key, lo, hi, dst)                                       \
    do {                                                                     \
        long v_ = 0;                                                         \
        field_t f_ = get_int((obj), (key), (lo), (hi), &v_);                 \
        if (f_ == FIELD_BAD) {                                               \
            snprintf(msg, sizeof(msg), "%s out of range", (key));            \
            goto fail;                                                       \
        }                                                                    \
        if (f_ == FIELD_OK) {                                                \
            (dst) = (__typeof__(dst))v_;                                     \
        }                                                                    \
    } while (0)

bool web_settings_from_json(const char *json, const app_settings_t *base, app_settings_t *out, char *err, size_t err_n)
{
    char msg[64] = "invalid json";
    cJSON *o = cJSON_Parse(json);
    app_settings_t s = *base;

    if (!cJSON_IsObject(o)) {
        goto fail;
    }
    OPT_INT(o, "snooze_min", APP_SNOOZE_MIN_MIN, APP_SNOOZE_MIN_MAX, s.alarm.snooze_min);
    OPT_INT(o, "long_press_ms", APP_LONG_PRESS_MS_MIN, APP_LONG_PRESS_MS_MAX, s.alarm.long_press_ms);
    OPT_INT(o, "auto_stop_min", APP_AUTO_STOP_MIN_MIN, APP_AUTO_STOP_MIN_MAX, s.alarm.auto_stop_min);
    OPT_INT(o, "default_volume", 0, ALARM_VOLUME_MAX, s.default_volume);
    OPT_INT(o, "day_brightness", APP_BRIGHTNESS_MIN, 100, s.day_brightness);
    OPT_INT(o, "night_brightness", APP_BRIGHTNESS_MIN, 100, s.night_brightness);
    OPT_INT(o, "night_start_min", 0, APP_MINUTES_PER_DAY - 1, s.night_start_min);
    OPT_INT(o, "night_end_min", 0, APP_MINUTES_PER_DAY - 1, s.night_end_min);

    field_t f = get_str(o, "device_name", s.device_name, sizeof(s.device_name));
    if (f == FIELD_BAD || (f == FIELD_OK && !app_device_name_ok(s.device_name))) {
        snprintf(msg, sizeof(msg), "device_name must be a-z, 0-9, '-' (1-24)");
        goto fail;
    }
    if (!app_settings_validate(&s)) {
        snprintf(msg, sizeof(msg), "invalid settings");
        goto fail;
    }

    cJSON_Delete(o);
    *out = s;
    return true;

fail:
    cJSON_Delete(o);
    set_err(err, err_n, msg);
    return false;
}

static cJSON *alarm_obj(const alarm_t *a)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "id", a->id);
    cJSON_AddBoolToObject(o, "enabled", a->enabled);
    cJSON_AddNumberToObject(o, "hour", a->hour);
    cJSON_AddNumberToObject(o, "minute", a->minute);
    cJSON_AddNumberToObject(o, "days", a->days_mask);
    cJSON_AddNumberToObject(o, "pre_minutes", a->pre_minutes);
    cJSON_AddNumberToObject(o, "pre_volume", a->pre_volume);
    cJSON_AddNumberToObject(o, "main_volume", a->main_volume);
    cJSON_AddStringToObject(o, "label", a->label);
    cJSON_AddStringToObject(o, "clipset", a->clipset);
    cJSON_AddStringToObject(o, "song", a->song);
    return o;
}

static size_t print_into(cJSON *o, char *out, size_t n)
{
    size_t len = 0;
    if (o != NULL && n > 0 && cJSON_PrintPreallocated(o, out, (int)n, false)) {
        len = strlen(out);
    } else if (n > 0) {
        out[0] = '\0';
    }
    cJSON_Delete(o);
    return len;
}

size_t web_alarm_to_json(const alarm_t *a, char *out, size_t n)
{
    return print_into(alarm_obj(a), out, n);
}

size_t web_alarms_to_json(const alarm_t *a, size_t count, char *out, size_t n)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "alarms");
    for (size_t i = 0; i < count; i++) {
        cJSON_AddItemToArray(arr, alarm_obj(&a[i]));
    }
    return print_into(root, out, n);
}

size_t web_settings_to_json(const app_settings_t *s, char *out, size_t n)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "snooze_min", s->alarm.snooze_min);
    cJSON_AddNumberToObject(o, "long_press_ms", s->alarm.long_press_ms);
    cJSON_AddNumberToObject(o, "auto_stop_min", s->alarm.auto_stop_min);
    cJSON_AddNumberToObject(o, "default_volume", s->default_volume);
    cJSON_AddNumberToObject(o, "day_brightness", s->day_brightness);
    cJSON_AddNumberToObject(o, "night_brightness", s->night_brightness);
    cJSON_AddNumberToObject(o, "night_start_min", s->night_start_min);
    cJSON_AddNumberToObject(o, "night_end_min", s->night_end_min);
    cJSON_AddStringToObject(o, "device_name", s->device_name);
    return print_into(o, out, n);
}

uint8_t web_pick_alarm_id(const alarm_t *a, size_t count)
{
    for (int id = 1; id <= 255; id++) {
        bool used = false;
        for (size_t i = 0; i < count && !used; i++) {
            used = a[i].id == id;
        }
        if (!used) {
            return (uint8_t)id;
        }
    }
    return 0;
}

bool web_wifi_from_json(const char *json, char ssid[33], char pass[65], char *err, size_t err_n)
{
    cJSON *o = cJSON_Parse(json);
    bool ok = false;
    char s[33], p[65];

    if (!cJSON_IsObject(o)) {
        set_err(err, err_n, "invalid json");
    } else if (get_str(o, "ssid", s, sizeof(s)) != FIELD_OK || s[0] == '\0') {
        set_err(err, err_n, "ssid must be 1-32 bytes");
    } else if (get_str(o, "pass", p, sizeof(p)) != FIELD_OK || (p[0] != '\0' && strlen(p) < 8)) {
        set_err(err, err_n, "password must be empty or 8-63 bytes");
    } else {
        memcpy(ssid, s, sizeof(s));
        memcpy(pass, p, sizeof(p));
        ok = true;
    }
    cJSON_Delete(o);
    return ok;
}

bool web_time_from_json(const char *json, int64_t *utc, char *err, size_t err_n)
{
    cJSON *o = cJSON_Parse(json);
    const cJSON *it = cJSON_IsObject(o) ? cJSON_GetObjectItemCaseSensitive(o, "utc") : NULL;
    bool ok = cJSON_IsNumber(it) && it->valuedouble == floor(it->valuedouble) && it->valuedouble > 0 &&
              it->valuedouble < 1e11;
    if (ok) {
        *utc = (int64_t)it->valuedouble;
    } else {
        set_err(err, err_n, "utc must be epoch seconds");
    }
    cJSON_Delete(o);
    return ok;
}
