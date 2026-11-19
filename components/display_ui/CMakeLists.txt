# ILI9341 + LVGL 화면, 백라이트 PWM. 문자열은 ui_text(순수 C)가 만든다.
idf_component_register(
    SRCS "display_ui_esp.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd
    PRIV_REQUIRES ui_text esp_driver_spi esp_driver_ledc freertos
)
