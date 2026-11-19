#ifndef UI_TEXT_H
#define UI_TEXT_H

/*
 * 화면에 쓰는 문자열 만들기(순수 C, PC 테스트).
 * 모든 함수는 out[n-1]까지만 쓰고 항상 NUL로 끝낸다.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 로컬 epoch 초 → "7:30"(12시간제, 앞자리 0 없음) + 오후 여부. 00:05는 "12:05" AM */
void ui_fmt_clock(int64_t local_s, char *out, size_t n, bool *pm);
/* "MON, NOV 23" (영문 대문자, 일은 앞자리 0 없음) */
void ui_fmt_date(int64_t local_s, char *out, size_t n);
/* 남은 초 → "4:59". 3600 → "60:00", 0 이하 → "0:00" */
void ui_fmt_countdown(int64_t seconds, char *out, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* UI_TEXT_H */
