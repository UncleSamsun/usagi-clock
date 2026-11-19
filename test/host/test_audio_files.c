#include <string.h>

#include "unity.h"

#include "audio_files.h"

void setUp(void) {}
void tearDown(void) {}

void test_name_ok(void)
{
    TEST_ASSERT_TRUE(audio_name_ok("song.mp3"));
    TEST_ASSERT_TRUE(audio_name_ok("우사기 아침.mp3"));   /* UTF-8 */
}

/* Review Focus 4: 경로 이동·숨김 파일 차단 */
void test_path_rejects_traversal(void)
{
    TEST_ASSERT_FALSE(audio_name_ok(""));
    TEST_ASSERT_FALSE(audio_name_ok("../x.mp3"));
    TEST_ASSERT_FALSE(audio_name_ok("a/b.mp3"));
    TEST_ASSERT_FALSE(audio_name_ok("a\\b.mp3"));
    TEST_ASSERT_FALSE(audio_name_ok(".."));
    TEST_ASSERT_FALSE(audio_name_ok(".hidden.mp3"));

    char out[128] = "junk";
    TEST_ASSERT_FALSE(audio_path_join(out, sizeof(out), SD_SOUNDS_DIR, "../boot.mp3"));
    TEST_ASSERT_EQUAL_CHAR('\0', out[0]);
}

void test_name_too_long(void)
{
    char name[AUDIO_NAME_MAX + 1];
    memset(name, 'a', AUDIO_NAME_MAX);
    name[AUDIO_NAME_MAX] = '\0';
    TEST_ASSERT_FALSE(audio_name_ok(name));
    name[AUDIO_NAME_MAX - 1] = '\0';   /* 63바이트 */
    TEST_ASSERT_TRUE(audio_name_ok(name));
}

void test_path_join(void)
{
    char out[128];
    TEST_ASSERT_TRUE(audio_path_join(out, sizeof(out), SD_SOUNDS_DIR, "a.mp3"));
    TEST_ASSERT_EQUAL_STRING("/sd/sounds/a.mp3", out);

    char small[12];
    TEST_ASSERT_FALSE(audio_path_join(small, sizeof(small), SD_SOUNDS_DIR, "a.mp3"));
    TEST_ASSERT_EQUAL_CHAR('\0', small[0]);
}

void test_is_mp3_name(void)
{
    TEST_ASSERT_TRUE(audio_is_mp3_name("a.mp3"));
    TEST_ASSERT_TRUE(audio_is_mp3_name("B.MP3"));
    TEST_ASSERT_TRUE(audio_is_mp3_name("c.Mp3"));
    TEST_ASSERT_FALSE(audio_is_mp3_name("a.wav"));
    TEST_ASSERT_FALSE(audio_is_mp3_name("mp3"));
    TEST_ASSERT_FALSE(audio_is_mp3_name(".a.mp3"));
    TEST_ASSERT_FALSE(audio_is_mp3_name("a.mp3.txt"));
}

void test_sort_names(void)
{
    char names[3][AUDIO_NAME_MAX] = { "02_na.mp3", "01_u.mp3", "10_x.mp3" };
    audio_sort_names(names, 3);
    TEST_ASSERT_EQUAL_STRING("01_u.mp3", names[0]);
    TEST_ASSERT_EQUAL_STRING("02_na.mp3", names[1]);
    TEST_ASSERT_EQUAL_STRING("10_x.mp3", names[2]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_name_ok);
    RUN_TEST(test_path_rejects_traversal);
    RUN_TEST(test_name_too_long);
    RUN_TEST(test_path_join);
    RUN_TEST(test_is_mp3_name);
    RUN_TEST(test_sort_names);
    return UNITY_END();
}
