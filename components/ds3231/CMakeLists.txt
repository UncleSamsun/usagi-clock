# DS3231 RTC 드라이버. ds3231_codec.c는 순수 C(PC 테스트), ds3231_esp.c는 I2C 입출력.
idf_component_register(
    SRCS "ds3231_codec.c" "ds3231_esp.c"
    INCLUDE_DIRS "include"
    REQUIRES alarm_core
    PRIV_REQUIRES esp_driver_i2c
)
