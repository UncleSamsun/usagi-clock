#include <string.h>

#include "unity.h"

#include "web_api.h"

static char out[128];

void setUp(void) { memset(out, 'x', sizeof(out)); }
void tearDown(void) {}

void test_sanitize_keeps_normal(void)
{
    TEST_ASSERT_TRUE(web_sanitize_upload_name("Morning Song.mp3", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("Morning Song.mp3", out);
    TEST_ASSERT_TRUE(web_sanitize_upload_name("아침 노래.MP3", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("아침 노래.MP3", out);
}

/* Review Focus 5 */
void test_sanitize_replaces_forbidden(void)
{
    TEST_ASSERT_TRUE(web_sanitize_upload_name("a:b*c?.mp3", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("a_b_c_.mp3", out);
    TEST_ASSERT_TRUE(web_sanitize_upload_name("../x.mp3", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("_x.mp3", out);
    TEST_ASSERT_TRUE(web_sanitize_upload_name("song", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("song.mp3", out);
    TEST_ASSERT_TRUE(web_sanitize_upload_name("tab\there.mp3", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("tab_here.mp3", out);
}

void test_sanitize_trims(void)
{
    TEST_ASSERT_TRUE(web_sanitize_upload_name("  a.mp3  ", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("a.mp3", out);
    TEST_ASSERT_TRUE(web_sanitize_upload_name("...a.mp3", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("a.mp3", out);
}

/* 올바른 UTF-8인지(잘린 글자 없음) */
static bool valid_utf8(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int extra = (*p < 0x80) ? 0 : ((*p & 0xE0) == 0xC0) ? 1 : ((*p & 0xF0) == 0xE0) ? 2 : ((*p & 0xF8) == 0xF0) ? 3 : -1;
        if (extra < 0) {
            return false;
        }
        p++;
        for (int i = 0; i < extra; i++, p++) {
            if ((*p & 0xC0) != 0x80) {
                return false;
            }
        }
    }
    return true;
}

/* Review Focus 5: 한글 30글자(90바이트) → 63바이트 이하, 글자 중간에서 자르지 않음 */
void test_sanitize_truncates_on_utf8_boundary(void)
{
    char in[128] = "";
    for (int i = 0; i < 30; i++) {
        strcat(in, "가");
    }
    strcat(in, ".mp3");
    TEST_ASSERT_TRUE(web_sanitize_upload_name(in, out, sizeof(out)));
    TEST_ASSERT_LESS_OR_EQUAL_size_t(63, strlen(out));
    TEST_ASSERT_EQUAL_STRING(".mp3", out + strlen(out) - 4);
    TEST_ASSERT_TRUE(valid_utf8(out));
    TEST_ASSERT_EQUAL_size_t(19 * 3 + 4, strlen(out));   /* 59바이트 중 글자 단위로 57바이트 */
}

void test_sanitize_rejects_empty(void)
{
    TEST_ASSERT_FALSE(web_sanitize_upload_name("", out, sizeof(out)));
    TEST_ASSERT_FALSE(web_sanitize_upload_name(".mp3", out, sizeof(out)));
    TEST_ASSERT_FALSE(web_sanitize_upload_name("   ", out, sizeof(out)));
    TEST_ASSERT_FALSE(web_sanitize_upload_name("...", out, sizeof(out)));
}

void test_url_decode(void)
{
    char d[64];
    TEST_ASSERT_TRUE(web_url_decode("a%20b+c.mp3", d, sizeof(d)));
    TEST_ASSERT_EQUAL_STRING("a b c.mp3", d);
    TEST_ASSERT_TRUE(web_url_decode("%EA%B0%80.mp3", d, sizeof(d)));
    TEST_ASSERT_EQUAL_STRING("가.mp3", d);
    TEST_ASSERT_FALSE(web_url_decode("bad%2", d, sizeof(d)));
    TEST_ASSERT_FALSE(web_url_decode("bad%zz", d, sizeof(d)));
    TEST_ASSERT_FALSE(web_url_decode("toolong", d, 4));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sanitize_keeps_normal);
    RUN_TEST(test_sanitize_replaces_forbidden);
    RUN_TEST(test_sanitize_trims);
    RUN_TEST(test_sanitize_truncates_on_utf8_boundary);
    RUN_TEST(test_sanitize_rejects_empty);
    RUN_TEST(test_url_decode);
    return UNITY_END();
}
