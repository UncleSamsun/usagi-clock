#ifndef BOARD_PINS_H
#define BOARD_PINS_H

/*
 * 핀 배정: WeMos LOLIN S3 V1.0.0 (계획 2 "핀 배정" 표).
 * 이 헤더는 main만 include한다. 컴포넌트는 핀 번호를 시작 함수의 인자로 받는다.
 *
 * 쓰지 않는 GPIO: 0·3·45·46(스트래핑), 19·20(USB 콘솔·JTAG), 35~37(옥탈 PSRAM),
 *                 38·48(RGB LED, 실물 확인 전), 43·44(UART0)
 *
 * [실물 확인] 부품 도착 전에 정한 값이다. 실크 인쇄와 배선으로 확인하고 다르면 여기와 계획 2 문서를 함께 고친다.
 */

/* LCD: SPI2(FSPI)의 IOMUX 전용 핀 → 40MHz를 GPIO 매트릭스 없이 낸다 */
#define PIN_LCD_SCLK 12
#define PIN_LCD_MOSI 11
#define PIN_LCD_MISO 13
#define PIN_LCD_CS   10
#define PIN_LCD_DC   9
#define PIN_LCD_RST  14
#define PIN_LCD_BL   21   /* 백라이트 PWM(LEDC) */

/* SD카드: SPI3. LCD와 버스를 나눠 화면 갱신과 파일 읽기가 서로 막지 않게 한다 */
#define PIN_SD_SCK  7
#define PIN_SD_MOSI 6
#define PIN_SD_MISO 5
#define PIN_SD_CS   4

/* RTC DS3231: I2C0, 100kHz. 모듈 VCC는 반드시 3V3 (풀업이 VCC에 걸려 있음) */
#define PIN_I2C_SDA 8
#define PIN_I2C_SCL 18

/* 앰프 MAX98357A: I2S0 */
#define PIN_I2S_BCLK 15
#define PIN_I2S_WS   16
#define PIN_I2S_DOUT 17

/* 스누즈 바: 내부 풀업, 누르면 LOW */
#define PIN_BUTTON 47

/* 예약: 볼륨 다이얼(ADC1_CH0). 지금은 쓰지 않는다 */
#define PIN_RESERVED_DIAL 1

#endif /* BOARD_PINS_H */
