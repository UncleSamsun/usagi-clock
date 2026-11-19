#include "net.h"

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mdns.h"
#include "nvs.h"
#include "timekeeper.h"

static const char *TAG = "net";

#define NVS_NS          "usagi"
#define NVS_KEY_WIFI    "wifi"
#define NVS_NS_FACTORY  "factory"
#define NVS_KEY_AP_PASS "ap_pass"
#define AP_CHANNEL      6
#define AP_MAX_CONN     2
#define STA_RETRY_S     10

typedef struct { char ssid[33]; char pass[65]; } wifi_creds_t;

void captive_dns_start(void);
void captive_dns_stop(void);

static SemaphoreHandle_t s_lock;
static net_policy_t s_policy;
static net_status_t s_status;
static wifi_creds_t s_creds;
static bool s_has_creds;
static net_mode_t s_applied = (net_mode_t)-1;
static int64_t s_next_retry;
static char s_hostname[33];
static esp_netif_t *s_sta, *s_ap;

static int64_t now_s(void)
{
    return esp_timer_get_time() / 1000000;   /* 단조 증가 시계(SNTP로 시간이 바뀌어도 영향 없음) */
}

/* ===== NVS ===== */

static bool load_creds(wifi_creds_t *c)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }
    size_t len = sizeof(*c);
    esp_err_t err = nvs_get_blob(h, NVS_KEY_WIFI, c, &len);
    nvs_close(h);
    return err == ESP_OK && len == sizeof(*c) && c->ssid[0] != '\0';
}

static esp_err_t save_creds(const wifi_creds_t *c)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "nvs");
    esp_err_t err = nvs_set_blob(h, NVS_KEY_WIFI, c, sizeof(*c));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

/* AP 비밀번호: 처음 부팅 때 만들어 "factory"에 둔다. 공장 초기화에도 지우지 않는다(바닥 스티커와 맞게) */
static void load_or_make_ap_pass(char out[17])
{
    nvs_handle_t h;
    size_t len = 17;
    if (nvs_open(NVS_NS_FACTORY, NVS_READWRITE, &h) != ESP_OK) {
        strcpy(out, "usagi-setup");
        return;
    }
    if (nvs_get_str(h, NVS_KEY_AP_PASS, out, &len) != ESP_OK || strlen(out) < 8) {
        static const char chars[] = "abcdefghijkmnpqrstuvwxyz23456789";   /* 헷갈리는 l, o, 0, 1 제외 */
        for (int i = 0; i < 10; i++) {
            out[i] = chars[esp_random() % (sizeof(chars) - 1)];
        }
        out[10] = '\0';
        nvs_set_str(h, NVS_KEY_AP_PASS, out);
        nvs_commit(h);
    }
    nvs_close(h);
}

/* ===== SNTP: 시간 반영은 timekeeper가 범위 검사 후 한다 ===== */

/* ESP-IDF의 약한 심볼을 대신한다. 이상한 시각(2000년 등)이 와도 시스템 시간을 바꾸지 않는다 */
void sntp_sync_time(struct timeval *tv)
{
    if (timekeeper_set_utc((int64_t)tv->tv_sec) == ESP_OK || timekeeper_utc_plausible((int64_t)tv->tv_sec)) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_status.last_sync_utc = (int64_t)tv->tv_sec;
        xSemaphoreGive(s_lock);
        ESP_LOGI(TAG, "sntp: synced");
    } else {
        ESP_LOGW(TAG, "sntp: ignored implausible time %lld", (long long)tv->tv_sec);
    }
    sntp_set_sync_status(SNTP_SYNC_STATUS_COMPLETED);
}

static void sntp_start(void)
{
    /* 서버 수는 CONFIG_LWIP_SNTP_MAX_SERVERS(기본 1)까지. pool.ntp.org가 여러 서버로 나눠 준다 */
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    cfg.start = true;   /* 동기화 주기는 CONFIG_LWIP_SNTP_UPDATE_DELAY(기본 1시간) */
    esp_netif_sntp_init(&cfg);
}

/* ===== mDNS ===== */

static void mdns_start(const char *hostname)
{
    if (mdns_init() != ESP_OK) {
        ESP_LOGW(TAG, "mdns init failed");
        return;
    }
    mdns_hostname_set(hostname);
    mdns_instance_name_set("Usagi Clock");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mdns: http://%s.local", hostname);
}

esp_err_t net_set_hostname(const char *hostname)
{
    strncpy(s_hostname, hostname, sizeof(s_hostname) - 1);
    return mdns_hostname_set(hostname);
}

/* ===== Wi-Fi ===== */

static void apply_sta_config(void)
{
    wifi_config_t sta = {0};
    strncpy((char *)sta.sta.ssid, s_creds.ssid, sizeof(sta.sta.ssid));
    strncpy((char *)sta.sta.password, s_creds.pass, sizeof(sta.sta.password));
    sta.sta.threshold.authmode = s_creds.pass[0] != '\0' ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    esp_wifi_set_config(WIFI_IF_STA, &sta);
}

