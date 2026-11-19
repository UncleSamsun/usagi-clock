#include "store_codec.h"

#include <string.h>

_Static_assert(sizeof(alarm_t) == 128, "alarm_t layout changed: bump STORE_VERSION and add a migration");

uint32_t store_crc32(const uint8_t *p, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static void put_u16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static uint16_t get_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static size_t encode(uint32_t magic, uint16_t count, const void *payload, size_t len, uint8_t *out, size_t cap)
{
    if (cap < STORE_HEADER_BYTES + len) {
        return 0;
    }
    if (len > 0) {
        memcpy(out + STORE_HEADER_BYTES, payload, len);
    }
    put_u32(out, magic);
    put_u16(out + 4, STORE_VERSION);
    put_u16(out + 6, count);
    put_u32(out + 8, (uint32_t)len);
    put_u32(out + 12, store_crc32(out + STORE_HEADER_BYTES, len));
    return STORE_HEADER_BYTES + len;
}

/* 헤더·길이·CRC 확인. 맞으면 payload 위치와 개수 */
static bool check(const uint8_t *in, size_t len, uint32_t magic, const uint8_t **payload, uint16_t *count, size_t *plen)
{
    if (len < STORE_HEADER_BYTES || get_u32(in) != magic || get_u16(in + 4) != STORE_VERSION) {
        return false;
    }
    uint32_t n = get_u32(in + 8);
    if ((size_t)n != len - STORE_HEADER_BYTES) {
        return false;
    }
    if (store_crc32(in + STORE_HEADER_BYTES, n) != get_u32(in + 12)) {
        return false;
    }
    *payload = in + STORE_HEADER_BYTES;
    *count = get_u16(in + 6);
    *plen = n;
    return true;
}

size_t store_encode_alarms(const alarm_t *a, size_t n, uint8_t *out, size_t cap)
{
    if (n > ALARM_MAX) {
        return 0;
    }
    return encode(STORE_MAGIC_ALARMS, (uint16_t)n, a, n * sizeof(alarm_t), out, cap);
}

bool store_decode_alarms(const uint8_t *in, size_t len, alarm_t *out, size_t *n, size_t *dropped)
{
    const uint8_t *p;
    uint16_t count;
    size_t plen;
    if (!check(in, len, STORE_MAGIC_ALARMS, &p, &count, &plen) || count > ALARM_MAX ||
        plen != count * sizeof(alarm_t)) {
        return false;
    }
    *n = 0;
    *dropped = 0;
    for (uint16_t i = 0; i < count; i++) {
        alarm_t a;
        memcpy(&a, p + i * sizeof(alarm_t), sizeof(alarm_t));
        a.label[sizeof(a.label) - 1] = '\0';        /* 문자열 끝 보장 */
        a.clipset[sizeof(a.clipset) - 1] = '\0';
        a.song[sizeof(a.song) - 1] = '\0';
        if (alarm_validate(&a) && a.id != 0) {
            out[(*n)++] = a;
        } else {
            (*dropped)++;
        }
    }
    return true;
}

size_t store_encode_settings(const app_settings_t *s, uint8_t *out, size_t cap)
{
    return encode(STORE_MAGIC_SETTINGS, 1, s, sizeof(*s), out, cap);
}

bool store_decode_settings(const uint8_t *in, size_t len, app_settings_t *out)
{
    const uint8_t *p;
    uint16_t count;
    size_t plen;
    if (!check(in, len, STORE_MAGIC_SETTINGS, &p, &count, &plen) || plen != sizeof(app_settings_t)) {
        return false;
    }
    app_settings_t s;
    memcpy(&s, p, sizeof(s));
    s.device_name[sizeof(s.device_name) - 1] = '\0';
    if (!app_settings_validate(&s)) {
        return false;
    }
    *out = s;
    return true;
}
