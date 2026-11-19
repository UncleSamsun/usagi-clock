# 시간 관리. time_rules.c는 순수 C(PC 테스트), timekeeper_esp.c는 RTC·시스템 시간 입출력.
idf_component_register(
    SRCS "time_rules.c" "timekeeper_esp.c"
    INCLUDE_DIRS "include"
    REQUIRES ds3231
    PRIV_REQUIRES alarm_core
)
