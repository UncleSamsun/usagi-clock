# 순수 C 컴포넌트: esp_*, freertos/*, driver/* 헤더를 쓰지 않는다.
# 같은 소스를 PC 테스트(test/host)에서도 그대로 컴파일한다.
# .c 파일을 추가하면 SRCS 목록에도 추가한다.
idf_component_register(
    SRCS "alarm_time.c" "alarm_validate.c"
    INCLUDE_DIRS "include"
)
