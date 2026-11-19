#include "alarm_service_esp.h"

#include <string.h>
#include <sys/time.h>

#include "audio.h"
#include "display_ui.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "timekeeper.h"

static const char *TAG = "alarm";

#define ALARM_TASK_PRIO  8
#define ALARM_TASK_STACK 6144

static alarm_engine_t s_eng;               /* 엔진은 여기 하나뿐 */
static SemaphoreHandle_t s_lock;
static QueueHandle_t s_input_q;
static int64_t s_last_tick_sec = -1;
static uint8_t s_fallback_volume = 60;

static alarm_service_change_cb_t s_change_cb;
static alarm_service_brightness_fn_t s_brightness_fn;
static alarm_service_ui_decorator_t s_ui_decorator;

static void lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(s_lock); }

/* 엔진 알람 목록에서 id 찾기(잠금 안에서) */
static const alarm_t *find_alarm(uint8_t id)
{
    for (size_t i = 0; i < s_eng.n_alarms; i++) {
        if (s_eng.alarms[i].id == id) {
            return &s_eng.alarms[i];
        }
    }
    return NULL;
}

static void post_audio(audio_op_t op, uint8_t volume, const char *name)
{
    audio_cmd_t cmd = { .op = op, .volume = volume };
    if (name != NULL) {
        strncpy(cmd.name, name, sizeof(cmd.name) - 1);
    }
    audio_post(&cmd);
}

/*
 * 엔진 출력 → 오디오 명령. 순서는 alarm_core.h의 약속대로
 * AUDIO_STOP → CONFIRM_BEEP → MAIN_START → PRE_CLIP
 * (회차가 끝나는 틱에 다른 알람이 시작되면 멈춤이 먼저 와야 한다)
 */
static void handle_output(const alarm_output_t *out)
{
    if (out->actions == 0) {
        return;
    }

    /* 볼륨·노래·클립 세트는 대표 알람에서 꺼낸다. 목록에서 지워졌으면 기본값 */
    uint8_t pre_vol = s_fallback_volume, main_vol = s_fallback_volume;
    char song[AUDIO_NAME_MAX] = "", clipset[AUDIO_NAME_MAX] = "";
    lock();
    const alarm_t *a = find_alarm(out->alarm_id);
    if (a != NULL) {
        pre_vol = a->pre_volume;
        main_vol = a->main_volume;
        strncpy(song, a->song, sizeof(song) - 1);
        strncpy(clipset, a->clipset, sizeof(clipset) - 1);
    }
    unlock();

    if (out->actions & ALARM_ACT_AUDIO_STOP) {
        post_audio(AUDIO_OP_STOP, 0, NULL);
    }
    if (out->actions & ALARM_ACT_CONFIRM_BEEP) {
        post_audio(AUDIO_OP_CONFIRM, main_vol, NULL);
    }
    if (out->actions & ALARM_ACT_MAIN_START) {
        ESP_LOGI(TAG, "ring id=%u", out->alarm_id);
        post_audio(AUDIO_OP_ALARM_LOOP, main_vol, song);
    }
    if (out->actions & ALARM_ACT_PRE_CLIP) {
        post_audio(AUDIO_OP_PRE_TICK, pre_vol, clipset);
    }
    if (out->actions & ALARM_ACT_DISABLE_ONCE) {
        for (uint8_t i = 0; i < out->n_disable; i++) {
            ESP_LOGI(TAG, "DISABLE_ONCE id=%u", out->disable_ids[i]);
        }
        if (s_change_cb != NULL) {
            s_change_cb();
        }
    }
}

static ui_mode_t mode_of(alarm_state_t st)
{
    switch (st) {
    case ALARM_STATE_PRE:      return UI_MODE_PRE;
    case ALARM_STATE_RINGING:  return UI_MODE_RINGING;
    case ALARM_STATE_SNOOZED:  return UI_MODE_SNOOZED;
    case ALARM_STATE_PRE_MUTED:return UI_MODE_PRE;
    case ALARM_STATE_IDLE:
    default:                   return UI_MODE_CLOCK;
    }
}

