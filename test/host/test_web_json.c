#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "web_api.h"

static char err[96];
static char buf[8192];

static const char *ALARM_OK =
    "{\"id\":3,\"enabled\":true,\"hour\":7,\"minute\":30,\"days\":31,\"pre_minutes\":5,"
    "\"pre_volume\":60,\"main_volume\":70,\"label\":\"출근\",\"clipset\":\"default\",\"song\":\"a.mp3\"}";

void setUp(void) { err[0] = '\0'; }
void tearDown(void) {}

void test_alarm_json_round_trip(void)
{
    alarm_t a;
    TEST_ASSERT_TRUE_MESSAGE(web_alarm_from_json(ALARM_OK, &a, err, sizeof(err)), err);
    TEST_ASSERT_EQUAL_UINT8(3, a.id);
    TEST_ASSERT_EQUAL_UINT8(7, a.hour);
    TEST_ASSERT_EQUAL_UINT8(30, a.minute);
    TEST_ASSERT_EQUAL_STRING("출근", a.label);

    TEST_ASSERT_GREATER_THAN_size_t(0, web_alarm_to_json(&a, buf, sizeof(buf)));
    alarm_t b;
    TEST_ASSERT_TRUE(web_alarm_from_json(buf, &b, err, sizeof(err)));
    TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof(a));
}

/* 한 필드만 바꾼 알람 JSON */
static const char *with(const char *field_json)
{
    static char j[512];
    /* 기본값 뒤에 같은 키를 다시 쓰면 cJSON은 앞의 것을 찾으므로, 키를 바꿔 끼운다 */
    snprintf(j, sizeof(j),
             "{\"enabled\":true,\"hour\":7,\"minute\":30,\"days\":31,\"pre_minutes\":5,"
             "\"pre_volume\":60,\"main_volume\":70,\"label\":\"x\",\"clipset\":\"\",\"song\":\"\"}");
    char key[32];
    sscanf(field_json, "\"%31[^\"]\"", key);
    char needle[40];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    char *p = strstr(j, needle);
    if (p == NULL) {
        return j;
    }
    char *end = p + strlen(needle);
    int depth = 0;
    while (*end && !(depth == 0 && (*end == ',' || *end == '}'))) {
        if (*end == '"') {
            end++;
            while (*end && *end != '"') {
                end++;
            }
        }
        end++;
    }
    static char r[512];
    snprintf(r, sizeof(r), "%.*s%s%s", (int)(p - j), j, field_json, end);
    return r;
}

/* Review Focus 2 */
void test_alarm_json_rejects_out_of_range(void)
{
    alarm_t a;
    const char *bad[] = { "\"hour\":24", "\"minute\":60", "\"days\":128", "\"pre_minutes\":61", "\"main_volume\":101",
                          "\"pre_volume\":-1" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        err[0] = '\0';
        TEST_ASSERT_FALSE_MESSAGE(web_alarm_from_json(with(bad[i]), &a, err, sizeof(err)), bad[i]);
        TEST_ASSERT_NOT_EQUAL(0, strlen(err));
    }
    TEST_ASSERT_TRUE(web_alarm_from_json(with("\"hour\":23"), &a, err, sizeof(err)));
}

/* Review Focus 2 */
void test_alarm_json_rejects_wrong_type(void)
{
    alarm_t a;
    TEST_ASSERT_FALSE(web_alarm_from_json(with("\"hour\":\"7\""), &a, err, sizeof(err)));
    TEST_ASSERT_FALSE(web_alarm_from_json(with("\"enabled\":1"), &a, err, sizeof(err)));
    TEST_ASSERT_FALSE(web_alarm_from_json(with("\"days\":1.5"), &a, err, sizeof(err)));
    TEST_ASSERT_FALSE(web_alarm_from_json(with("\"label\":5"), &a, err, sizeof(err)));
}

/* Review Focus 2: label은 23바이트까지 */
void test_alarm_json_label_too_long(void)
{
    alarm_t a;
    TEST_ASSERT_FALSE(web_alarm_from_json(with("\"label\":\"abcdefghijklmnopqrstuvwx\""), &a, err, sizeof(err)));  /* 24 */
    TEST_ASSERT_TRUE(web_alarm_from_json(with("\"label\":\"abcdefghijklmnopqrstuvw\""), &a, err, sizeof(err)));   /* 23 */
    TEST_ASSERT_FALSE(web_alarm_from_json(with("\"label\":\"가나다라마바사아\""), &a, err, sizeof(err)));          /* 24바이트 */
    TEST_ASSERT_TRUE(web_alarm_from_json(with("\"label\":\"가나다라마바사\""), &a, err, sizeof(err)));             /* 21바이트 */
}

