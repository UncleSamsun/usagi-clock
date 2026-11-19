#include "net.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static const char *TAG = "captive";

static TaskHandle_t s_task;
static volatile bool s_run;

/* 설정 모드 DNS 서버: 모든 이름 → 192.168.4.1 (응답 만들기는 dns_reply.c) */
static void dns_task(void *arg)
{
    (void)arg;
    static const uint8_t ip[4] = { 192, 168, 4, 1 };
    static uint8_t q[512], out[600];

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in addr = { .sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (sock < 0 || bind(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        ESP_LOGE(TAG, "dns socket failed");
        if (sock >= 0) {
            close(sock);
        }
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    struct timeval tv = { .tv_sec = 1 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ESP_LOGI(TAG, "dns started");

    while (s_run) {
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        int n = recvfrom(sock, q, sizeof(q), 0, (struct sockaddr *)&from, &flen);
        if (n <= 0) {
            continue;
        }
        size_t r = dns_build_reply(q, (size_t)n, ip, out, sizeof(out));
        if (r > 0) {
            sendto(sock, out, r, 0, (struct sockaddr *)&from, flen);
        }
    }
    close(sock);
    ESP_LOGI(TAG, "dns stopped");
    s_task = NULL;
    vTaskDelete(NULL);
}

void captive_dns_start(void)
{
    if (s_task != NULL) {
        return;
    }
    s_run = true;
    xTaskCreate(dns_task, "dns", 3072, NULL, 4, &s_task);
}

void captive_dns_stop(void)
{
    s_run = false;   /* 1초 안에 작업이 스스로 끝난다 */
}
