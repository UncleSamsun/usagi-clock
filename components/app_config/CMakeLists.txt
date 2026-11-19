# 앱 설정. app_config.c·store_codec.c(순수 C)는 값·검사·저장 형식, storage_esp.c는 NVS.
idf_component_register(
    SRCS "app_config.c" "store_codec.c" "storage_esp.c"
    INCLUDE_DIRS "include"
    REQUIRES alarm_core
    PRIV_REQUIRES nvs_flash freertos
)
