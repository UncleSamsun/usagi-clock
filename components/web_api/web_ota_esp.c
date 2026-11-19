#include "web_internal.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ota";

#define OTA_CHUNK 8192

static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(2000));   /* 응답이 휴대폰에 도착할 시간 */
    esp_restart();
}

/* POST /api/ota  본문 = build/usagi_clock.bin */
static esp_err_t ota_post(httpd_req_t *req)
{
    if (g_web_hooks->session_active()) {
        return web_send_error(req, "409 Conflict", "an alarm is active");
    }
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (next == NULL || req->content_len == 0 || req->content_len > next->size) {
        return web_send_error(req, "400 Bad Request", "bad firmware size");
    }

    esp_ota_handle_t h = 0;
    esp_err_t err = esp_ota_begin(next, req->content_len, &h);
    if (err != ESP_OK) {
        return web_send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }

    static char chunk[OTA_CHUNK];
    size_t left = req->content_len;
    while (left > 0) {
        int r = httpd_req_recv(req, chunk, left < sizeof(chunk) ? left : sizeof(chunk));
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0 || esp_ota_write(h, chunk, (size_t)r) != ESP_OK) {
            esp_ota_abort(h);   /* 지금 펌웨어는 그대로 */
            ESP_LOGW(TAG, "upload interrupted");
            return web_send_error(req, "400 Bad Request", "upload interrupted");
        }
        left -= (size_t)r;
    }

    err = esp_ota_end(h);   /* 이미지 검증(체크섬, 칩 종류) */
    if (err != ESP_OK) {
        return web_send_error(req, "400 Bad Request", "invalid firmware image");
    }
    err = esp_ota_set_boot_partition(next);
    if (err != ESP_OK) {
        return web_send_error(req, "500 Internal Server Error", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "update written to %s, restarting", next->label);
    web_send_json(req, "200 OK", "{\"ok\":true,\"restarting\":true}");
    xTaskCreate(restart_task, "restart", 2048, NULL, 5, NULL);
    return ESP_OK;
}

void web_ota_register(httpd_handle_t server)
{
    const httpd_uri_t route = { .uri = "/api/ota", .method = HTTP_POST, .handler = ota_post };
    httpd_register_uri_handler(server, &route);
}

void web_ota_mark_valid_if_pending(void)
{
    esp_ota_img_states_t state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "new firmware confirmed (rollback cancelled)");
    }
}
