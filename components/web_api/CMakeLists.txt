# 웹 API. web_json.c·upload_name.c(순수 C)는 PC 테스트, *_esp.c는 HTTP 서버·업로드·OTA.
idf_component_register(
    SRCS "web_json.c" "upload_name.c" "web_server_esp.c" "web_sounds_esp.c" "web_ota_esp.c"
    INCLUDE_DIRS "include"
    REQUIRES alarm_core app_config
    PRIV_REQUIRES esp_http_server app_update esp_app_format audio net timekeeper freertos
)
