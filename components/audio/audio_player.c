#include "audio_player.h"

#include <string.h>

void audio_player_init(audio_player_t *p)
{
    memset(p, 0, sizeof(*p));
}

static play_action_t make(play_kind_t kind, uint8_t volume)
{
    play_action_t a;
    memset(&a, 0, sizeof(a));
    a.kind = kind;
    a.volume = volume;
    return a;
}

/* 재생할 곡의 경로를 정한다: 요청 곡 → 첫 곡 → 없음(false) */
static bool pick_song(const audio_cmd_t *cmd, const play_env_t *env, char *path, size_t n)
{
    if (!env->sd_ok) {
        return false;
    }
    if (env->song_exists && audio_path_join(path, n, SD_SOUNDS_DIR, cmd->name)) {
        return true;
    }
    if (env->first_song != NULL && audio_path_join(path, n, SD_SOUNDS_DIR, env->first_song)) {
        return true;
    }
    return false;
}

play_action_t audio_player_on_cmd(audio_player_t *p, const audio_cmd_t *cmd, const play_env_t *env)
{
    play_action_t a = make(PLAY_NONE, cmd->volume);

    switch (cmd->op) {
    case AUDIO_OP_STOP:
        a.volume = 0;
        p->next_clip = 0;
        break;
    case AUDIO_OP_ALARM_LOOP:
        a.kind = pick_song(cmd, env, a.path, sizeof(a.path)) ? PLAY_SONG : PLAY_BEEP_LOOP;
        break;
    case AUDIO_OP_PREVIEW:
        /* 미리 듣기는 요청한 곡만. 없으면 조용히 */
        if (env->sd_ok && env->song_exists && audio_path_join(a.path, sizeof(a.path), SD_SOUNDS_DIR, cmd->name)) {
            a.kind = PLAY_PREVIEW;
        }
        break;
    case AUDIO_OP_PRE_TICK:
        if (env->n_clips > 0) {
            if (p->next_clip >= env->n_clips) {
                p->next_clip = 0;
            }
            a.kind = PLAY_CLIP;
            a.clip_index = p->next_clip;
            p->next_clip = (uint8_t)((p->next_clip + 1) % env->n_clips);
        } else {
            a.kind = PLAY_BEEP_TICK;
        }
        break;
    case AUDIO_OP_CONFIRM:
        a.kind = PLAY_CONFIRM;
        break;
    default:
        break;
    }

    p->current = a;
    return a;
}

play_action_t audio_player_on_end(audio_player_t *p, bool error)
{
    play_action_t a = p->current;

    switch (a.kind) {
    case PLAY_SONG:
        if (error) {
            a = make(PLAY_BEEP_LOOP, a.volume);   /* 노래가 안 되면 비프로라도 울린다 */
        }
        break;
    case PLAY_BEEP_LOOP:
        break;                                    /* 반복 */
    case PLAY_NONE:
    case PLAY_PREVIEW:
    case PLAY_CLIP:
    case PLAY_BEEP_TICK:
    case PLAY_CONFIRM:
    default:
        a = make(PLAY_NONE, 0);
        break;
    }

    p->current = a;
    return a;
}
