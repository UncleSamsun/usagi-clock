#ifndef WEB_INTERNAL_H
#define WEB_INTERNAL_H

/* web_api 컴포넌트 안에서만 쓰는 함수(ESP 전용) */

#include "esp_http_server.h"
#include "web_api.h"

extern const web_hooks_t *g_web_hooks;

esp_err_t web_send_json(httpd_req_t *req, const char *status, const char *json);
esp_err_t web_send_error(httpd_req_t *req, const char *status, const char *msg);
/* 본문 전체를 buf에 읽는다(NUL로 끝냄). 너무 크면 413을 보내고 ESP_FAIL */
esp_err_t web_read_body(httpd_req_t *req, char *buf, size_t cap);
/* 쿼리 문자열의 key 값을 URL 디코드해서 out에. 없으면 false */
bool      web_query_get(httpd_req_t *req, const char *key, char *out, size_t n);

void      web_sounds_register(httpd_handle_t server);
void      web_ota_register(httpd_handle_t server);

#endif /* WEB_INTERNAL_H */
