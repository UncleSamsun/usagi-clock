#include "web_api.h"

#include <ctype.h>
#include <string.h>

#include "audio_files.h"

static bool forbidden(unsigned char c)
{
    return c < 0x20 || c == 0x7F || strchr("\\/:*?\"<>|", c) != NULL;
}

/* UTF-8에서 연속 바이트(10xxxxxx)인지 */
static bool is_cont(unsigned char c)
{
    return (c & 0xC0) == 0x80;
}

static bool ends_with_mp3(const char *s, size_t len)
{
    return len >= 4 && s[len - 4] == '.' && tolower((unsigned char)s[len - 3]) == 'm' &&
           tolower((unsigned char)s[len - 2]) == 'p' && s[len - 1] == '3';
}

bool web_sanitize_upload_name(const char *in, char *out, size_t n)
{
    char tmp[256];
    size_t len = 0;

    if (n == 0) {
        return false;
    }
    out[0] = '\0';

    /* 1) 금지 문자 바꾸기 */
    for (size_t i = 0; in[i] != '\0' && len < sizeof(tmp) - 1; i++) {
        unsigned char c = (unsigned char)in[i];
        tmp[len++] = forbidden(c) ? '_' : (char)c;
    }
    tmp[len] = '\0';

    /* 2) 앞뒤 공백 제거 후 확장자를 떼어 둔다(.mp3가 아니면 나중에 붙인다) */
    while (len > 0 && tmp[len - 1] == ' ') {
        len--;
    }
    tmp[len] = '\0';
    char ext_buf[5] = ".mp3";
    if (ends_with_mp3(tmp, len)) {
        memcpy(ext_buf, tmp + len - 4, 4);   /* 원래 대소문자 유지 */
        len -= 4;
    }
    const char *ext = ext_buf;

    /* 3) 본문 앞뒤 공백·점 제거("../x" → "_x", ".mp3"만 있던 이름 → 빈 값) */
    size_t start = 0;
    while (start < len && (tmp[start] == ' ' || tmp[start] == '.')) {
        start++;
    }
    while (len > start && (tmp[len - 1] == ' ' || tmp[len - 1] == '.')) {
        len--;
    }
    if (len == start) {
        return false;
    }
    const char *name = tmp + start;
    size_t base = len - start;

    /* 4) 전체 63바이트(AUDIO_NAME_MAX - 1) 이하. 본문을 UTF-8 글자 경계에서 자른다 */
    size_t max_total = (n - 1 < AUDIO_NAME_MAX - 1) ? n - 1 : AUDIO_NAME_MAX - 1;
    if (max_total <= 4) {
        return false;
    }
    size_t max_base = max_total - 4;
    if (base > max_base) {
        base = max_base;
        while (base > 0 && is_cont((unsigned char)name[base])) {
            base--;   /* 글자 중간이면 그 글자 시작까지 물러난다 */
        }
        while (base > 0 && name[base - 1] == ' ') {
            base--;
        }
        if (base == 0) {
            return false;
        }
    }

    memcpy(out, name, base);
    memcpy(out + base, ext, 4);
    out[base + 4] = '\0';
    if (!audio_name_ok(out)) {
        out[0] = '\0';
        return false;
    }
    return true;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    c = (char)tolower((unsigned char)c);
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

bool web_url_decode(const char *in, char *out, size_t n)
{
    size_t o = 0;
    for (size_t i = 0; in[i] != '\0'; i++) {
        if (o + 1 >= n) {
            return false;
        }
        if (in[i] == '%') {
            int hi = hexval(in[i + 1]);
            int lo = hi >= 0 ? hexval(in[i + 2]) : -1;
            if (hi < 0 || lo < 0) {
                return false;
            }
            out[o++] = (char)(hi * 16 + lo);
            i += 2;
        } else if (in[i] == '+') {
            out[o++] = ' ';
        } else {
            out[o++] = in[i];
        }
    }
    out[o] = '\0';
    return true;
}
