#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

/*
 * 재생 결정(순수 C): 명령과 바깥 상태(SD, 파일 유무)를 보고 무엇을 재생할지 정한다.
 * 알람을 놓치지 않는 것이 최우선이라(설계 목표 1, 5.3) 노래·클립이 안 되면 항상 비프음으로 대신한다.
 *
 * | 상황                                         | 결과                          |
 * |----------------------------------------------|-------------------------------|
 * | STOP                                          | NONE, 클립 번호 0으로         |
 * | ALARM_LOOP, SD 정상, 요청 곡 있음             | SONG(요청 곡)                 |
 * | ALARM_LOOP, 요청 곡 없음/이름 위반, 첫 곡 있음 | SONG(첫 곡)                   |
 * | ALARM_LOOP, SD 없음 또는 곡 없음              | BEEP_LOOP                     |
 * | 곡 재생 중 끝남                               | 같은 SONG(반복)               |
 * | 곡 재생 중 오류                               | BEEP_LOOP(이 회차엔 곡 재시도 안 함) |
 * | PRE_TICK, 클립 1개 이상                       | CLIP 0,1,…,n-1,0,…            |
 * | PRE_TICK, 클립 0개                            | BEEP_TICK                     |
 * | 클립·비프 틱·확인음·미리 듣기 끝              | NONE                          |
 * | CONFIRM                                       | CONFIRM                       |
 * | PREVIEW, 곡 있음                              | PREVIEW(10초, 반복 없음)      |
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio.h"
#include "audio_files.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PLAY_NONE = 0,
    PLAY_SONG,          /* path의 MP3, 끝나면 처음부터 반복 */
    PLAY_PREVIEW,       /* path의 MP3를 한 번, 최대 10초 */
    PLAY_CLIP,          /* 클립 캐시의 clip_index번째 한 번 */
    PLAY_BEEP_LOOP,
    PLAY_BEEP_TICK,
    PLAY_CONFIRM,
} play_kind_t;

typedef struct {
    play_kind_t kind;
    uint8_t     volume;
    char        path[AUDIO_PATH_MAX];   /* SONG·PREVIEW */
    uint8_t     clip_index;             /* CLIP */
} play_action_t;

/* 결정에 필요한 바깥 상태. audio_esp.c가 채운다 */
typedef struct {
    bool        sd_ok;
    bool        song_exists;   /* 요청한 노래 파일이 있는지 */
    const char *first_song;    /* /sd/sounds 첫 곡 이름, 없으면 NULL */
    size_t      n_clips;       /* 요청한 클립 세트의 캐시된 클립 수 */
} play_env_t;

typedef struct {
    play_action_t current;
    uint8_t       next_clip;
} audio_player_t;

void          audio_player_init(audio_player_t *p);
play_action_t audio_player_on_cmd(audio_player_t *p, const audio_cmd_t *cmd, const play_env_t *env);
/* 지금 소스가 끝났거나(error=false) 실패했을 때(error=true) 다음에 할 일 */
play_action_t audio_player_on_end(audio_player_t *p, bool error);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_PLAYER_H */
