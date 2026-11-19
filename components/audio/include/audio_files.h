#ifndef AUDIO_FILES_H
#define AUDIO_FILES_H

/*
 * SD카드 파일 이름 규칙(순수 C).
 * 노래 /sd/sounds/<이름>.mp3, 클립 세트 /sd/clips/<세트>/<이름>.mp3 (설계 5.6)
 */

#include <stdbool.h>
#include <stddef.h>

#include "audio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_PATH_MAX   128
#define SD_MOUNT_POINT   "/sd"
#define SD_SOUNDS_DIR    "/sd/sounds"
#define SD_CLIPS_DIR     "/sd/clips"
#define CLIPSET_DEFAULT  "default"

/* 빈 값, '/', '\\', "..", '.'으로 시작, AUDIO_NAME_MAX 이상 → false */
bool audio_name_ok(const char *name);
/* ".mp3"로 끝남(대소문자 무시). 숨김 파일('.'으로 시작)은 false */
bool audio_is_mp3_name(const char *name);
/* dir + "/" + name. name이 audio_name_ok가 아니거나 n을 넘으면 false, out은 빈 문자열 */
bool audio_path_join(char *out, size_t n, const char *dir, const char *name);
/* 이름 배열을 바이트 순(strcmp)으로 정렬 */
void audio_sort_names(char names[][AUDIO_NAME_MAX], size_t count);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_FILES_H */