static void apply_ap_config(void)
{
    wifi_config_t ap = {0};
    strncpy((char *)ap.ap.ssid, s_status.ap_ssid, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = (uint8_t)strlen(s_status.ap_ssid);
    strncpy((char *)ap.ap.password, s_status.ap_pass, sizeof(ap.ap.password));
    ap.ap.channel = AP_CHANNEL;
    ap.ap.max_connection = AP_MAX_CONN;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    esp_wifi_set_config(WIFI_IF_AP, &ap);
}

static void apply_mode(net_mode_t mode)
{
    if (mode == s_applied) {
        return;
    }
    wifi_mode_t m = mode == NET_MODE_STA ? WIFI_MODE_STA : mode == NET_MODE_AP ? WIFI_MODE_AP : WIFI_MODE_APSTA;
    ESP_LOGI(TAG, "mode -> %s", mode == NET_MODE_STA ? "STA" : mode == NET_MODE_AP ? "AP" : "AP+STA");
    esp_wifi_set_mode(m);
    if (mode != NET_MODE_STA) {
        apply_ap_config();
        captive_dns_start();
    } else {
        captive_dns_stop();
    }
    if (mode != NET_MODE_AP && s_has_creds) {
        apply_sta_config();
        esp_wifi_connect();
    }
    s_applied = mode;
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        net_policy_on_disconnected(&s_policy, now_s());
        s_status.sta_connected = false;
        s_status.ip[0] = '\0';
        s_next_retry = now_s() + STA_RETRY_S;
        xSemaphoreGive(s_lock);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ev = data;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        net_policy_on_connected(&s_policy, now_s());
        s_status.sta_connected = true;
        snprintf(s_status.ip, sizeof(s_status.ip), IPSTR, IP2STR(&ev->ip_info.ip));
        xSemaphoreGive(s_lock);
        ESP_LOGI(TAG, "got ip %s", s_status.ip);
    }
}

/* 매초: 정책대로 모드를 바꾸고, 끊겼으면 STA_RETRY_S마다 다시 연결 */
static void net_task(void *arg)
{
    (void)arg;
    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        net_mode_t mode = net_policy_mode(&s_policy, now_s());
        s_status.mode = mode;
        s_status.why = net_policy_why(&s_policy);
        bool retry = mode != NET_MODE_AP && s_has_creds && !s_status.sta_connected && now_s() >= s_next_retry;
        if (retry) {
            s_next_retry = now_s() + STA_RETRY_S;
        }
        xSemaphoreGive(s_lock);

        apply_mode(mode);
        if (retry) {
            esp_wifi_connect();
        }
        if (s_status.sta_connected) {
            wifi_ap_record_t ap;
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                s_status.rssi = ap.rssi;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

esp_err_t net_start(bool setup_button, const char *hostname)
{
    s_lock = xSemaphoreCreateMutex();
    strncpy(s_hostname, hostname, sizeof(s_hostname) - 1);

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif");
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    s_sta = esp_netif_create_default_wifi_sta();
    s_ap = esp_netif_create_default_wifi_ap();
    esp_netif_set_hostname(s_sta, hostname);

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init), TAG, "wifi init");
    /* Wi-Fi 정보는 우리 NVS("usagi")에만 둔다: 공장 초기화를 한 곳에서 하기 위해 */
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(s_status.ap_ssid, sizeof(s_status.ap_ssid), "Usagi-Setup-%02X%02X", mac[4], mac[5]);
    load_or_make_ap_pass(s_status.ap_pass);

    s_has_creds = load_creds(&s_creds);
    net_policy_init(&s_policy, s_has_creds, setup_button, now_s());

    esp_wifi_set_mode(WIFI_MODE_STA);
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");

    sntp_start();
    mdns_start(hostname);

    if (xTaskCreate(net_task, "net", 4096, NULL, 5, NULL) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t net_set_wifi(const char *ssid, const char *pass)
{
    size_t sl = strlen(ssid), pl = strlen(pass);
    if (sl == 0 || sl > 32 || (pl != 0 && (pl < 8 || pl > 63))) {
        return ESP_ERR_INVALID_ARG;
    }
    wifi_creds_t c = {0};
    memcpy(c.ssid, ssid, sl);
    memcpy(c.pass, pass, pl);
    ESP_RETURN_ON_ERROR(save_creds(&c), TAG, "save wifi");

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_creds = c;
    s_has_creds = true;
    net_policy_on_creds_saved(&s_policy, now_s());
    s_next_retry = 0;
    s_applied = (net_mode_t)-1;   /* 다음 틱에 설정을 다시 적용 */
    xSemaphoreGive(s_lock);

    esp_wifi_disconnect();
    ESP_LOGI(TAG, "wifi saved: %s", ssid);   /* 비밀번호는 로그에 남기지 않는다 */
    return ESP_OK;
}

esp_err_t net_forget_wifi(void)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NVS_NS, NVS_READWRITE, &h), TAG, "nvs");
    nvs_erase_key(h, NVS_KEY_WIFI);
    esp_err_t err = nvs_commit(h);
    nvs_close(h);
    return err;
}

net_status_t net_status(void)
{
    net_status_t st;
    if (s_lock == NULL) {   /* net_start 전이거나 실패: STA·미연결로 본다 */
        memset(&st, 0, sizeof(st));
        return st;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    st = s_status;
    xSemaphoreGive(s_lock);
    return st;
}
