# 네트워크. net_policy.c·dns_reply.c(순수 C)는 PC 테스트, *_esp.c는 Wi-Fi·SNTP·mDNS·DNS 서버.
idf_component_register(
    SRCS "net_policy.c" "dns_reply.c" "net_esp.c" "captive_esp.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_timer lwip timekeeper freertos
)
