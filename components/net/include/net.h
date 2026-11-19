#ifndef NET_H
#define NET_H

/*
 * 네트워크.
 *   순수 부분: Wi-Fi 모드 결정(net_policy.c), 캡티브 포털 DNS 응답(dns_reply.c), 부팅 버튼 판정
 *   ESP 부분:  Wi-Fi, SNTP, mDNS(net_esp.c), DNS 서버(captive_esp.c)
 *
 * 외부 인터넷으로는 SNTP만 나간다. 웹 설정은 같은 Wi-Fi 안에서만(설계 5.5).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== Wi-Fi 모드 결정 ===== */

typedef enum { NET_MODE_STA = 0, NET_MODE_APSTA, NET_MODE_AP } net_mode_t;
typedef enum { NET_WHY_NORMAL = 0, NET_WHY_NO_CREDS, NET_WHY_STA_FAILED, NET_WHY_BUTTON } net_why_t;

#define NET_STA_FAIL_TO_AP_S 300   /* 설계 5.5: 5분 이상 계속 실패하면 설정 모드 */

typedef struct {
    bool      has_creds;
    bool      setup_button;     /* 버튼을 누른 채 부팅 → 재부팅 전까지 AP 유지 */
    bool      connected;
    bool      ap_until_connected;   /* 설정 모드에서 Wi-Fi를 저장함: 연결될 때까지 AP 유지(휴대폰 연결이 끊기지 않게) */
    int64_t   down_since;       /* 마지막으로 끊긴(또는 시작한) 시각 */
    net_why_t why;
} net_policy_t;

void       net_policy_init(net_policy_t *p, bool has_creds, bool setup_button, int64_t now_s);
void       net_policy_on_connected(net_policy_t *p, int64_t now_s);
void       net_policy_on_disconnected(net_policy_t *p, int64_t now_s);
void       net_policy_on_creds_saved(net_policy_t *p, int64_t now_s);
net_mode_t net_policy_mode(net_policy_t *p, int64_t now_s);   /* 매초 부른다 */
net_why_t  net_policy_why(const net_policy_t *p);

/* ===== 캡티브 포털 DNS ===== */

/* DNS 질의 패킷(UDP 본문)에 대해 A 질의를 ip로 답하는 응답을 만든다.
 * 질의가 1개가 아니거나, 응답 패킷(QR=1)이거나, 길이가 맞지 않으면 0(응답하지 않음) */
size_t dns_build_reply(const uint8_t *q, size_t qlen, const uint8_t ip[4], uint8_t *out, size_t out_n);

/* ===== 부팅 때 버튼 ===== */

typedef enum { BOOT_HOLD_NONE = 0, BOOT_HOLD_SETUP, BOOT_HOLD_FACTORY_RESET } boot_hold_t;
#define BOOT_HOLD_FACTORY_MS 10000   /* 설계 5.5: 10초 누른 채 부팅 → 공장 초기화 */

boot_hold_t boot_hold_classify(bool pressed_at_boot, uint32_t held_ms);

#ifdef ESP_PLATFORM
#include "esp_err.h"

typedef struct {
    net_mode_t mode;
    net_why_t  why;
    bool       sta_connected;
    int8_t     rssi;
    int64_t    last_sync_utc;   /* SNTP 마지막 성공, 없으면 0 */
    char       ip[16];
    char       ap_ssid[33];
    char       ap_pass[17];
} net_status_t;

/* NVS의 Wi-Fi 정보로 시작하고 매초 정책을 반영한다. SNTP·mDNS도 시작 */
esp_err_t    net_start(bool setup_button, const char *hostname);
/* ssid 1~32, pass 0 또는 8~63바이트 → NVS 저장 → 다시 연결 */
esp_err_t    net_set_wifi(const char *ssid, const char *pass);
esp_err_t    net_set_hostname(const char *hostname);
net_status_t net_status(void);
esp_err_t    net_forget_wifi(void);   /* 공장 초기화용 */
#endif

#ifdef __cplusplus
}
#endif

#endif /* NET_H */
