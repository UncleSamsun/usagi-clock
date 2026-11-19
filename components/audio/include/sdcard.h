#ifndef SDCARD_H
#define SDCARD_H

/*
 * SD카드(SPI3, FAT32). 마운트 지점 /sd.
 * 사용자 파일이 들어 있으므로 절대 포맷하지 않는다.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio_files.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { int sck, mosi, miso, cs; } sdcard_pins_t;

/* SPI3 버스를 만들고 마운트를 시도한다. 카드가 없어도 ESP_OK(상태만 기록) */
esp_err_t sdcard_start(const sdcard_pins_t *pins);
/* 마운트돼 있으면 true. 아니면 마지막 시도 30초 뒤부터 다시 시도한다 */
bool      sdcard_ensure_mounted(void);
/* 읽기 오류가 나면 부른다: 언마운트하고 다음 ensure에서 다시 시도 */
void      sdcard_mark_failed(void);
bool      sdcard_is_mounted(void);
/* dir 안의 .mp3 이름을 정렬해 최대 max개 채운다. 폴더가 없거나 마운트 안 됐으면 0 */
size_t    sdcard_list_mp3(const char *dir, char names[][AUDIO_NAME_MAX], size_t max);
/* dir 안의 하위 폴더 이름(클립 세트)을 정렬해 최대 max개 채운다 */
size_t    sdcard_list_dirs(const char *dir, char names[][AUDIO_NAME_MAX], size_t max);
bool      sdcard_file_exists(const char *path);
/* 전체·남은 용량(바이트). 마운트 안 됐으면 false */
bool      sdcard_space(uint64_t *total, uint64_t *free_bytes);

#ifdef __cplusplus
}
#endif

#endif /* SDCARD_H */
