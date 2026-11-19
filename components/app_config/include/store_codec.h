#ifndef STORE_CODEC_H
#define STORE_CODEC_H

/*
 * 저장 형식(순수 C): 알람 목록과 설정을 NVS blob으로 바꾸고 되돌린다.
 *
 * 헤더 16바이트(리틀 엔디언): magic u32, version u16, count u16, payload_len u32, crc32 u32
 * 그 뒤 payload. 읽을 때 헤더·길이·CRC가 하나라도 맞지 않으면 통째로 버린다.
 * 구조체가 바뀌면 STORE_VERSION을 올리고 이전 버전 변환을 추가해야 한다.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "alarm_core.h"
#include "app_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORE_MAGIC_ALARMS     0x41475355u   /* "USGA" */
#define STORE_MAGIC_SETTINGS   0x53475355u   /* "USGS" */
#define STORE_VERSION          1
#define STORE_HEADER_BYTES     16
#define STORE_ALARMS_MAX_BYTES (STORE_HEADER_BYTES + ALARM_MAX * sizeof(alarm_t))
#define STORE_SETTINGS_BYTES   (STORE_HEADER_BYTES + sizeof(app_settings_t))

uint32_t store_crc32(const uint8_t *p, size_t n);   /* IEEE 802.3, "123456789" → 0xCBF43926 */

size_t store_encode_alarms(const alarm_t *a, size_t n, uint8_t *out, size_t cap);   /* 쓴 바이트, 실패 0 */
/* 알람 중 alarm_validate 실패는 빼고 *dropped에 개수 */
bool   store_decode_alarms(const uint8_t *in, size_t len, alarm_t *out, size_t *n, size_t *dropped);
size_t store_encode_settings(const app_settings_t *s, uint8_t *out, size_t cap);
bool   store_decode_settings(const uint8_t *in, size_t len, app_settings_t *out);   /* 검사 실패도 false */

#ifdef __cplusplus
}
#endif

#endif /* STORE_CODEC_H */
