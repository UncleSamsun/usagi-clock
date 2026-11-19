#include "audio.h"

#include <stdio.h>
#include <string.h>

#include "audio_files.h"
#include "audio_player.h"
#include "clip_cache.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mp3_stream.h"
#include "sdcard.h"

static const char *TAG = "audio";

#define AUDIO_QUEUE_LEN    8
#define AUDIO_CHUNK        1152          /* 한 번에 채워 보내는 샘플 수(44.1kHz에서 약 26ms) */
#define AUDIO_TASK_PRIO    10            /* alarm_service보다 높게 */
#define AUDIO_TASK_STACK   (24 * 1024)   /* minimp3가 프레임마다 스택에 약 16KB를 쓴다 */
#define PREVIEW_SECONDS    10

static i2s_chan_handle_t s_tx;
static QueueHandle_t s_cmd_q;
static uint32_t s_rate;

static audio_player_t s_player;
static play_action_t s_act;              /* 지금 재생 중인 것 */

/* 소스들 */
static beep_t s_beep;
typedef struct { FILE *f; bool io_error; } file_src_t;
static file_src_t s_song_file;
static mp3_stream_t s_song;
static size_t s_preview_left;
static clip_cache_t s_clips;
static size_t s_clip_pos;

/* ===== I2S ===== */

static void set_rate(uint32_t rate)
{
    if (rate == 0 || rate == s_rate) {
        return;
    }
    i2s_channel_disable(s_tx);
    i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(rate);
    i2s_channel_reconfig_std_clock(s_tx, &clk);
    i2s_channel_enable(s_tx);
    s_rate = rate;
}

/* ===== 파일 소스 ===== */

static size_t file_read(void *ctx, uint8_t *buf, size_t n)
{
    file_src_t *src = ctx;
    if (src->f == NULL) {
        return 0;
    }
    size_t got = fread(buf, 1, n, src->f);
    if (got < n && ferror(src->f)) {
        src->io_error = true;   /* SD를 뽑았거나 읽기 오류 */
    }
    return got;
}

static void close_song(void)
{
    if (s_song_file.f != NULL) {
        fclose(s_song_file.f);
        s_song_file.f = NULL;
    }
}

static bool open_song(const char *path)
{
    close_song();
    s_song_file.io_error = false;
    s_song_file.f = fopen(path, "rb");
    if (s_song_file.f == NULL) {
        return false;
    }
    setvbuf(s_song_file.f, NULL, _IOFBF, 8192);
    mp3_stream_init(&s_song, file_read, &s_song_file);
    return true;
}

/* ===== 클립 세트 캐시 ===== */

typedef struct { mp3_stream_t *s; file_src_t *f; } clip_ctx_t;

static size_t clip_src_read(void *ctx, int16_t *out, size_t max) { return mp3_stream_read(((clip_ctx_t *)ctx)->s, out, max); }
static int clip_src_rate(void *ctx) { return mp3_stream_sample_rate(((clip_ctx_t *)ctx)->s); }
static bool clip_src_failed(void *ctx)
{
    clip_ctx_t *c = ctx;
    return c->f->io_error || mp3_stream_status(c->s) == MP3_ERR_NO_FRAMES;
}

static void load_clipset(const char *set)
{
    static char names[CLIPSET_MAX][AUDIO_NAME_MAX];
    static mp3_stream_t clip_stream;
    static file_src_t clip_file;
    char dir[AUDIO_PATH_MAX], path[AUDIO_PATH_MAX];

    clip_cache_clear(&s_clips, set);
    if (!audio_path_join(dir, sizeof(dir), SD_CLIPS_DIR, set)) {
        return;
    }
    size_t n = sdcard_list_mp3(dir, names, CLIPSET_MAX);
    int64_t t0 = esp_timer_get_time();
    for (size_t i = 0; i < n; i++) {
        if (!audio_path_join(path, sizeof(path), dir, names[i])) {
            continue;
        }
        clip_file.io_error = false;
        clip_file.f = fopen(path, "rb");
        if (clip_file.f == NULL) {
            continue;
        }
        mp3_stream_init(&clip_stream, file_read, &clip_file);
        clip_ctx_t ctx = { .s = &clip_stream, .f = &clip_file };
        clip_source_t src = { .read = clip_src_read, .rate = clip_src_rate, .failed = clip_src_failed, .ctx = &ctx };
        if (!clip_cache_add(&s_clips, &src)) {
            ESP_LOGW(TAG, "clip skipped: %s", names[i]);
        }
        fclose(clip_file.f);
        clip_file.f = NULL;
    }
    ESP_LOGI(TAG, "clip set \"%s\": %u clips, %lld ms", set, s_clips.count,
             (long long)((esp_timer_get_time() - t0) / 1000));
}

