#include "net.h"

#include <string.h>

void net_policy_init(net_policy_t *p, bool has_creds, bool setup_button, int64_t now_s)
{
    memset(p, 0, sizeof(*p));
    p->has_creds = has_creds;
    p->setup_button = setup_button;
    p->down_since = now_s;   /* 한 번도 연결된 적이 없으면 부팅부터 센다 */
    p->why = NET_WHY_NORMAL;
}

void net_policy_on_connected(net_policy_t *p, int64_t now_s)
{
    (void)now_s;
    p->connected = true;
    p->ap_until_connected = false;
}

void net_policy_on_disconnected(net_policy_t *p, int64_t now_s)
{
    if (p->connected) {
        p->down_since = now_s;
    }
    p->connected = false;
}

void net_policy_on_creds_saved(net_policy_t *p, int64_t now_s)
{
    if (net_policy_mode(p, now_s) != NET_MODE_STA) {
        p->ap_until_connected = true;   /* AP로 접속한 휴대폰에서 저장했다: 연결될 때까지 AP 유지 */
    }
    p->has_creds = true;
    p->connected = false;
    p->down_since = now_s;   /* 새 정보로 다시 5분을 준다 */
}

net_mode_t net_policy_mode(net_policy_t *p, int64_t now_s)
{
    if (p->setup_button) {
        p->why = NET_WHY_BUTTON;
        return p->has_creds ? NET_MODE_APSTA : NET_MODE_AP;
    }
    if (!p->has_creds) {
        p->why = NET_WHY_NO_CREDS;
        return NET_MODE_AP;
    }
    if (p->ap_until_connected) {
        p->why = NET_WHY_NO_CREDS;
        return NET_MODE_APSTA;
    }
    if (!p->connected && now_s - p->down_since >= NET_STA_FAIL_TO_AP_S) {
        p->why = NET_WHY_STA_FAILED;
        return NET_MODE_APSTA;   /* AP를 띄우되 집 Wi-Fi 재시도는 계속 */
    }
    p->why = NET_WHY_NORMAL;
    return NET_MODE_STA;
}

net_why_t net_policy_why(const net_policy_t *p)
{
    return p->why;
}

boot_hold_t boot_hold_classify(bool pressed_at_boot, uint32_t held_ms)
{
    if (!pressed_at_boot) {
        return BOOT_HOLD_NONE;
    }
    return held_ms >= BOOT_HOLD_FACTORY_MS ? BOOT_HOLD_FACTORY_RESET : BOOT_HOLD_SETUP;
}
