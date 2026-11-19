#include <string.h>

#include "unity.h"

#include "net.h"

static const uint8_t IP[4] = { 192, 168, 4, 1 };
static uint8_t out[512];

/* "captive.apple.com" A IN 질의, ID 0x1234, RD=1 */
static const uint8_t QUERY[] = {
    0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    7, 'c', 'a', 'p', 't', 'i', 'v', 'e', 5, 'a', 'p', 'p', 'l', 'e', 3, 'c', 'o', 'm', 0,
    0x00, 0x01, 0x00, 0x01,
};

void setUp(void) {}
void tearDown(void) {}

void test_reply_to_a_query(void)
{
    size_t n = dns_build_reply(QUERY, sizeof(QUERY), IP, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(sizeof(QUERY) + 16, n);
    TEST_ASSERT_EQUAL_HEX8(0x12, out[0]);                 /* ID 그대로 */
    TEST_ASSERT_EQUAL_HEX8(0x34, out[1]);
    TEST_ASSERT_TRUE(out[2] & 0x80);                      /* QR=1 */
    TEST_ASSERT_EQUAL_HEX8(0x01, out[7]);                 /* ANCOUNT 1 */
    TEST_ASSERT_EQUAL_MEMORY(QUERY + 12, out + 12, sizeof(QUERY) - 12);   /* 질문 그대로 */
    TEST_ASSERT_EQUAL_MEMORY(IP, out + n - 4, 4);         /* 마지막 4바이트가 주소 */
}

void test_aaaa_query_gets_no_answer(void)
{
    uint8_t q[sizeof(QUERY)];
    memcpy(q, QUERY, sizeof(q));
    q[sizeof(q) - 3] = 28;   /* QTYPE AAAA */
    size_t n = dns_build_reply(q, sizeof(q), IP, out, sizeof(out));
    TEST_ASSERT_EQUAL_size_t(sizeof(q), n);
    TEST_ASSERT_EQUAL_HEX8(0x00, out[7]);    /* 답 없음(IPv6 주소는 주지 않는다) */
}

void test_ignore_response_packet(void)
{
    uint8_t q[sizeof(QUERY)];
    memcpy(q, QUERY, sizeof(q));
    q[2] |= 0x80;
    TEST_ASSERT_EQUAL_size_t(0, dns_build_reply(q, sizeof(q), IP, out, sizeof(out)));
}

void test_ignore_malformed(void)
{
    TEST_ASSERT_EQUAL_size_t(0, dns_build_reply(QUERY, 5, IP, out, sizeof(out)));

    uint8_t q[sizeof(QUERY)];
    memcpy(q, QUERY, sizeof(q));
    q[12] = 60;   /* 레이블 길이가 패킷을 넘음 */
    TEST_ASSERT_EQUAL_size_t(0, dns_build_reply(q, sizeof(q), IP, out, sizeof(out)));

    memcpy(q, QUERY, sizeof(q));
    q[5] = 2;     /* QDCOUNT 2 */
    TEST_ASSERT_EQUAL_size_t(0, dns_build_reply(q, sizeof(q), IP, out, sizeof(out)));

    memcpy(q, QUERY, sizeof(q));
    q[12] = 0xC0; /* 질의 안의 압축 포인터 */
    TEST_ASSERT_EQUAL_size_t(0, dns_build_reply(q, sizeof(q), IP, out, sizeof(out)));

    /* QTYPE·QCLASS가 잘림 */
    TEST_ASSERT_EQUAL_size_t(0, dns_build_reply(QUERY, sizeof(QUERY) - 2, IP, out, sizeof(out)));
}

void test_out_buffer_too_small(void)
{
    TEST_ASSERT_EQUAL_size_t(0, dns_build_reply(QUERY, sizeof(QUERY), IP, out, sizeof(QUERY)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_reply_to_a_query);
    RUN_TEST(test_aaaa_query_gets_no_answer);
    RUN_TEST(test_ignore_response_packet);
    RUN_TEST(test_ignore_malformed);
    RUN_TEST(test_out_buffer_too_small);
    return UNITY_END();
}
