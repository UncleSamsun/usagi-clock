#include <string.h>

#include "unity.h"

#include "audio_player.h"

static audio_player_t p;

void setUp(void)
{
    audio_player_init(&p);
}

void tearDown(void) {}

static audio_cmd_t cmd(audio_op_t op, uint8_t vol, const char *name)
{
    audio_cmd_t c;
    memset(&c, 0, sizeof(c));
    c.op = op;
    c.volume = vol;
    strncpy(c.name, name, sizeof(c.name) - 1);
    return c;
}

static const play_env_t ENV_OK = { .sd_ok = true, .song_exists = true, .first_song = "01.mp3", .n_clips = 2 };

void test_song_plays_requested_file(void)
{
    audio_cmd_t c = cmd(AUDIO_OP_ALARM_LOOP, 70, "a.mp3");
    play_action_t a = audio_player_on_cmd(&p, &c, &ENV_OK);
    TEST_ASSERT_EQUAL_INT(PLAY_SONG, a.kind);
    TEST_ASSERT_EQUAL_STRING("/sd/sounds/a.mp3", a.path);
    TEST_ASSERT_EQUAL_UINT8(70, a.volume);
}

/* Review Focus 3: 지정 곡이 없으면 첫 곡 */
void test_missing_song_uses_first_song(void)
{
    play_env_t env = ENV_OK;
    env.song_exists = false;
    audio_cmd_t c = cmd(AUDIO_OP_ALARM_LOOP, 70, "gone.mp3");
    play_action_t a = audio_player_on_cmd(&p, &c, &env);
    TEST_ASSERT_EQUAL_INT(PLAY_SONG, a.kind);
    TEST_ASSERT_EQUAL_STRING("/sd/sounds/01.mp3", a.path);

    /* 규칙 위반 이름은 있다고 해도 쓰지 않는다 */
    c = cmd(AUDIO_OP_ALARM_LOOP, 70, "../x.mp3");
    a = audio_player_on_cmd(&p, &c, &ENV_OK);
    TEST_ASSERT_EQUAL_STRING("/sd/sounds/01.mp3", a.path);
}

void test_empty_song_name_uses_first_song(void)
{
    play_env_t env = ENV_OK;
    env.song_exists = false;
    audio_cmd_t c = cmd(AUDIO_OP_ALARM_LOOP, 70, "");
    play_action_t a = audio_player_on_cmd(&p, &c, &env);
    TEST_ASSERT_EQUAL_STRING("/sd/sounds/01.mp3", a.path);
}

/* Review Focus 3: 곡이 하나도 없으면 비프 */
void test_no_songs_uses_beep_loop(void)
{
    play_env_t env = { .sd_ok = true, .song_exists = false, .first_song = NULL };
    audio_cmd_t c = cmd(AUDIO_OP_ALARM_LOOP, 55, "a.mp3");
    play_action_t a = audio_player_on_cmd(&p, &c, &env);
    TEST_ASSERT_EQUAL_INT(PLAY_BEEP_LOOP, a.kind);
    TEST_ASSERT_EQUAL_UINT8(55, a.volume);
}

/* Review Focus 2: SD카드가 없으면 비프 */
void test_sd_missing_uses_beep_loop(void)
{
    play_env_t env = ENV_OK;
    env.sd_ok = false;
    audio_cmd_t c = cmd(AUDIO_OP_ALARM_LOOP, 70, "a.mp3");
    TEST_ASSERT_EQUAL_INT(PLAY_BEEP_LOOP, audio_player_on_cmd(&p, &c, &env).kind);
}

void test_song_end_loops(void)
{
    audio_cmd_t c = cmd(AUDIO_OP_ALARM_LOOP, 70, "a.mp3");
    audio_player_on_cmd(&p, &c, &ENV_OK);
    play_action_t a = audio_player_on_end(&p, false);
    TEST_ASSERT_EQUAL_INT(PLAY_SONG, a.kind);
    TEST_ASSERT_EQUAL_STRING("/sd/sounds/a.mp3", a.path);
}

/* Review Focus 1: 해독 실패 → 비프로 계속 */
void test_song_error_falls_back_to_beep_loop(void)
{
    audio_cmd_t c = cmd(AUDIO_OP_ALARM_LOOP, 70, "a.mp3");
    audio_player_on_cmd(&p, &c, &ENV_OK);
    play_action_t a = audio_player_on_end(&p, true);
    TEST_ASSERT_EQUAL_INT(PLAY_BEEP_LOOP, a.kind);
    TEST_ASSERT_EQUAL_UINT8(70, a.volume);
    TEST_ASSERT_EQUAL_INT(PLAY_BEEP_LOOP, audio_player_on_end(&p, false).kind);
}