static void post_ui(bool known, int64_t now_local)
{
    ui_state_t ui = { .time_known = known, .now_local = now_local, .next_alarm_local = -1 };

    lock();
    alarm_view_t v = alarm_engine_view(&s_eng);
    ui.mode = mode_of(v.state);
    ui.ring_at = v.ring_at;
    ui.snooze_until = v.snooze_until;
    if (v.state != ALARM_STATE_IDLE) {
        const alarm_t *a = find_alarm(v.alarm_id);
        if (a != NULL) {
            strncpy(ui.label, a->label, sizeof(ui.label) - 1);
        }
    }
    if (known) {
        ui.next_alarm_local = alarm_next_any(s_eng.alarms, s_eng.n_alarms, now_local, NULL);
    }
    unlock();

    if (s_ui_decorator != NULL) {
        s_ui_decorator(&ui);
    }
    display_ui_post(&ui);

    bool ringing = v.state == ALARM_STATE_RINGING;
    uint8_t pct = (s_brightness_fn != NULL && known) ? s_brightness_fn(now_local, ringing) : (ringing ? 100 : 80);
    display_ui_set_brightness(pct);
}

/* 다음 초 경계까지 남은 시간(ms), 최소 1 */
static TickType_t ticks_to_next_second(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int ms = 1000 - (int)(tv.tv_usec / 1000);
    if (ms < 1) {
        ms = 1;
    }
    return pdMS_TO_TICKS(ms);
}

static void alarm_task(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);

    for (;;) {
        alarm_input_t in;
        if (xQueueReceive(s_input_q, &in, ticks_to_next_second()) == pdTRUE) {
            int64_t now = 0;
            timekeeper_now_local(&now);
            lock();
            alarm_output_t out = alarm_engine_input(&s_eng, in, now);
            unlock();
            handle_output(&out);
        }

        int64_t now = 0;
        bool known = timekeeper_now_local(&now);
        /* 시간을 모르면 틱을 돌리지 않는다(설계 5.1). 같은 초에 두 번 돌리지 않는다 */
        if (known && now != s_last_tick_sec) {
            s_last_tick_sec = now;
            lock();
            alarm_output_t out = alarm_engine_tick(&s_eng, now);
            unlock();
            handle_output(&out);
        }
        post_ui(known, now);
        esp_task_wdt_reset();
    }
}

void alarm_service_start(const alarm_settings_t *s)
{
    s_lock = xSemaphoreCreateMutex();
    s_input_q = xQueueCreate(4, sizeof(alarm_input_t));
    alarm_engine_init(&s_eng, s);
    xTaskCreate(alarm_task, "alarm", ALARM_TASK_STACK, NULL, ALARM_TASK_PRIO, NULL);
}

void alarm_service_post_input(alarm_input_t in)
{
    if (s_input_q != NULL && xQueueSend(s_input_q, &in, 0) != pdTRUE) {
        ESP_LOGW(TAG, "input queue full");
    }
}

esp_err_t alarm_service_set_alarms(const alarm_t *a, size_t n)
{
    if (n > ALARM_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }
    for (size_t i = 0; i < n; i++) {
        if (!alarm_validate(&a[i]) || a[i].id == 0) {
            return ESP_ERR_INVALID_ARG;
        }
        for (size_t j = 0; j < i; j++) {
            if (a[j].id == a[i].id) {
                return ESP_ERR_INVALID_ARG;   /* id 중복 */
            }
        }
    }
    lock();
    alarm_engine_set_alarms(&s_eng, a, n);
    unlock();
    if (s_change_cb != NULL) {
        s_change_cb();
    }
    return ESP_OK;
}

size_t alarm_service_get_alarms(alarm_t *out, size_t max)
{
    lock();
    size_t n = s_eng.n_alarms < max ? s_eng.n_alarms : max;
    memcpy(out, s_eng.alarms, n * sizeof(alarm_t));
    unlock();
    return n;
}

alarm_view_t alarm_service_view(void)
{
    lock();
    alarm_view_t v = alarm_engine_view(&s_eng);
    unlock();
    return v;
}

void alarm_service_set_settings(const alarm_settings_t *s)
{
    lock();
    alarm_engine_set_settings(&s_eng, s);
    unlock();
}

void alarm_service_set_change_cb(alarm_service_change_cb_t cb) { s_change_cb = cb; }
void alarm_service_set_brightness_fn(alarm_service_brightness_fn_t fn) { s_brightness_fn = fn; }
void alarm_service_set_ui_decorator(alarm_service_ui_decorator_t fn) { s_ui_decorator = fn; }
void alarm_service_set_fallback_volume(uint8_t vol) { s_fallback_volume = vol; }
