# 오디오. 순수 C(audio_tone/beep/files, mp3_stream, audio_player, clip_cache)는 PC 테스트,
# *_esp.c는 SD카드와 I2S 출력. minimp3는 third_party(경고를 오류로 보지 않음).
idf_component_register(
    SRCS "audio_tone.c" "audio_beep.c" "audio_files.c" "mp3_stream.c" "audio_player.c" "clip_cache.c"
         "sdcard_esp.c" "audio_esp.c" "third_party/minimp3_impl.c"
    INCLUDE_DIRS "include" "third_party"
    PRIV_REQUIRES esp_driver_i2s esp_driver_sdspi esp_driver_spi fatfs sdmmc esp_timer freertos
)
set_source_files_properties("third_party/minimp3_impl.c" PROPERTIES COMPILE_OPTIONS "-Wno-error;-w")
