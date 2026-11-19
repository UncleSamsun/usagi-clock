# 스누즈 바 버튼. button_fsm.c(순수 C)는 디바운스와 짧게/길게 판별, button_gpio_esp.c는 10ms 폴링 작업.
# 컴포넌트 이름을 "button"으로 하면 Espressif의 button(iot_button) 컴포넌트와 겹쳐 esp_lvgl_port가 잘못 빌드된다.
idf_component_register(
    SRCS "button_fsm.c" "button_gpio_esp.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_driver_gpio esp_timer freertos
)