/* ===== 무엇을 재생할지 ===== */

static void start_action(play_action_t a)
{
    s_act = a;
    switch (a.kind) {
    case PLAY_SONG:
    case PLAY_PREVIEW:
        if (!open_song(a.path)) {
            /* 파일이 지워졌을 수도 있으니 SD를 언마운트하지는 않는다. 이 회차는 비프로 */
            ESP_LOGW(TAG, "cannot open %s", a.path);
            start_action(audio_player_on_end(&s_player, true));
            return;
        }
        s_preview_left = 0;   /* 샘플레이트를 알게 되면 정한다 */
        ESP_LOGI(TAG, "%s %s", a.kind == PLAY_SONG ? "song" : "preview", a.path);
        break;
    case PLAY_CLIP:
        s_clip_pos = 0;
        set_rate((uint32_t)s_clips.rate[a.clip_index]);
        break;
    case PLAY_BEEP_LOOP:
        close_song();
        beep_start(&s_beep, BEEP_ALARM_LOOP, a.volume);
        set_rate(AUDIO_SAMPLE_RATE);
        ESP_LOGI(TAG, "beep loop");
        break;
    case PLAY_BEEP_TICK:
        beep_start(&s_beep, BEEP_TICK, a.volume);
        set_rate(AUDIO_SAMPLE_RATE);
        break;
    case PLAY_CONFIRM:
        close_song();
        beep_start(&s_beep, BEEP_CONFIRM, a.volume);
        set_rate(AUDIO_SAMPLE_RATE);
        break;
    case PLAY_NONE:
    default:
        close_song();
        break;
    }
}

static void handle_cmd(const audio_cmd_t *cmd)
{
    play_env_t env = { .sd_ok = false };
    char path[AUDIO_PATH_MAX];
    static char first[1][AUDIO_NAME_MAX];

    if (cmd->op == AUDIO_OP_ALARM_LOOP || cmd->op == AUDIO_OP_PREVIEW) {
        env.sd_ok = sdcard_ensure_mounted();
        if (env.sd_ok) {
            env.song_exists = audio_path_join(path, sizeof(path), SD_SOUNDS_DIR, cmd->name) &&
                              sdcard_file_exists(path);
            if (sdcard_list_mp3(SD_SOUNDS_DIR, first, 1) == 1) {
                env.first_song = first[0];
            }
        }
    } else if (cmd->op == AUDIO_OP_PRE_TICK) {
        const char *set = cmd->name[0] != '\0' ? cmd->name : CLIPSET_DEFAULT;
        env.sd_ok = sdcard_ensure_mounted();
        if (env.sd_ok && strcmp(s_clips.set_name, set) != 0) {
            load_clipset(set);
        }
        env.n_clips = (env.sd_ok && strcmp(s_clips.set_name, set) == 0) ? s_clips.count : 0;
    }

    /* 새 명령은 지금 소리를 바로 끊는다(클립이 1초보다 길어도 다음 초 명령에서 끊김) */
    start_action(audio_player_on_cmd(&s_player, cmd, &env));
}