/* Review Focus 2 */
void test_alarm_json_missing_field(void)
{
    alarm_t a;
    const char *no_minute =
        "{\"enabled\":true,\"hour\":7,\"days\":31,\"pre_minutes\":5,"
        "\"pre_volume\":60,\"main_volume\":70,\"label\":\"x\",\"clipset\":\"\",\"song\":\"\"}";
    TEST_ASSERT_FALSE(web_alarm_from_json(no_minute, &a, err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("minute missing", err);
    TEST_ASSERT_FALSE(web_alarm_from_json("{", &a, err, sizeof(err)));
    TEST_ASSERT_FALSE(web_alarm_from_json("", &a, err, sizeof(err)));
    TEST_ASSERT_FALSE(web_alarm_from_json("[1,2]", &a, err, sizeof(err)));
}

static app_settings_t base(void)
{
    static const uint8_t mac[6] = { 0, 0, 0, 0, 0xab, 0xcd };
    return app_settings_default(mac);
}

void test_settings_partial_update(void)
{
    app_settings_t b = base(), s;
    TEST_ASSERT_TRUE(web_settings_from_json("{\"snooze_min\":7}", &b, &s, err, sizeof(err)));
    TEST_ASSERT_EQUAL_UINT16(7, s.alarm.snooze_min);
    TEST_ASSERT_EQUAL_UINT16(b.alarm.auto_stop_min, s.alarm.auto_stop_min);
    TEST_ASSERT_EQUAL_STRING(b.device_name, s.device_name);

    TEST_ASSERT_TRUE(web_settings_from_json("{\"device_name\":\"usagi-mj\",\"night_end_min\":360}", &b, &s, err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("usagi-mj", s.device_name);
    TEST_ASSERT_EQUAL_UINT16(360, s.night_end_min);
}

void test_settings_rejects_invalid(void)
{
    app_settings_t b = base(), s;
    TEST_ASSERT_FALSE(web_settings_from_json("{\"device_name\":\"Bad_Name\"}", &b, &s, err, sizeof(err)));
    TEST_ASSERT_FALSE(web_settings_from_json("{\"night_start_min\":1440}", &b, &s, err, sizeof(err)));
    TEST_ASSERT_FALSE(web_settings_from_json("{\"snooze_min\":31}", &b, &s, err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("snooze_min out of range", err);
    TEST_ASSERT_FALSE(web_settings_from_json("nope", &b, &s, err, sizeof(err)));
}

void test_settings_to_json_round_trip(void)
{
    app_settings_t b = base(), s;
    TEST_ASSERT_GREATER_THAN_size_t(0, web_settings_to_json(&b, buf, sizeof(buf)));
    app_settings_t zero = b;
    zero.alarm.snooze_min = 9;
    TEST_ASSERT_TRUE(web_settings_from_json(buf, &zero, &s, err, sizeof(err)));
    TEST_ASSERT_EQUAL_MEMORY(&b, &s, sizeof(b));
}

void test_pick_alarm_id(void)
{
    alarm_t list[255];
    memset(list, 0, sizeof(list));
    TEST_ASSERT_EQUAL_UINT8(1, web_pick_alarm_id(list, 0));
    list[0].id = 1; list[1].id = 2; list[2].id = 4;
    TEST_ASSERT_EQUAL_UINT8(3, web_pick_alarm_id(list, 3));
    for (int i = 0; i < 255; i++) {
        list[i].id = (uint8_t)(i + 1);
    }
    TEST_ASSERT_EQUAL_UINT8(0, web_pick_alarm_id(list, 255));
}

void test_alarms_to_json(void)
{
    alarm_t a;
    web_alarm_from_json(ALARM_OK, &a, err, sizeof(err));
    TEST_ASSERT_EQUAL_size_t(0, web_alarms_to_json(&a, 1, buf, 10));   /* 버퍼 부족 */
    size_t n = web_alarms_to_json(&a, 1, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN_size_t(0, n);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"alarms\":[{"));
}

void test_wifi_and_time_json(void)
{
    char ssid[33], pass[65];
    int64_t utc = 0;
    TEST_ASSERT_TRUE(web_wifi_from_json("{\"ssid\":\"home\",\"pass\":\"12345678\"}", ssid, pass, err, sizeof(err)));
    TEST_ASSERT_EQUAL_STRING("home", ssid);
    TEST_ASSERT_TRUE(web_wifi_from_json("{\"ssid\":\"open\",\"pass\":\"\"}", ssid, pass, err, sizeof(err)));
    TEST_ASSERT_FALSE(web_wifi_from_json("{\"ssid\":\"home\",\"pass\":\"short\"}", ssid, pass, err, sizeof(err)));
    TEST_ASSERT_FALSE(web_wifi_from_json("{\"ssid\":\"\",\"pass\":\"\"}", ssid, pass, err, sizeof(err)));
    TEST_ASSERT_FALSE(web_wifi_from_json("{\"ssid\":\"123456789012345678901234567890123\",\"pass\":\"\"}", ssid, pass, err, sizeof(err)));

    TEST_ASSERT_TRUE(web_time_from_json("{\"utc\":1794780000}", &utc, err, sizeof(err)));
    TEST_ASSERT_EQUAL_INT64(1794780000LL, utc);
    TEST_ASSERT_FALSE(web_time_from_json("{\"utc\":\"now\"}", &utc, err, sizeof(err)));
    TEST_ASSERT_FALSE(web_time_from_json("{\"utc\":1.5}", &utc, err, sizeof(err)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_alarm_json_round_trip);
    RUN_TEST(test_alarm_json_rejects_out_of_range);
    RUN_TEST(test_alarm_json_rejects_wrong_type);
    RUN_TEST(test_alarm_json_label_too_long);
    RUN_TEST(test_alarm_json_missing_field);
    RUN_TEST(test_settings_partial_update);
    RUN_TEST(test_settings_rejects_invalid);
    RUN_TEST(test_settings_to_json_round_trip);
    RUN_TEST(test_pick_alarm_id);
    RUN_TEST(test_alarms_to_json);
    RUN_TEST(test_wifi_and_time_json);
    return UNITY_END();
}