/* Review Focus 2: 재생 중 SD카드를 뽑음(읽기 오류) → 비프 */
void test_sd_removed_during_song_falls_back(void)
{
    audio_cmd_t c = cmd(AUDIO_OP_ALARM_LOOP, 70, "a.mp3");
    audio_player_on_cmd(&p, &c, &ENV_OK);
    TEST_ASSERT_EQUAL_INT(PLAY_BEEP_LOOP, audio_player_on_end(&p, true).kind);
}

void test_pre_ticks_cycle_clips(void)
{
    audio_cmd_t c = cmd(AUDIO_OP_PRE_TICK, 40, "default");
    const uint8_t expect[5] = { 0, 1, 0, 1, 0 };
    for (int i = 0; i < 5; i++) {
        play_action_t a = audio_player_on_cmd(&p, &c, &ENV_OK);
        TEST_ASSERT_EQUAL_INT(PLAY_CLIP, a.kind);
        TEST_ASSERT_EQUAL_UINT8(expect[i], a.clip_index);
    }
    audio_cmd_t stop = cmd(AUDIO_OP_STOP, 0, "");
    audio_player_on_cmd(&p, &stop, &ENV_OK);
    TEST_ASSERT_EQUAL_UINT8(0, audio_player_on_cmd(&p, &c, &ENV_OK).clip_index);
}

/* Review Focus 5: 클립이 없으면 짧은 비프로 박자 유지 */
void test_pre_tick_without_clips_uses_beep_tick(void)
{
    play_env_t env = ENV_OK;
    env.n_clips = 0;
    audio_cmd_t c = cmd(AUDIO_OP_PRE_TICK, 40, "default");
    TEST_ASSERT_EQUAL_INT(PLAY_BEEP_TICK, audio_player_on_cmd(&p, &c, &env).kind);
}

void test_clip_end_is_silence(void)
{
    audio_cmd_t c = cmd(AUDIO_OP_PRE_TICK, 40, "default");
    audio_player_on_cmd(&p, &c, &ENV_OK);
    TEST_ASSERT_EQUAL_INT(PLAY_NONE, audio_player_on_end(&p, false).kind);
}

void test_stop_and_confirm(void)
{
    audio_cmd_t stop = cmd(AUDIO_OP_STOP, 0, "");
    TEST_ASSERT_EQUAL_INT(PLAY_NONE, audio_player_on_cmd(&p, &stop, &ENV_OK).kind);
    audio_cmd_t conf = cmd(AUDIO_OP_CONFIRM, 80, "");
    play_action_t a = audio_player_on_cmd(&p, &conf, &ENV_OK);
    TEST_ASSERT_EQUAL_INT(PLAY_CONFIRM, a.kind);
    TEST_ASSERT_EQUAL_UINT8(80, a.volume);
    TEST_ASSERT_EQUAL_INT(PLAY_NONE, audio_player_on_end(&p, false).kind);
}

void test_preview(void)
{
    audio_cmd_t c = cmd(AUDIO_OP_PREVIEW, 50, "a.mp3");
    play_action_t a = audio_player_on_cmd(&p, &c, &ENV_OK);
    TEST_ASSERT_EQUAL_INT(PLAY_PREVIEW, a.kind);
    TEST_ASSERT_EQUAL_STRING("/sd/sounds/a.mp3", a.path);
    TEST_ASSERT_EQUAL_INT(PLAY_NONE, audio_player_on_end(&p, false).kind);

    play_env_t env = ENV_OK;
    env.song_exists = false;
    TEST_ASSERT_EQUAL_INT(PLAY_NONE, audio_player_on_cmd(&p, &c, &env).kind);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_song_plays_requested_file);
    RUN_TEST(test_missing_song_uses_first_song);
    RUN_TEST(test_empty_song_name_uses_first_song);
    RUN_TEST(test_no_songs_uses_beep_loop);
    RUN_TEST(test_sd_missing_uses_beep_loop);
    RUN_TEST(test_song_end_loops);
    RUN_TEST(test_song_error_falls_back_to_beep_loop);
    RUN_TEST(test_sd_removed_during_song_falls_back);
    RUN_TEST(test_pre_ticks_cycle_clips);
    RUN_TEST(test_pre_tick_without_clips_uses_beep_tick);
    RUN_TEST(test_clip_end_is_silence);
    RUN_TEST(test_stop_and_confirm);
    RUN_TEST(test_preview);
    return UNITY_END();
}
