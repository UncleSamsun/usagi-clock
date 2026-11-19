#include "app_config.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "store_codec.h"

static const char *TAG = "storage";

#define NVS_NS        "usagi"
#define KEY_SETTINGS  "settings"
#define KEY_ALARMS    "alarms"

static SemaphoreHandle_t s_lock;
static app_settings_t s_settings;
static app_settings_apply_fn_t s_apply;
static QueueHandle_t s_save_q;
static size_t (*s_alarm_source)(alarm_t *out, size_t max);

esp_err_t storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS layout changed, erasing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }
    return err;
}

static esp_err_t read_blob(const char *key, uint8_t *buf, size_t cap, size_t *len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    *len = cap;
    err = nvs_get_blob(h, key, buf, len);
    nvs_close(h);
    return err;
}

static esp_err_t write_blob(const char *key, const uint8_t *buf, size_t len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    /* NVS는 키 단위로 원자적으로 쓴다: 전원이 끊겨도 이전 값이나 새 값 중 하나가 남는다 */
    err = nvs_set_blob(h, key, buf, len);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

void storage_load_settings(app_settings_t *out)
{
    static uint8_t buf[STORE_SETTINGS_BYTES];
    size_t len = 0;
    app_settings_t s;
    esp_err_t err = read_blob(KEY_SETTINGS, buf, sizeof(buf), &len);
    if (err == ESP_OK && store_decode_settings(buf, len, &s)) {
        *out = s;
        return;
    }
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "settings unreadable (%s): using defaults", err == ESP_OK ? "corrupt" : esp_err_to_name(err));
    }
    /* out에는 호출하는 쪽이 넣어 둔 기본값이 그대로 남는다 */
}

esp_err_t storage_save_settings(const app_settings_t *s)
{
    static uint8_t buf[STORE_SETTINGS_BYTES];
    size_t len = store_encode_settings(s, buf, sizeof(buf));
    return len > 0 ? write_blob(KEY_SETTINGS, buf, len) : ESP_ERR_INVALID_SIZE;
}

size_t storage_load_alarms(alarm_t *out, size_t max)
{
    static uint8_t buf[STORE_ALARMS_MAX_BYTES];
    static alarm_t tmp[ALARM_MAX];
    size_t len = 0, n = 0, dropped = 0;

    esp_err_t err = read_blob(KEY_ALARMS, buf, sizeof(buf), &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return 0;
    }
    if (err != ESP_OK || !store_decode_alarms(buf, len, tmp, &n, &dropped)) {
        ESP_LOGW(TAG, "alarm list unreadable (%s): starting empty", err == ESP_OK ? "corrupt" : esp_err_to_name(err));
        return 0;
    }
    if (dropped > 0) {
        ESP_LOGW(TAG, "dropped %u invalid alarms", (unsigned)dropped);
    }
    if (n > max) {
        n = max;
    }
    memcpy(out, tmp, n * sizeof(alarm_t));
    return n;
}

esp_err_t storage_save_alarms(const alarm_t *a, size_t n)
{
    static uint8_t buf[STORE_ALARMS_MAX_BYTES];
    size_t len = store_encode_alarms(a, n, buf, sizeof(buf));
    return len > 0 ? write_blob(KEY_ALARMS, buf, len) : ESP_ERR_INVALID_SIZE;
}

/* 저장 작업: 요청이 몰려도 마지막 한 번만 저장한다(알람 작업을 막지 않으려고 따로 둔다) */
static void saver_task(void *arg)
{
    (void)arg;
    static alarm_t list[ALARM_MAX];
    uint8_t dummy;
    for (;;) {
        xQueueReceive(s_save_q, &dummy, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(200));   /* 잇따른 변경을 한 번에 */
        xQueueReset(s_save_q);
        if (s_alarm_source == NULL) {
            continue;
        }
        size_t n = s_alarm_source(list, ALARM_MAX);
        esp_err_t err = storage_save_alarms(list, n);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "alarm save failed: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "saved %u alarms", (unsigned)n);
        }
    }
}

void storage_set_alarm_source(size_t (*get)(alarm_t *out, size_t max))
{
    s_alarm_source = get;
    if (s_save_q == NULL) {
        s_save_q = xQueueCreate(1, sizeof(uint8_t));
        xTaskCreate(saver_task, "saver", 4096, NULL, 3, NULL);
    }
}

void storage_request_save_alarms(void)
{
    uint8_t one = 1;
    if (s_save_q != NULL) {
        xQueueOverwrite(s_save_q, &one);
    }
}

esp_err_t storage_factory_reset(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_all(h);   /* 설정, 알람, Wi-Fi. "factory"(AP 비밀번호)와 SD 파일은 남는다 */
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

void app_settings_init(const app_settings_t *loaded, app_settings_apply_fn_t apply)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
    }
    s_settings = *loaded;
    s_apply = apply;
}

app_settings_t app_settings_get(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    app_settings_t s = s_settings;
    xSemaphoreGive(s_lock);
    return s;
}

esp_err_t app_settings_set(const app_settings_t *s)
{
    if (!app_settings_validate(s)) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = storage_save_settings(s);
    if (err != ESP_OK) {
        return err;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_settings = *s;
    xSemaphoreGive(s_lock);
    if (s_apply != NULL) {
        s_apply(s);
    }
    return ESP_OK;
}
