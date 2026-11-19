idf_component_register(
    SRCS "main.c" "dev_console_esp.c" "alarm_service_esp.c"
    INCLUDE_DIRS "."
    REQUIRES alarm_core snooze_button ds3231 timekeeper display_ui audio app_config net web_api
             console esp_driver_gpio esp_timer
)

# 웹 페이지(web/)를 LittleFS 이미지로 만들어 storage 파티션에 함께 굽는다(joltwallet/littlefs)
littlefs_create_partition_image(storage ../web FLASH_IN_PROJECT)
