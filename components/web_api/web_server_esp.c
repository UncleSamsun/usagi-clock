#include "web_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "audio_files.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "net.h"
#include "sdcard.h"
#include "timekeeper.h"

static const char *TAG = "web";

#define WWW_ROOT  "/www"
#define WWW_LABEL "storage"

const web_hooks_t *g_web_hooks;
static httpd_handle_t s_server;
static volatile bool s_captive;

/* ===== 공통 ===== */

esp_err_t web_send_json(httpd_req_t *req, const char *status, const char *json)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

esp_err_t web_send_error(httpd_req_t *req, const char *status, const char *msg)
{
    char buf[160];
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "error", msg);
    if (!cJSON_PrintPreallocated(o, buf, sizeof(buf), false)) {
        strcpy(buf, "{\"error\":\"error\"}");
    }
    cJSON_Delete(o);
    return web_send_json(req, status, buf);
}

esp_err_t web_read_body(httpd_req_t *req, char *buf, size_t cap)
{
    if (req->content_len >= cap) {
        web_send_error(req, "413 Payload Too Large", "body too large");
        return ESP_FAIL;
    }
    size_t got = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, buf + got, req->content_len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            return ESP_FAIL;
        }
        got += (size_t)r;
    }
    buf[got] = '\0';
    return ESP_OK;
}

bool web_query_get(httpd_req_t *req, const char *key, char *out, size_t n)
{
    char query[256], raw[192];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, key, raw, sizeof(raw)) != ESP_OK) {
        return false;
    }
    return web_url_decode(raw, out, n);
}

/* "/api/alarms/12" 같은 경로의 마지막 숫자 */
static bool path_id(const char *uri, const char *prefix, uint8_t *id)
{
    size_t pl = strlen(prefix);
    if (strncmp(uri, prefix, pl) != 0) {
        return false;
    }
    char *end = NULL;
    long v = strtol(uri + pl, &end, 10);
    if (end == uri + pl || (*end != '\0' && *end != '?') || v < 1 || v > 255) {
        return false;
    }
    *id = (uint8_t)v;
    return true;
}

/* ===== 알람 ===== */

static char s_json[WEB_JSON_BODY_MAX + 1];      /* 요청 본문 (httpd는 작업 하나에서 순서대로 처리) */
static char s_out[ALARM_MAX * 384 + 64];        /* 응답 */
static alarm_t s_list[ALARM_MAX];

static esp_err_t alarms_get(httpd_req_t *req)
{
    size_t n = g_web_hooks->get_alarms(s_list, ALARM_MAX);
    if (web_alarms_to_json(s_list, n, s_out, sizeof(s_out)) == 0) {
        return web_send_error(req, "500 Internal Server Error", "response too large");
    }
    return web_send_json(req, "200 OK", s_out);
}

static esp_err_t save_list(httpd_req_t *req, size_t n, const alarm_t *changed, const char *ok_status)
{
    esp_err_t err = g_web_hooks->set_alarms(s_list, n);
    if (err != ESP_OK) {
        return web_send_error(req, "400 Bad Request", "invalid alarm");
    }
    if (changed == NULL) {
        httpd_resp_set_status(req, "204 No Content");
        return httpd_resp_send(req, NULL, 0);
    }
    web_alarm_to_json(changed, s_out, sizeof(s_out));
    return web_send_json(req, ok_status, s_out);
}

static esp_err_t alarms_post(httpd_req_t *req)
{
    char err[96];
    alarm_t a;
    if (web_read_body(req, s_json, sizeof(s_json)) != ESP_OK) {
        return ESP_FAIL;
    }
    if (!web_alarm_from_json(s_json, &a, err, sizeof(err))) {
        return web_send_error(req, "400 Bad Request", err);
    }
    size_t n = g_web_hooks->get_alarms(s_list, ALARM_MAX);
    if (n >= ALARM_MAX) {
        return web_send_error(req, "409 Conflict", "alarm list is full (32)");
    }
    a.id = web_pick_alarm_id(s_list, n);
    s_list[n] = a;
    return save_list(req, n + 1, &a, "201 Created");
}