/* 지금 소스에서 최대 n샘플. 끝나거나 실패하면 *ended, *error를 세운다 */
static size_t fill(int16_t *buf, size_t n, bool *ended, bool *error)
{
    *ended = false;
    *error = false;
    size_t got = 0;

    switch (s_act.kind) {
    case PLAY_SONG:
    case PLAY_PREVIEW:
        got = mp3_stream_read(&s_song, buf, n);
        if (mp3_stream_sample_rate(&s_song) > 0) {
            set_rate((uint32_t)mp3_stream_sample_rate(&s_song));
            if (s_act.kind == PLAY_PREVIEW) {
                if (s_preview_left == 0) {
                    s_preview_left = (size_t)mp3_stream_sample_rate(&s_song) * PREVIEW_SECONDS;
                }
                if (got >= s_preview_left) {
                    got = s_preview_left;
                    *ended = true;
                }
                s_preview_left -= got;
            }
        }
        if (got == 0) {
            *ended = true;
            *error = s_song_file.io_error || mp3_stream_status(&s_song) == MP3_ERR_NO_FRAMES;
            if (s_song_file.io_error) {
                sdcard_mark_failed();
            }
        }
        audio_apply_volume(buf, got, s_act.volume);
        break;
    case PLAY_CLIP: {
        size_t len = s_clips.length[s_act.clip_index];
        size_t take = len - s_clip_pos;
        if (take > n) {
            take = n;
        }
        memcpy(buf, s_clips.pcm + s_clips.offset[s_act.clip_index] + s_clip_pos, take * sizeof(int16_t));
        s_clip_pos += take;
        audio_apply_volume(buf, take, s_act.volume);
        got = take;
        *ended = s_clip_pos >= len;
        break;
    }
    case PLAY_BEEP_LOOP:
    case PLAY_BEEP_TICK:
    case PLAY_CONFIRM:
        got = beep_fill(&s_beep, buf, n);   /* 비프는 이미 볼륨이 들어가 있다 */
        *ended = got < n;
        break;
    case PLAY_NONE:
    default:
        *ended = true;
        break;
    }
    return got;
}

static void audio_task(void *arg)
{
    (void)arg;
    static int16_t buf[AUDIO_CHUNK];
    audio_cmd_t cmd;

    for (;;) {
        /* 재생할 것이 없으면 명령이 올 때까지 잔다. DMA는 auto_clear로 0을 내보낸다 */
        TickType_t wait = s_act.kind == PLAY_NONE ? portMAX_DELAY : 0;
        while (xQueueReceive(s_cmd_q, &cmd, wait) == pdTRUE) {
            handle_cmd(&cmd);
            wait = 0;
        }
        if (s_act.kind == PLAY_NONE) {
            continue;
        }

        bool ended = false, error = false;
        size_t got = fill(buf, AUDIO_CHUNK, &ended, &error);
        if (got > 0) {
            size_t written = 0;
            i2s_channel_write(s_tx, buf, got * sizeof(int16_t), &written, portMAX_DELAY);
        }
        if (ended) {
            if (error) {
                ESP_LOGW(TAG, "source failed, falling back");
            }
            /* 곡이 끝나면 같은 곡을 처음부터 다시 연다(반복), 실패면 비프로 */
            start_action(audio_player_on_end(&s_player, error));
        }
    }
}

void audio_post(const audio_cmd_t *cmd)
{
    if (s_cmd_q == NULL) {
        return;
    }
    if (xQueueSend(s_cmd_q, cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "queue full, dropped op %d", (int)cmd->op);
    }
}

esp_err_t audio_start(int bclk_gpio, int ws_gpio, int dout_gpio)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 8;
    chan_cfg.dma_frame_num = 576;   /* 44.1kHz에서 약 100ms 여유: SD 읽기가 잠깐 늦어도 끊기지 않게 */
    chan_cfg.auto_clear = true;     /* 보낼 데이터가 없으면 0을 내보내 "팝" 소리를 막는다 */
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx, NULL), TAG, "i2s channel");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = bclk_gpio,
            .ws = ws_gpio,
            .dout = dout_gpio,
            .din = I2S_GPIO_UNUSED,
        },
    };
    /* 모노 샘플을 왼쪽·오른쪽 양쪽에 같이 보낸다. MAX98357A는 SD 핀이 떠 있으면 (L+R)/2를 낸다 */
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std_cfg), TAG, "i2s std");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "i2s enable");
    s_rate = AUDIO_SAMPLE_RATE;

    /* 클립 캐시는 PSRAM에 한 번만 잡는다(알람 경로에서 malloc 금지) */
    int16_t *pcm = heap_caps_malloc(CLIP_CACHE_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (pcm == NULL) {
        ESP_LOGE(TAG, "no PSRAM for clip cache: pre-alarm will use beeps");
    }
    clip_cache_init(&s_clips, pcm, pcm != NULL ? CLIP_CACHE_SAMPLES : 0);
    audio_player_init(&s_player);

    s_cmd_q = xQueueCreate(AUDIO_QUEUE_LEN, sizeof(audio_cmd_t));
    if (s_cmd_q == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(audio_task, "audio", AUDIO_TASK_STACK, NULL, AUDIO_TASK_PRIO, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
