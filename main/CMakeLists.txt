idf_component_register(
    SRCS "main.c" "dev_console_esp.c"
    INCLUDE_DIRS "."
    REQUIRES alarm_core button ds3231 timekeeper console
)