static esp_err_t alarm_put(httpd_req_t *req)
{
    char err[96];
    uint8_t id;
    alarm_t a;
    if (!path_id(req->uri, "/api/alarms/", &id)) {
        return web_send_error(req, "404 Not Found", "no such alarm");
    }
    if (web_read_body(req, s_json, sizeof(s_json)) != ESP_OK) {
        return ESP_FAIL;
    }
    if (!web_alarm_from_json(s_json, &a, err, sizeof(err))) {
        return web_send_error(req, "400 Bad Request", err);
    }
    size_t n = g_web_hooks->get_alarms(s_list, ALARM_MAX);
    for (size_t i = 0; i < n; i++) {
        if (s_list[i].id == id) {
            a.id = id;
            s_list[i] = a;
            return save_list(req, n, &a, "200 OK");
        }
    }
    return web_send_error(req, "404 Not Found", "no such alarm");
}

static esp_err_t alarm_delete(httpd_req_t *req)
{
    uint8_t id;
    if (!path_id(req->uri, "/api/alarms/", &id)) {
        return web_send_error(req, "404 Not Found", "no such alarm");
    }
    size_t n = g_web_hooks->get_alarms(s_list, ALARM_MAX);
    for (size_t i = 0; i < n; i++) {
        if (s_list[i].id == id) {
            s_list[i] = s_list[n - 1];
            return save_list(req, n - 1, NULL, NULL);
        }
    }
    return web_send_error(req, "404 Not Found", "no such alarm");
}

/* ===== 설정·상태·시간·Wi-Fi ===== */

static esp_err_t settings_get(httpd_req_t *req)
{
    app_settings_t s = app_settings_get();
    web_settings_to_json(&s, s_out, sizeof(s_out));
    return web_send_json(req, "200 OK", s_out);
}

static esp_err_t settings_put(httpd_req_t *req)
{
    char err[96];
    app_settings_t base = app_settings_get(), s;
    if (web_read_body(req, s_json, sizeof(s_json)) != ESP_OK) {
        return ESP_FAIL;
    }
    if (!web_settings_from_json(s_json, &base, &s, err, sizeof(err))) {
        return web_send_error(req, "400 Bad Request", err);
    }
    if (app_settings_set(&s) != ESP_OK) {
        return web_send_error(req, "500 Internal Server Error", "save failed");
    }
    web_settings_to_json(&s, s_out, sizeof(s_out));
    return web_send_json(req, "200 OK", s_out);
}

static esp_err_t status_get(httpd_req_t *req)
{
    cJSON *o = cJSON_CreateObject();
    int64_t local = 0;
    bool known = timekeeper_now_local(&local);
    cJSON_AddBoolToObject(o, "time_known", known);
    cJSON_AddNumberToObject(o, "now_local", (double)local);
    net_status_t ns = net_status();
    cJSON_AddNumberToObject(o, "last_sync_utc", (double)ns.last_sync_utc);
    cJSON_AddStringToObject(o, "wifi_mode", ns.mode == NET_MODE_STA ? "sta" : ns.mode == NET_MODE_AP ? "ap" : "ap+sta");
    cJSON_AddBoolToObject(o, "wifi_connected", ns.sta_connected);
    cJSON_AddNumberToObject(o, "wifi_rssi", ns.rssi);
    cJSON_AddStringToObject(o, "ip", ns.ip);
    uint64_t total = 0, free_bytes = 0;
    bool sd = sdcard_space(&total, &free_bytes);
    cJSON_AddBoolToObject(o, "sd_ok", sd);
    cJSON_AddNumberToObject(o, "sd_total", (double)total);
    cJSON_AddNumberToObject(o, "sd_free", (double)free_bytes);
    cJSON_AddStringToObject(o, "fw_version", esp_app_get_description()->version);
    cJSON_AddBoolToObject(o, "session_active", g_web_hooks->session_active());
    bool ok = cJSON_PrintPreallocated(o, s_out, sizeof(s_out), false);
    cJSON_Delete(o);
    return ok ? web_send_json(req, "200 OK", s_out) : web_send_error(req, "500 Internal Server Error", "status");
}

