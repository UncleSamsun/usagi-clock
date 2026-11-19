#include "net.h"

#include <string.h>

/*
 * 캡티브 포털용 DNS 응답.
 * 설정 모드에서 휴대폰이 어떤 이름을 물어도 시계(192.168.4.1)를 가리키게 해서,
 * 아이폰이 접속하자마자 설정 페이지를 띄우게 한다.
 *
 * DNS 메시지: 헤더 12바이트(ID, 플래그, QDCOUNT, ANCOUNT, NSCOUNT, ARCOUNT) + 질문 + 답
 * 질문: 이름(길이+글자 레이블들, 0으로 끝) + QTYPE 2바이트 + QCLASS 2바이트
 * 답:   이름 포인터(0xC00C) + TYPE A + CLASS IN + TTL + RDLENGTH 4 + IPv4
 */

#define DNS_HEADER 12
#define DNS_ANSWER 16   /* 포인터 2 + TYPE 2 + CLASS 2 + TTL 4 + RDLEN 2 + IP 4 */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

size_t dns_build_reply(const uint8_t *q, size_t qlen, const uint8_t ip[4], uint8_t *out, size_t out_n)
{
    if (qlen < DNS_HEADER + 5) {
        return 0;
    }
    if (q[2] & 0x80) {                 /* QR=1: 응답 패킷에는 답하지 않는다 */
        return 0;
    }
    if (rd16(q + 4) != 1) {            /* 질문은 정확히 1개 */
        return 0;
    }

    /* 질문 이름 끝 찾기(압축 포인터는 질의에 오지 않는다고 보고 거절) */
    size_t pos = DNS_HEADER;
    for (;;) {
        if (pos >= qlen) {
            return 0;
        }
        uint8_t l = q[pos];
        if (l == 0) {
            pos++;
            break;
        }
        if (l & 0xC0) {
            return 0;
        }
        pos += 1 + l;
    }
    if (pos + 4 > qlen) {
        return 0;
    }
    uint16_t qtype = rd16(q + pos);
    size_t qend = pos + 4;             /* QTYPE, QCLASS 포함 */
    bool answer = qtype == 1 || qtype == 255;   /* A 또는 ANY에만 주소를 준다 */

    size_t need = qend + (answer ? DNS_ANSWER : 0);
    if (out_n < need) {
        return 0;
    }

    memcpy(out, q, qend);              /* 헤더 + 질문 그대로 */
    out[2] = 0x84 | (q[2] & 0x01);     /* QR=1, AA=1, RD는 질의 그대로 */
    out[3] = 0x00;                     /* RA=0, RCODE=0 */
    out[6] = 0;
    out[7] = answer ? 1 : 0;           /* ANCOUNT */
    memset(out + 8, 0, 4);             /* NSCOUNT, ARCOUNT */

    if (answer) {
        uint8_t *a = out + qend;
        a[0] = 0xC0; a[1] = 0x0C;      /* 이름 = 오프셋 12의 질문 이름 */
        a[2] = 0x00; a[3] = 0x01;      /* TYPE A */
        a[4] = 0x00; a[5] = 0x01;      /* CLASS IN */
        a[6] = 0; a[7] = 0; a[8] = 0; a[9] = 60;   /* TTL 60초 */
        a[10] = 0x00; a[11] = 0x04;    /* RDLENGTH */
        memcpy(a + 12, ip, 4);
    }
    return need;
}