static esp_err_t time_post(httpd_req_t *req)
{
    char err[96];
    int64_t utc = 0;
    if (web_read_body(req, s_json, sizeof(s_json)) != ESP_OK) {
        return ESP_FAIL;
    }
    if (!web_time_from_json(s_json, &utc, err, sizeof(err))) {
        return web_send_error(req, "400 Bad Request", err);
    }
    esp_err_t e = timekeeper_set_utc(utc);
    if (e == ESP_ERR_INVALID_ARG) {
        return web_send_error(req, "400 Bad Request", "time out of range");
    }
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t wifi_post(httpd_req_t *req)
{
    char err[96], ssid[33], pass[65];
    if (web_read_body(req, s_json, sizeof(s_json)) != ESP_OK) {
        return ESP_FAIL;
    }
    if (!web_wifi_from_json(s_json, ssid, pass, err, sizeof(err))) {
        return web_send_error(req, "400 Bad Request", err);
    }
    web_send_json(req, "202 Accepted", "{\"ok\":true}");   /* 응답을 먼저 보내고 다시 연결 */
    net_set_wifi(ssid, pass);
    memset(pass, 0, sizeof(pass));
    return ESP_OK;
}

/* ===== 정적 파일(LittleFS의 web/) ===== */

static const char *content_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (ext == NULL) {
        return "application/octet-stream";
    }
    if (strcmp(ext, ".html") == 0) return "text/html; charset=utf-8";
    if (strcmp(ext, ".js") == 0) return "text/javascript; charset=utf-8";
    if (strcmp(ext, ".css") == 0) return "text/css; charset=utf-8";
    if (strcmp(ext, ".svg") == 0) return "image/svg+xml";
    if (strcmp(ext, ".webmanifest") == 0) return "application/manifest+json";
    return "application/octet-stream";
}

static esp_err_t static_get(httpd_req_t *req)
{
    char path[96];
    const char *uri = req->uri;
    size_t ulen = strcspn(uri, "?");

    if (ulen == 1 && uri[0] == '/') {
        snprintf(path, sizeof(path), WWW_ROOT "/index.html");
    } else if (ulen < 40 && strstr(uri, "..") == NULL) {
        snprintf(path, sizeof(path), WWW_ROOT "%.*s", (int)ulen, uri);
    } else {
        path[0] = '\0';
    }

    struct stat st;
    if (path[0] == '\0' || stat(path, &st) != 0) {
        if (s_captive) {
            /* 설정 모드: 아이폰의 /hotspot-detect.html 등 모르는 경로는 설정 페이지로 */
            httpd_resp_set_status(req, "302 Found");
            httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
            return httpd_resp_send(req, NULL, 0);
        }
        return web_send_error(req, "404 Not Found", "not found");
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return web_send_error(req, "500 Internal Server Error", "open failed");
    }
    httpd_resp_set_type(req, content_type(path));
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    static char chunk[2048];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (httpd_resp_send_chunk(req, chunk, (ssize_t)n) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }
    fclose(f);
    return httpd_resp_send_chunk(req, NULL, 0);
}

void web_server_set_captive(bool on)
{
    s_captive = on;
}

esp_err_t web_server_start(const web_hooks_t *hooks)
{
    g_web_hooks = hooks;

    esp_vfs_littlefs_conf_t fs = {
        .base_path = WWW_ROOT,
        .partition_label = WWW_LABEL,
        .format_if_mount_failed = false,
        .dont_mount = false,
    };
    if (esp_vfs_littlefs_register(&fs) != ESP_OK) {
        ESP_LOGW(TAG, "web files not found (flash the storage image): API still works");
    }

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.max_uri_handlers = 24;
    cfg.stack_size = 8192;
    cfg.lru_purge_enable = true;
    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &cfg), TAG, "httpd");

    const httpd_uri_t routes[] = {
        { .uri = "/api/alarms", .method = HTTP_GET, .handler = alarms_get },
        { .uri = "/api/alarms", .method = HTTP_POST, .handler = alarms_post },
        { .uri = "/api/alarms/*", .method = HTTP_PUT, .handler = alarm_put },
        { .uri = "/api/alarms/*", .method = HTTP_DELETE, .handler = alarm_delete },
        { .uri = "/api/settings", .method = HTTP_GET, .handler = settings_get },
        { .uri = "/api/settings", .method = HTTP_PUT, .handler = settings_put },
        { .uri = "/api/status", .method = HTTP_GET, .handler = status_get },
        { .uri = "/api/time", .method = HTTP_POST, .handler = time_post },
        { .uri = "/api/wifi", .method = HTTP_POST, .handler = wifi_post },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(s_server, &routes[i]);
    }
    web_sounds_register(s_server);
    web_ota_register(s_server);

    /* 맨 마지막: 나머지 GET은 정적 파일(설정 모드에서는 캡티브 포털 리다이렉트) */
    const httpd_uri_t files = { .uri = "/*", .method = HTTP_GET, .handler = static_get };
    httpd_register_uri_handler(s_server, &files);
    ESP_LOGI(TAG, "http server started");
    return ESP_OK;
}
