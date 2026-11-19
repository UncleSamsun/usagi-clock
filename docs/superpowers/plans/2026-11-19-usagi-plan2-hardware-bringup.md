# 우사기 알람시계 — 계획 2: 하드웨어 기동 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **이 계획의 실행 방식 [사용자 결정, 2026-11-19]:** 코드와 테스트는 Claude가 작성한다. 사용자는 납땜·배선, 플래시와 실기 확인, Task 3(JTAG 디버깅 실습)을 직접 한다. 실기 확인이 필요한 Step은 사용자 PC와 보드에서만 끝낼 수 있으므로 서브에이전트에게 맡기지 않는다.
>
> **표기:** **[실물 확인]** 부품이 도착하기 전에 데이터시트와 판매 페이지 기준으로 정한 값이다. 실물(실크 인쇄, 측정, 첫 동작)로 확인하고 다르면 이 문서와 `main/board_pins.h`를 함께 고친다. **[제안]** Claude가 정한 것으로 바꿀 수 있다.

**Goal:** 브레드보드에 조립한 시계 한 대가 DS3231 시간으로 부팅해서 12시간제 시계 화면을 보여주고, 개발용 콘솔로 넣은 알람이 정해진 시각에 비프음으로 울리며, 스누즈 바 버튼으로 스누즈·끄기가 된다.

**Architecture:** 계획 1의 순수 C 로직(`alarm_core`, `button_fsm`)은 그대로 두고, 그 둘레에 하드웨어 컴포넌트를 붙인다. 하드웨어마다 "순수 C 부분(PC 테스트)"과 "ESP-IDF 부분(`*_esp.c`, 실기 확인)"으로 파일을 나눈다. 알람 엔진은 `alarm_service` 하나만 소유하고, 버튼·오디오·화면과는 FreeRTOS 큐로만 주고받는다.

**Tech Stack:** ESP-IDF v6.1, C11, FreeRTOS, `esp_driver_i2c`(i2c_master), `esp_lcd` + `espressif/esp_lcd_ili9341`, `espressif/esp_lvgl_port` + LVGL 9, `esp_driver_i2s`(std 모드), LEDC, `esp_console`(USB Serial/JTAG), OpenOCD + GDB, PulseView(로직 분석기), Unity·gcovr·cppcheck(PC 테스트)

**Spec:** `docs/specs/2026-11-16-usagi-alarm-clock-design.md` (4장 하드웨어, 5.1 시간, 5.3 상태 머신, 5.4 화면, 6장 펌웨어 구조, 9.1 통신 기초 보강)
**이전 계획:** `docs/superpowers/plans/2026-11-16-usagi-plan1-core-logic.md` (완료)

> **실행 메모 (2026-11-19, 부품 도착 전 코드 선작성) [사용자 결정: "소프트웨어는 미리 다 짜두자"]**
> - 코드·PC 테스트·펌웨어 빌드까지 끝났다. 남은 것은 **[실기]** Step(배선, 플래시, 소리·화면 확인, 로직 분석기, 밤샘 시험)과 Task 3(JTAG 실습)이다. 실기 Step은 체크하지 않은 채로 둔다.
> - 계획과 달라진 점
>   - 컴포넌트 `components/button` → **`components/snooze_button`**. 이름이 `button`이면 Espressif의 `button`(iot_button)과 겹쳐 `esp_lvgl_port`가 잘못 빌드된다. 헤더 이름(`button_fsm.h`)은 그대로다.
>   - 비프 무늬를 순수 C `audio_beep.c`로 따로 빼고 PC 테스트(`test_audio_beep.c`)를 더했다.
>   - `audio_op_t`·`audio_cmd_t`는 처음부터 `audio.h`의 순수 부분에 두고 `name` 필드와 `AUDIO_OP_PREVIEW`(계획 4)를 미리 넣었다.
>   - `alarm_service`에 계획 4용 연결점(설정 변경, 저장 콜백, 밝기 함수, 화면 장식, 기본 볼륨)을 미리 두었다. 화면 상태(`ui_state_t`)에도 Wi-Fi 표시와 설정 모드 안내 필드가 있다.
>   - 화면의 다음 알람 계산용으로 `alarm_next_any()`를 `alarm_core`에 더했다(PC 테스트 포함).
>   - 펌웨어 파일 이름은 `build/usagi_clock.bin`이다.

## 범위

| 들어감 | 빠짐(다음 계획) |
|---|---|
| 핀 배정 확정, 보드 기동, USB 콘솔 | SD카드, MP3, "우! 나!" 클립 → 계획 3 |
| DS3231 드라이버(직접 작성), 시간 유지 | SNTP, Wi-Fi, 웹 설정, NVS 저장 → 계획 4 |
| ILI9341 + LVGL 시계 화면, 백라이트 PWM | 야간 밝기 시간대 → 계획 4 |
| 버튼 GPIO, 알람 서비스(FreeRTOS 작업), 비프음 | 캐릭터 이미지 → 계획 3 이후 |
| JTAG 디버깅 실습(사용자) | 두 번째 시계 조립 → 계획 5 |

- 비프음은 계획 3에서 MP3로 바뀐다. 다만 설계 5.3의 "소리 실패 대비"용 비프음으로 그대로 남는다.
- 알람 목록과 설정은 RAM에만 있다. 재부팅하면 사라지고, 계획 4에서 NVS와 웹으로 바뀐다. 그 전까지는 개발용 콘솔 명령으로 넣는다.

## 준비 작업 (코드 아님, 부품 도착 후)

- [ ] 부품 수량·외관 확인(2대분). 디스플레이와 앰프는 반품 불가 상품이므로 받은 날 확인한다.
- [ ] LOLIN S3 핀 헤더, MAX98357A 핀 헤더·스피커 단자 납땜(보드 1대분 먼저)
- [ ] DS3231 모듈(SZH-EK047)의 충전 회로 확인. CR2032 옆에 "201" 저항과 다이오드가 있으면 저항을 떼고 CR2032를 끼운다(parts-list E.4).
- [ ] **[실물 확인]** LOLIN S3 실크 인쇄로 헤더의 GPIO 번호를 확인하고, RGB LED가 GPIO38인지 GPIO48인지 확인한다. 자료마다 다르다([CircuitPython](https://circuitpython.org/board/lolin_s3)은 38, [mischianti](https://mischianti.org/wemos-lolin-s3-esp32-s3-high-resolution-pinout-datasheet-and-specs/)는 48). 이 계획은 두 핀을 모두 쓰지 않는다.
- [ ] **[실물 확인]** 디스플레이(THL-CD11) 뒷면 핀 이름과 순서(LCD 쪽 VCC·GND·CS·RESET·DC·SDI·SCK·LED·SDO, SD 쪽 SD_CS·SD_MOSI·SD_MISO·SD_SCK)를 사진으로 남긴다.
- [ ] PulseView(sigrok) 설치. 로직 분석기(AN-1, fx2lafw 호환)가 잡히는지 확인한다.
- [ ] 멀티미터로 LOLIN S3의 3V3 핀 전압 확인(USB 연결 상태에서 3.2~3.4V)

## Global Constraints

- ESP-IDF v6.1, target `esp32s3`, 보드 WeMos LOLIN S3 V1.0.0(N16R8, 3.3V 옥탈 PSRAM). 계획 1의 `sdkconfig.defaults`와 `partitions.csv`를 유지한다.
- **파일 규칙:** ESP-IDF 헤더(`esp_*`, `freertos/*`, `driver/*`, `lvgl.h`)를 include하는 파일은 이름이 `_esp.c`로 끝난다. 나머지 `components/*/*.c`는 순수 C이고 PC 테스트·커버리지 90%·cppcheck 대상이다.
- **쓰지 않는 GPIO:** 0·3·45·46(스트래핑), 19·20(USB, 콘솔·JTAG), 35~37(옥탈 PSRAM), 38·48(RGB LED, 실물 확인 전까지), 43·44(UART0, 비워 둠)
- 콘솔과 JTAG는 내장 USB Serial/JTAG(GPIO19/20)만 쓴다. 외부 JTAG 어댑터는 쓰지 않는다.
- 시간 기준: RTC와 시스템 시간은 **UTC**로 둔다. 알람과 화면은 `alarm_local_from_utc()`로 바꾼 로컬 epoch 초를 쓴다(KST 고정).
- 시간을 모르면(RTC 무효이고 설정한 적 없음) 화면은 `--:--`와 `SET TIME`을 보여주고 알람 틱을 돌리지 않는다(설계 5.1).
- 유효한 시간 범위: UTC `1767225600`(2026-01-01 00:00) 이상 `4102444800`(2100-01-01 00:00) 미만. DS3231은 2000~2099년만 표현한다.
- DS3231 모듈 VCC는 **3V3**에 연결한다. 모듈의 I2C 풀업이 VCC에 걸려 있어서 5V에 연결하면 ESP32-S3 핀에 5V가 걸린다.
- 알람 경로(`alarm_service`, 버튼, 비프음)는 실행 중 `malloc`을 쓰지 않는다. 큐와 작업은 시작할 때 한 번 만든다.
- 스피커 보호: 볼륨 100일 때 비프음 최대 진폭은 `AUDIO_PEAK_MAX = 16383`(int16 최대의 50%) **[제안, 실물에서 소리 크기를 듣고 조정]**
- 작업 워치독(TWDT)을 켜고 `alarm_service` 작업을 등록한다. 시간 제한 5초.
- 저장소에 캐릭터 이미지·음성 커밋 금지(계획 1과 같음). 화면은 텍스트만 쓴다.
- 커밋 메시지 끝에 `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`

## 핀 배정 [제안, 실물 확인]

| 기능 | 신호 | GPIO | 이유 |
|---|---|---|---|
| LCD (SPI2) | SCLK / MOSI / MISO / CS | 12 / 11 / 13 / 10 | SPI2의 IOMUX 전용 핀이라 40MHz를 GPIO 매트릭스 없이 낸다 |
| LCD | DC / RST / 백라이트(LEDC) | 9 / 14 / 21 | |
| SD (SPI3, 계획 3) | SCK / MOSI / MISO / CS | 7 / 6 / 5 / 4 | 배선만 해 둔다. LCD와 버스를 나눠 화면 갱신과 파일 읽기가 서로 막지 않게 한다(설계 4장) |
| RTC (I2C0, 100kHz) | SDA / SCL | 8 / 18 | |
| 앰프 (I2S0) | BCLK / LRC(WS) / DIN | 15 / 16 / 17 | |
| 스누즈 바 | 버튼(내부 풀업, 누르면 LOW) | 47 | |
| 예약 | 볼륨 다이얼(ADC1_CH0, 설계 1장 비목표) | 1 | 핀만 남긴다 |

- 모든 값은 `main/board_pins.h` 한 곳에만 둔다. 이 헤더는 `main`만 include하고, 컴포넌트는 핀 번호를 시작 함수의 인자로 받는다. 그래야 컴포넌트가 보드에 묶이지 않는다.
- 헤더 위치가 브레드보드 배선에 불편하면 같은 종류의 빈 핀(2, 39~42)으로 바꿔도 된다. 단, LCD SPI 4핀은 IOMUX 핀(10~13)을 유지한다.

## Review Focus

1. 새 DS3231 모듈이거나 배터리가 빠졌다 끼워지면 발진 정지 플래그(OSF)가 켜져 있다. 이때 레지스터 값을 시간으로 믿지 말고 "시간 모름"으로 처리해야 한다. → Task 2 `test_osf_set_means_invalid`
2. RTC가 2000-01-01이나 2100년 이후 같은 값을 주면(전지 교체 직후, 쓰레기 값) 그 시각으로 알람을 돌리지 말고 "시간 모름"으로 처리해야 한다. → Task 4 `test_plausible_rejects_before_2026`, `test_plausible_rejects_2100`
3. 다른 라이브러리로 12시간 모드가 설정된 모듈이어도 시각을 바르게 읽어야 한다(12 AM = 0시, 12 PM = 12시). → Task 2 `test_decode_12h_mode`
4. 자정과 정오 경계에서 화면이 `0:05`나 `12:00 AM` 대신 엉뚱한 값을 보이면 안 된다. 00:05는 `12:05 AM`, 12:00은 `12:00 PM`, 13:07은 `1:07 PM`이다. → Task 5 `test_clock_midnight_and_noon`
5. 볼륨 100으로 계속 울려도 스피커 정격(3W)을 넘는 진폭이 나오면 안 된다. → Task 6 `test_tone_peak_capped_at_volume_100`

---

## 파일 구조

```
usagi-clock/
├── main/
│   ├── CMakeLists.txt               REQUIRES 추가
│   ├── idf_component.yml            esp_lcd_ili9341, esp_lvgl_port, lvgl (Task 5)
│   ├── board_pins.h                 핀 배정 전부 (Task 1)
│   ├── main.c                       부팅 순서와 작업 생성
│   ├── alarm_service_esp.c/.h       알람 엔진 소유, 1초 틱, 입력 큐 (Task 7)
│   └── dev_console_esp.c/.h         settime / time / alarm 명령 (Task 4, 7)
├── components/
│   ├── alarm_core/alarm_time.c      + 날짜↔epoch 변환 (Task 2)
│   ├── button/button_gpio_esp.c     10ms 폴링 작업 (Task 7)
│   ├── ds3231/
│   │   ├── include/ds3231.h
│   │   ├── ds3231_codec.c           BCD 레지스터 ↔ UTC epoch (순수, Task 2)
│   │   └── ds3231_esp.c             I2C 읽기/쓰기 (Task 2)
│   ├── timekeeper/
│   │   ├── include/timekeeper.h
│   │   ├── time_rules.c             유효 범위 판단 (순수, Task 4)
│   │   └── timekeeper_esp.c         RTC → 시스템 시간, 수동 설정 (Task 4)
│   ├── ui_text/
│   │   ├── include/ui_text.h
│   │   └── ui_text.c                12시간제 시각·날짜·남은 시간 문자열 (순수, Task 5)
│   ├── display_ui/
│   │   ├── include/display_ui.h
│   │   └── display_ui_esp.c         ILI9341·LVGL·백라이트·화면 (Task 5, 7)
│   └── audio/
│       ├── include/audio.h
│       ├── audio_tone.c             사인파 생성과 볼륨 (순수, Task 6)
│       └── audio_esp.c              I2S 출력 작업 (Task 6)
├── test/host/
│   ├── CMakeLists.txt               components/*/*.c 중 *_esp.c 제외 (Task 1)
│   ├── test_ds3231_codec.c          (Task 2)
│   ├── test_time_rules.c            (Task 4)
│   ├── test_ui_text.c               (Task 5)
│   └── test_audio_tone.c            (Task 6)
├── tools/run_host_tests.sh          cppcheck 대상도 *_esp.c 제외 (Task 1)
└── docs/notes/jtag-ds3231.md        JTAG 실습 기록 (Task 3, 사용자)
```

---

### Task 1: 핀 배정, 보드 기동, 빌드 규칙

**작성:** Claude. 배선과 실기 확인은 사용자.

**Files:**
- Create: `main/board_pins.h`
- Modify: `test/host/CMakeLists.txt`, `tools/run_host_tests.sh`, `main/main.c`, `sdkconfig.defaults`

**Interfaces:**
- Produces: `board_pins.h`의 매크로 `PIN_LCD_SCLK`, `PIN_LCD_MOSI`, `PIN_LCD_MISO`, `PIN_LCD_CS`, `PIN_LCD_DC`, `PIN_LCD_RST`, `PIN_LCD_BL`, `PIN_SD_SCK`, `PIN_SD_MOSI`, `PIN_SD_MISO`, `PIN_SD_CS`, `PIN_I2C_SDA`, `PIN_I2C_SCL`, `PIN_I2S_BCLK`, `PIN_I2S_WS`, `PIN_I2S_DOUT`, `PIN_BUTTON`, `PIN_RESERVED_DIAL` (값은 위 핀 배정 표). `gpio_num_t` 캐스트 없이 정수로 둔다.
- Produces: PC 테스트 빌드 규칙 — `components/*/*.c` 전체에서 `_esp.c`로 끝나는 파일을 뺀 것이 `usagi_core` 라이브러리가 된다. include 경로는 `components/*/include` 전체.

- [ ] **Step 1: PC 테스트 빌드를 파일 규칙에 맞게 바꾼다**
  `test/host/CMakeLists.txt`: `file(GLOB CORE_SOURCES CONFIGURE_DEPENDS "${REPO_ROOT}/components/*/*.c")` 뒤에 `list(FILTER CORE_SOURCES EXCLUDE REGEX "_esp\\.c$")`. include는 `file(GLOB CORE_INCLUDES LIST_DIRECTORIES true "${REPO_ROOT}/components/*/include")`.
  `tools/run_host_tests.sh`의 cppcheck 대상: `find components -name '*.c' ! -name '*_esp.c'`, include도 `components/*/include` 전체.
  Run(WSL): `./tools/run_host_tests.sh`
  Expected: 계획 1의 48개 테스트 그대로 PASS, `== 모두 통과`

- [ ] **Step 2: `board_pins.h` 작성, `main.c`에서 부팅 로그에 핀 표를 한 줄씩 출력**
  `sdkconfig.defaults`에 추가: `CONFIG_ESP_TASK_WDT_EN=y`, `CONFIG_ESP_TASK_WDT_TIMEOUT_S=5`, `CONFIG_FREERTOS_HZ=1000`.
  Run: `idf.py build`
  Expected: `Project build complete`

- [ ] **Step 3: [실기] 보드만 USB로 연결해 플래시**
  Run: `idf.py -p <COM 포트> flash monitor`
  Expected: `I (…) usagi: boot`, 핀 표 로그, PSRAM 8MB 인식 로그(`Found 8MB PSRAM device`). 포트는 장치 관리자의 "USB 직렬 장치"(USB JTAG/serial debug unit)로 확인한다.

- [ ] **Step 4: Commit**

  ```bash
  git add main test/host/CMakeLists.txt tools/run_host_tests.sh sdkconfig.defaults
  git commit -m "chore: 핀 배정과 보드 기동, *_esp.c 빌드 규칙"
  ```

---

### Task 2: DS3231 드라이버 (I2C)

**작성:** Claude. 설계 9.1은 "사용자 직접 작성"이었으나 2026-11-19 사용자 결정으로 코드는 Claude가 쓰고, 사용자는 Task 3에서 이 드라이버를 JTAG와 로직 분석기로 뜯어본다. **[결정]**

**먼저 설명(설계 9.1):** Claude가 I2C 원리(SDA/SCL, 오픈 드레인과 풀업, START·주소·ACK·STOP, "레지스터 주소 쓰기 → 반복 START → 읽기" 순서)를 DS3231 레지스터 맵(0x00~0x06 시각, 0x0F 상태)과 함께 설명한다.

**Files:**
- Modify: `components/alarm_core/include/alarm_core.h`, `components/alarm_core/alarm_time.c`, `test/host/test_alarm_time.c`
- Create: `components/ds3231/CMakeLists.txt`, `components/ds3231/include/ds3231.h`, `components/ds3231/ds3231_codec.c`, `components/ds3231/ds3231_esp.c`
- Test: `test/host/test_ds3231_codec.c`

**Interfaces:**
- Produces (`alarm_core.h`):

  ```c
  typedef struct {
      int     year;     /* 예: 2026 */
      uint8_t month;    /* 1..12 */
      uint8_t day;      /* 1..31 */
      uint8_t hour;     /* 0..23 */
      uint8_t minute;
      uint8_t second;
      uint8_t weekday;  /* 0=월 .. 6=일 */
  } alarm_civil_t;

  void    alarm_civil_from_epoch(int64_t epoch_s, alarm_civil_t *out);  /* UTC든 로컬이든 epoch 초를 그대로 달력으로 */
  int64_t alarm_epoch_from_civil(const alarm_civil_t *c);               /* weekday는 무시 */
  ```
- Produces (`ds3231.h`, 순수 부분):

  ```c
  #define DS3231_I2C_ADDR   0x68
  #define DS3231_REG_TIME   0x00   /* 0x00..0x06 */
  #define DS3231_REG_STATUS 0x0F
  #define DS3231_STATUS_OSF 0x80

  bool ds3231_decode(const uint8_t regs[7], int64_t *utc_s);  /* BCD 범위 밖이면 false */
  bool ds3231_encode(int64_t utc_s, uint8_t regs[7]);         /* 2000..2099년 밖이면 false. 항상 24시간 모드, 요일 레지스터 = weekday+1 */
  bool ds3231_status_time_valid(uint8_t status_reg);          /* OSF가 꺼져 있으면 true */
  ```
- Produces (`ds3231.h`, ESP 부분):

  ```c
  typedef struct ds3231 ds3231_t;   /* 정의는 ds3231_esp.c 안, 정적 인스턴스 1개 */
  esp_err_t ds3231_init(int sda_gpio, int scl_gpio, ds3231_t **out);  /* I2C0 마스터 100kHz */
  esp_err_t ds3231_read_utc(ds3231_t *d, int64_t *utc_s);  /* OSF 켜짐 → ESP_ERR_INVALID_STATE, 디코드 실패 → ESP_ERR_INVALID_RESPONSE */
  esp_err_t ds3231_write_utc(ds3231_t *d, int64_t utc_s);  /* 시각을 쓰고 OSF를 지운다 */
  ```
  `ds3231.h`는 ESP 부분 선언을 `#ifdef ESP_PLATFORM`으로 감싼다. PC 테스트는 순수 부분만 본다.

- [ ] **Step 1: 실패하는 테스트 작성**
  `test_alarm_time.c`에 추가:

  ```c
  void test_civil_from_epoch_monday_0700(void)
  /* alarm_civil_from_epoch(1794812400) → 2026-11-16 07:00:00, weekday 0 */

  void test_epoch_from_civil_leap_day(void)
  /* 2028-02-29 23:59:59 → 1835481599 */

  void test_civil_round_trip_range(void)
  /* 1767225600(2026-01-01)부터 4102444799(2099-12-31 23:59:59)까지 86399초 간격으로
     from → to 왕복이 원래 값과 같다 */
  ```
  `test_ds3231_codec.c`:

  ```c
  void test_decode_known_time(void)
  /* {0x00,0x00,0x22,0x07,0x15,0x11,0x26} → true, 1794780000 (2026-11-15 22:00:00 UTC = 11-16 07:00 KST) */

  void test_encode_known_time(void)
  /* 1794780000 → true, regs == {0x00,0x00,0x22,0x07,0x15,0x11,0x26} (요일 7 = 일요일) */

  void test_encode_leap_day(void)
  /* 1835481599 → regs[4]==0x29, regs[5]==0x02, regs[6]==0x28; decode 하면 같은 값 */

  void test_decode_12h_mode(void)          /* Review Focus 3 */
  /* 시 레지스터 0x70(12시간 모드, PM, 10) → 22시; 0x52(12시간, AM, 12) → 0시; 0x72(PM 12) → 12시 */

  void test_decode_rejects_invalid_bcd(void)
  /* 초 0x5A, 분 0x60, 일 0x00, 월 0x13, 월 0x00 각각 false */

  void test_encode_rejects_out_of_range_year(void)
  /* 946684799(1999-12-31 23:59:59) → false; 4102444800(2100-01-01) → false */

  void test_osf_set_means_invalid(void)    /* Review Focus 1 */
  /* ds3231_status_time_valid(0x88) false, (0x08) true, (0x00) true */
  ```

- [ ] **Step 2: 실행해서 실패 확인**
  Run(WSL): `./tools/run_host_tests.sh`
  Expected: 링크 오류 또는 새 테스트 FAIL

- [ ] **Step 3: 순수 부분 구현**
  `alarm_civil_from_epoch`/`alarm_epoch_from_civil`은 Howard Hinnant의 `civil_from_days`/`days_from_civil` 알고리즘(그레고리력, 3월 시작 연도)을 쓰고, `alarm_weekday`와 같은 결과가 나오게 weekday를 채운다. `ds3231_codec.c`는 이 두 함수와 BCD 변환만으로 만든다. 월 레지스터의 century 비트(0x80)는 무시한다(2000~2099만 쓴다).

- [ ] **Step 4: 통과 확인**
  Run(WSL): `./tools/run_host_tests.sh`
  Expected: 전부 PASS, 커버리지 90% 이상, cppcheck 통과

- [ ] **Step 5: ESP 부분 구현(`ds3231_esp.c`)**
  `i2c_new_master_bus` + `i2c_master_bus_add_device`(0x68, 100kHz). 읽기는 `i2c_master_transmit_receive`로 레지스터 0x0F 1바이트, 0x00부터 7바이트. 쓰기는 0x00부터 7바이트, 그다음 상태 레지스터를 읽어 OSF 비트만 지워 다시 쓴다. 타임아웃 100ms.
  `main.c`에 임시 확인 코드: 부팅 때 `ds3231_read_utc` 결과와 에러 이름을 로그로 출력.

- [ ] **Step 6: [실기] 배선하고 읽기 확인**
  배선: 모듈 VCC→3V3, GND, SDA→GPIO8, SCL→GPIO18 (모듈 풀업 사용). CR2032를 끼운다.
  Run: `idf.py -p <포트> flash monitor`
  Expected: 새 모듈이면 `ESP_ERR_INVALID_STATE`(OSF 켜짐). 이 결과도 정상이다. 시각 쓰기 확인은 Task 4의 `settime`에서 한다.

- [ ] **Step 7: [실기] 로직 분석기로 파형 캡처**
  PulseView: 채널 0=SCL, 1=SDA, 2MHz 샘플링, I2C 디코더. 부팅 때 읽기 한 번을 캡처한다.
  Expected: `Write 0x68 ACK, 0x0F` → `Read 0x68 ACK, <상태> NACK`, 이어서 0x00부터 7바이트 읽기. 캡처 파일(`.sr`)은 `docs/notes/captures/`에 저장한다(Task 3에서 씀).

- [ ] **Step 8: Commit**

  ```bash
  git add components/alarm_core components/ds3231 test/host main docs/notes
  git commit -m "feat(ds3231): BCD 레지스터 변환과 I2C 드라이버"
  ```

---

### Task 3: JTAG 디버깅 실습 (사용자)

**작성:** 사용자가 직접 진행한다. Claude는 순서와 개념(OpenOCD가 하는 일, 하드웨어 브레이크포인트 2개 제한, `-Og` 최적화와 변수 표시)을 설명하고 막히면 같이 본다. 코드는 바꾸지 않는다.

**Files:**
- Create: `docs/notes/jtag-ds3231.md` (사용자가 쓰는 실습 기록, 스크린샷 포함. 포트폴리오 자료)

**Interfaces:**
- Consumes: Task 2의 `ds3231_read_utc`, `ds3231_decode`, Step 7 캡처 파일

- [ ] **Step 1: OpenOCD 연결**
  Run: `idf.py openocd` (다른 터미널에서 `idf.py gdb`, 또는 VS Code ESP-IDF 확장의 디버그 시작)
  Expected: OpenOCD 로그에 `esp32s3.cpu0: Debug controller was reset`, GDB가 `app_main`에서 멈출 수 있음. 드라이버 문제면 ESP-IDF 설치 도구의 USB JTAG 드라이버를 다시 설치한다.

- [ ] **Step 2: `ds3231_decode`에 브레이크포인트를 걸고 한 줄씩 따라간다**
  `regs[0..6]` 값을 GDB의 `x/7xb regs`로 보고, Task 2 Step 7 캡처의 7바이트와 같은지 비교한다. BCD → 10진 변환 뒤 `alarm_civil_t` 필드와 최종 `*utc_s`를 확인한다.
  Expected: 캡처 바이트 = 메모리 바이트, 변환 결과 = 같은 시각의 UTC epoch

- [ ] **Step 3: 일부러 버그를 넣었다 찾기(선택)**
  `ds3231_decode`에서 분 레지스터 마스크를 `0x7F` → `0xFF`로 바꿔 빌드하고, 59분 다음에 이상한 값이 나오는 이유를 디버거로 찾은 뒤 되돌린다.

- [ ] **Step 4: 기록 작성 후 Commit**
  `docs/notes/jtag-ds3231.md`: 연결 방법, 본 화면(스크린샷), 캡처와 메모리 비교, 배운 점.

  ```bash
  git add docs/notes
  git commit -m "docs: JTAG로 DS3231 읽기 따라가기 실습 기록"
  ```

---

### Task 4: 시간 관리(timekeeper)와 개발용 콘솔

**작성:** Claude. 실기 확인은 사용자.

**Files:**
- Create: `components/timekeeper/CMakeLists.txt`, `components/timekeeper/include/timekeeper.h`, `components/timekeeper/time_rules.c`, `components/timekeeper/timekeeper_esp.c`, `main/dev_console_esp.c`, `main/dev_console_esp.h`
- Test: `test/host/test_time_rules.c`

**Interfaces:**
- Consumes: Task 2의 `ds3231_t`, `ds3231_read_utc`, `ds3231_write_utc`; 계획 1의 `alarm_local_from_utc`
- Produces (`timekeeper.h`):

  ```c
  #define TIME_VALID_MIN_UTC 1767225600LL   /* 2026-01-01 00:00:00 UTC */
  #define TIME_VALID_END_UTC 4102444800LL   /* 2100-01-01 00:00:00 UTC (미만) */
  bool timekeeper_utc_plausible(int64_t utc_s);              /* 순수 */

  /* 아래는 ESP 부분 (#ifdef ESP_PLATFORM) */
  void      timekeeper_init(ds3231_t *rtc);   /* 부팅 때 1회: RTC 읽기 → 유효하면 settimeofday */
  bool      timekeeper_now_local(int64_t *local_s);  /* 시간을 모르면 false */
  esp_err_t timekeeper_set_utc(int64_t utc_s);       /* 범위 검사 → settimeofday → RTC 쓰기 */
  ```
  "시간을 안다" = 부팅 때 RTC 값이 유효했거나, 그 뒤 `timekeeper_set_utc`가 성공했다. 계획 4의 SNTP도 `timekeeper_set_utc`를 부른다.
- Produces (`dev_console_esp.h`): `void dev_console_start(void);` — USB Serial/JTAG REPL(`esp_console_new_repl_usb_serial_jtag`), 프롬프트 `usagi>`. 이 Task의 명령: `settime <utc_epoch>`, `time`(UTC, 로컬 시각, 시간 앎 여부 출력).

- [ ] **Step 1: 실패하는 테스트 작성** (`test_time_rules.c`)

  ```c
  void test_plausible_accepts_range(void)
  /* 1767225600 true, 1794780000 true, 4102444799 true */

  void test_plausible_rejects_before_2026(void)   /* Review Focus 2 */
  /* 946684800(2000-01-01) false, 1767225599 false, 0 false, -1 false */

  void test_plausible_rejects_2100(void)          /* Review Focus 2 */
  /* 4102444800 false */
  ```

- [ ] **Step 2: 실행해서 실패 확인**
  Run(WSL): `./tools/run_host_tests.sh`
  Expected: FAIL

- [ ] **Step 3: `time_rules.c`, `timekeeper_esp.c`, `dev_console_esp.c` 구현**
  `timekeeper_init`: `ds3231_read_utc` 성공 && `timekeeper_utc_plausible` → `settimeofday`, 상태 "앎". 아니면 "모름"으로 두고 경고 로그(원인 포함). "앎" 플래그는 `timekeeper_esp.c`의 정적 변수(작업 간 공유이므로 `_Atomic bool`).
  `main.c` 부팅 순서: 로그 → `ds3231_init` → `timekeeper_init` → `dev_console_start`.

- [ ] **Step 4: 통과 확인**
  Run(WSL): `./tools/run_host_tests.sh` → 전부 PASS
  Run: `idf.py build` → `Project build complete`

- [ ] **Step 5: [실기] 시간 설정과 유지 확인**
  1. 플래시 후 `usagi> time` → `unknown`
  2. PC에서 현재 UTC epoch를 구한다. Windows PowerShell: `[DateTimeOffset]::UtcNow.ToUnixTimeSeconds()`
  3. `usagi> settime <값>` → `time`이 로컬 시각(KST)을 맞게 출력
  4. USB를 뽑고 1분 뒤 다시 꽂는다 → 부팅 로그에 RTC 시간 사용, `time`이 1분 지난 시각
  Expected: 4에서 오차 1초 이내

- [ ] **Step 6: Commit**

  ```bash
  git add components/timekeeper main test/host/test_time_rules.c
  git commit -m "feat(timekeeper): RTC로 시간 복원, 수동 설정 콘솔"
  ```

---

### Task 5: 화면 — ILI9341, LVGL, 시계 화면

**작성:** Claude. 배선과 실기 확인은 사용자.

**먼저 설명(설계 9.1):** Claude가 SPI 원리(SCLK·MOSI·MISO·CS, 모드 0, 클럭 속도)와 ILI9341의 DC 핀(명령/데이터 구분), 화면 메모리 쓰기 흐름(CASET → PASET → RAMWR), DMA로 화면을 보내는 이유를 설명한다. 로직 분석기로 볼 때는 SPI 클럭을 1MHz로 낮춘 빌드로 초기화 명령 몇 개만 캡처한다.

**Files:**
- Create: `components/ui_text/CMakeLists.txt`, `components/ui_text/include/ui_text.h`, `components/ui_text/ui_text.c`, `components/display_ui/CMakeLists.txt`, `components/display_ui/include/display_ui.h`, `components/display_ui/display_ui_esp.c`, `main/idf_component.yml`
- Test: `test/host/test_ui_text.c`

**Interfaces:**
- Consumes: Task 2의 `alarm_civil_from_epoch`, Task 4의 `timekeeper_now_local`
- Produces (`ui_text.h`, 순수):

  ```c
  /* 로컬 epoch 초 → "7:30"(앞자리 0 없음) + PM 여부. out은 6바이트 이상 */
  void ui_fmt_clock(int64_t local_s, char *out, size_t n, bool *pm);
  /* "MON, NOV 23" (영문 대문자, 요일 3글자, 월 3글자, 일은 앞자리 0 없음) */
  void ui_fmt_date(int64_t local_s, char *out, size_t n);
  /* 남은 초 → "4:59", 3600 → "60:00", 0 이하 → "0:00" */
  void ui_fmt_countdown(int64_t seconds, char *out, size_t n);
  ```
- Produces (`display_ui.h`, ESP):

  ```c
  typedef enum { UI_MODE_CLOCK = 0, UI_MODE_PRE, UI_MODE_RINGING, UI_MODE_SNOOZED } ui_mode_t;
  typedef struct {
      bool      time_known;
      int64_t   now_local;
      int64_t   next_alarm_local;  /* 없으면 -1 */
      ui_mode_t mode;
      int64_t   ring_at;           /* PRE: 남은 시간 계산용 */
      int64_t   snooze_until;      /* SNOOZED */
      char      label[24];         /* 울리는 알람 이름 */
  } ui_state_t;

  typedef struct { int sclk, mosi, miso, cs, dc, rst, bl; } display_ui_pins_t;

  esp_err_t display_ui_start(const display_ui_pins_t *pins);  /* LCD·LVGL·백라이트 초기화, 화면 작업 시작 */
  void      display_ui_post(const ui_state_t *s);/* 길이 1 큐에 xQueueOverwrite. 어느 작업에서 불러도 됨 */
  void      display_ui_set_brightness(uint8_t pct);  /* 0..100, LEDC 듀티 */
  ```
  이 Task에서는 `UI_MODE_CLOCK`만 그린다. 나머지 모드는 Task 7에서 채운다.

- [ ] **Step 1: 실패하는 테스트 작성** (`test_ui_text.c`)

  ```c
  void test_clock_morning(void)
  /* 07:30 → "7:30", pm false */

  void test_clock_midnight_and_noon(void)   /* Review Focus 4 */
  /* 00:05 → "12:05" AM; 00:59 → "12:59" AM; 12:00 → "12:00" PM; 13:07 → "1:07" PM; 23:59 → "11:59" PM */

  void test_date_format(void)
  /* MON_0700(1794812400) → "MON, NOV 16"; SUN_1122_2357(1795391820) → "SUN, NOV 22" */

  void test_countdown(void)
  /* 299 → "4:59"; 3600 → "60:00"; 59 → "0:59"; 0 → "0:00"; -5 → "0:00" */

  void test_small_buffer_is_truncated_safely(void)
  /* n = 3 으로 ui_fmt_date 호출 → out[2] == '\0', 넘쳐 쓰지 않음 */
  ```

- [ ] **Step 2: 실행해서 실패 확인**
  Run(WSL): `./tools/run_host_tests.sh` → FAIL

- [ ] **Step 3: `ui_text.c` 구현**
  `snprintf`만 쓴다. 날짜는 `alarm_civil_from_epoch`로 구한다.

- [ ] **Step 4: 통과 확인**
  Run(WSL): `./tools/run_host_tests.sh` → 전부 PASS, 커버리지 90% 이상

- [ ] **Step 5: `display_ui_esp.c` 구현**
  - 의존성(`main/idf_component.yml`): `espressif/esp_lcd_ili9341: "^2"`, `espressif/esp_lvgl_port: "^2"`, `lvgl/lvgl: "^9"`. 처음 빌드 때 만들어지는 `dependencies.lock`을 커밋해 버전을 고정한다.
  - SPI2, 40MHz, 모드 0. `esp_lcd_new_panel_ili9341`, 세로(240×320) 또는 가로 중 **가로(320×240)** **[제안]**.
  - LVGL 버퍼: 내부 DMA RAM에 320×40 픽셀 2개(더블 버퍼). PSRAM은 쓰지 않는다(SPI DMA는 내부 RAM이 빠르다).
  - 컨트롤러 확인(parts-list E.7): 패널 초기화 전에 `esp_lcd_panel_io_rx_param(io, 0x04, buf, 3)`으로 RDDID를 읽어 로그로 남긴다. 값이 0이어도 화면이 나오면 계속한다(모듈에 따라 SDO가 연결되지 않음).
  - 백라이트: LEDC 5kHz, 10비트, 시작 밝기 80%.
  - 화면 작업: 200ms마다 큐를 확인해 상태가 바뀌었으면 `lvgl_port_lock` 안에서 라벨을 갱신한다. 시각은 큰 숫자(Montserrat 48)와 AM/PM(Montserrat 24), 아래에 날짜, 그 아래에 `NEXT 7:00 AM` 또는 `NO ALARM`. 시간을 모르면 `--:--`와 `SET TIME`.
  - 이 Task에서는 `main.c`가 1초마다 `timekeeper_now_local`로 `ui_state_t`를 채워 `display_ui_post`를 부른다(Task 7에서 `alarm_service`로 옮김).

- [ ] **Step 6: [실기] 배선하고 화면 확인**
  배선: VCC→3V3, GND, CS→10, RESET→14, DC→9, SDI(MOSI)→11, SCK→12, LED→21, SDO(MISO)→13.
  Run: `idf.py -p <포트> flash monitor`
  Expected: RDDID 로그, 화면에 현재 시각(12시간제)과 날짜. 1분 넘게 지켜봐서 분이 바뀌는지 확인. 색이 반전되거나 좌우가 뒤집히면 `esp_lcd_panel_invert_color`/`mirror`/`swap_xy`를 조정하고 그 값을 이 문서에 적는다 **[실물 확인]**.

- [ ] **Step 7: Commit**

  ```bash
  git add components/ui_text components/display_ui main test/host/test_ui_text.c
  git commit -m "feat(display): ILI9341·LVGL 12시간제 시계 화면"
  ```

---

### Task 6: 비프음 — I2S와 MAX98357A

**작성:** Claude. 배선과 실기 확인은 사용자.

**먼저 설명(설계 9.1):** Claude가 I2S 원리(BCLK·WS·DATA, 샘플레이트와 비트 수, 왼쪽/오른쪽 채널)와 MAX98357A가 디지털 입력을 받아 바로 스피커를 구동하는 구조(D급 앰프, SD 핀의 채널 선택, GAIN 핀)를 설명하고, 로직 분석기로 WS·BCLK·DATA 파형을 함께 본다.

**Files:**
- Create: `components/audio/CMakeLists.txt`, `components/audio/include/audio.h`, `components/audio/audio_tone.c`, `components/audio/audio_esp.c`
- Test: `test/host/test_audio_tone.c`

**Interfaces:**
- Produces (`audio.h`, 순수):

  ```c
  #define AUDIO_SAMPLE_RATE 16000
  #define AUDIO_PEAK_MAX    16383      /* 볼륨 100일 때 최대 진폭 */

  typedef struct { uint32_t phase; } audio_tone_t;   /* 32비트 위상 누산기 */

  /* freq_hz 사인파를 n개 채운다. volume 0..100(100 초과는 100으로). 호출 사이에 위상이 이어진다 */
  void audio_tone_fill(audio_tone_t *t, int16_t *out, size_t n, uint32_t freq_hz, uint8_t volume);
  ```
- Produces (`audio.h`, ESP):

  ```c
  typedef enum {
      AUDIO_OP_STOP = 0,
      AUDIO_OP_ALARM_LOOP,   /* 본 알람 대신: 2kHz 0.25초 켜짐/0.25초 꺼짐 반복. STOP까지 계속 (계획 3에서 MP3로 바뀜) */
      AUDIO_OP_PRE_TICK,     /* 미리 알림 클립 대신: 1kHz 80ms 한 번 (계획 3에서 클립으로 바뀜) */
      AUDIO_OP_CONFIRM,      /* 길게 누르기 확인음: 3kHz 60ms, 쉼 60ms, 3kHz 60ms */
  } audio_op_t;

  typedef struct { audio_op_t op; uint8_t volume; } audio_cmd_t;

  esp_err_t audio_start(int bclk_gpio, int ws_gpio, int dout_gpio);  /* I2S0 std, 16kHz, 16비트, 모노(왼쪽 슬롯), 출력 작업 시작 */
  void      audio_post(const audio_cmd_t *cmd); /* 길이 8 큐, 꽉 차면 가장 오래된 것을 버리지 않고 이번 것을 버림(로그) */
  ```
  출력 작업은 큐에서 받은 순서대로 처리한다. `STOP`은 지금 소리를 즉시 끊는다. `CONFIRM`과 `PRE_TICK`은 `ALARM_LOOP`를 멈추게 하지 않는다(짧은 소리가 끝나면 반복으로 돌아감).

- [ ] **Step 1: 실패하는 테스트 작성** (`test_audio_tone.c`)

  ```c
  void test_volume_zero_is_silent(void)
  /* 1000Hz, volume 0, 160샘플 → 전부 0 */

  void test_tone_peak_capped_at_volume_100(void)   /* Review Focus 5 */
  /* volume 100과 volume 255로 각각 1600샘플 → 최댓값 절댓값 <= AUDIO_PEAK_MAX, 그리고 >= AUDIO_PEAK_MAX * 95 / 100 */

  void test_volume_scales_linearly(void)
  /* volume 50의 최대 진폭이 volume 100의 45~55% */

  void test_frequency_by_zero_crossings(void)
  /* 1000Hz, 16000샘플(1초) → 음에서 양으로 넘어가는 횟수 999~1001 */

  void test_phase_continues_between_calls(void)
  /* 같은 tone으로 80샘플 두 번 채운 결과 == 160샘플 한 번 채운 결과 */
  ```

- [ ] **Step 2: 실행해서 실패 확인**
  Run(WSL): `./tools/run_host_tests.sh` → FAIL

- [ ] **Step 3: `audio_tone.c` 구현**
  256칸 사인 표(`static const int16_t`, 진폭 32767)와 32비트 위상 누산기를 쓴다(위상 증가 = `freq_hz * 2^32 / AUDIO_SAMPLE_RATE`). 표 값 × `AUDIO_PEAK_MAX` × volume / (32767 × 100), 정수 연산만 쓴다.

- [ ] **Step 4: 통과 확인**
  Run(WSL): `./tools/run_host_tests.sh` → 전부 PASS

- [ ] **Step 5: `audio_esp.c` 구현**
  `i2s_new_channel` + `i2s_channel_init_std_mode`(Philips, 16비트, 모노). 출력 작업은 10ms(160샘플) 단위로 버퍼를 채워 `i2s_channel_write`. 소리가 없을 때는 0을 계속 보낸다(앰프 "팝" 소리 방지). 작업 우선순위는 `alarm_service`보다 높게 둔다.
  `main.c`에 임시 확인: 부팅 3초 뒤 `CONFIRM` 한 번.

- [ ] **Step 6: [실기] 배선하고 소리 확인**
  배선: 앰프 VIN→5V, GND, BCLK→15, LRC→16, DIN→17, SD·GAIN은 연결하지 않음(기본: 좌우 평균, 9dB). 스피커는 앰프 +/− 단자. 전원을 켠 채로 앰프·스피커 배선을 바꾸지 않는다.
  Expected: 부팅 3초 뒤 "삑삑" 확인음. 볼륨 100(`AUDIO_PEAK_MAX`)에서 너무 크거나 찌그러지면 `AUDIO_PEAK_MAX`를 낮추고 이 문서의 Global Constraints 값을 고친다 **[실물 확인]**.

- [ ] **Step 7: Commit**

  ```bash
  git add components/audio main test/host/test_audio_tone.c
  git commit -m "feat(audio): I2S 사인파 비프음과 볼륨 상한"
  ```

---

### Task 7: 통합 — 버튼, 알람 서비스, 알람 화면

**작성:** Claude. 실기 시나리오 확인은 사용자.

**Files:**
- Create: `components/button/button_gpio_esp.c`, `main/alarm_service_esp.c`, `main/alarm_service_esp.h`
- Modify: `components/button/include/button_fsm.h`(ESP 부분 선언 추가), `components/button/CMakeLists.txt`, `components/display_ui/display_ui_esp.c`, `main/dev_console_esp.c`, `main/main.c`

**Interfaces:**
- Consumes: 계획 1의 `alarm_engine_*`, `alarm_validate`, `alarm_next_ring`, `button_fsm_*`; Task 4의 `timekeeper_now_local`; Task 5의 `display_ui_post`, `display_ui_set_brightness`; Task 6의 `audio_post`
- Produces (`button_fsm.h`, `#ifdef ESP_PLATFORM`):

  ```c
  typedef void (*button_cb_t)(button_event_t ev, void *ctx);
  /* GPIO를 입력+내부 풀업으로 설정하고 10ms 주기 작업에서 button_fsm_update를 부른다. LOW = 눌림 */
  esp_err_t button_gpio_start(int gpio, uint32_t long_ms, button_cb_t cb, void *ctx);
  ```
  인터럽트 대신 10ms 폴링을 쓴다. `button_fsm`이 10ms 샘플을 전제로 만들어졌고, 스위치 하나라 CPU 부담이 없다 **[제안]**.
- Produces (`alarm_service_esp.h`):

  ```c
  void         alarm_service_start(const alarm_settings_t *s);  /* 엔진 init, 작업 생성, TWDT 등록 */
  void         alarm_service_post_input(alarm_input_t in);     /* 버튼 콜백에서 부름. 길이 4 큐 */
  esp_err_t    alarm_service_set_alarms(const alarm_t *a, size_t n);  /* 하나라도 alarm_validate 실패면 ESP_ERR_INVALID_ARG, 엔진은 그대로 */
  size_t       alarm_service_get_alarms(alarm_t *out, size_t max);
  alarm_view_t alarm_service_view(void);
  ```
  엔진은 `alarm_service_esp.c` 안의 정적 변수 하나이고 뮤텍스로 보호한다. 콘솔(다른 작업)은 위 함수로만 접근한다.
- 알람 작업 한 바퀴:
  1. 다음 초 경계까지 남은 ms를 `gettimeofday`로 구해서, 그 시간을 제한으로 입력 큐를 기다린다.
  2. 입력이 오면 `alarm_engine_input` → 출력 처리. 초 경계가 지나면 `timekeeper_now_local`이 true일 때만 `alarm_engine_tick` → 출력 처리. 시간을 모르면 틱을 건너뛴다.
  3. 출력 처리 순서는 `AUDIO_STOP` → `CONFIRM_BEEP` → `MAIN_START` → `PRE_CLIP`이다(alarm_core.h 주석의 약속). 각각 `AUDIO_OP_STOP`, `AUDIO_OP_CONFIRM`, `AUDIO_OP_ALARM_LOOP`(main_volume), `AUDIO_OP_PRE_TICK`(pre_volume)으로 보낸다. 볼륨은 `out.alarm_id`로 알람 목록에서 찾고, 목록에서 지워졌으면 60을 쓴다 **[제안]**.
  4. `DISABLE_ONCE`는 로그만 남긴다(저장은 계획 4).
  5. `ui_state_t`를 채워 `display_ui_post`. `RINGING`이 되면 밝기 100%, `IDLE`로 돌아오면 80%.
  6. `esp_task_wdt_reset()`.
- 콘솔 명령 추가: `alarm add <id> <HH:MM> <days_hex> <pre_min> [label]`(볼륨 70/70, 노래·클립 빈 값), `alarm list`, `alarm del <id>`, `alarm clear`. 모두 `alarm_service_get_alarms` → 수정 → `alarm_service_set_alarms`로 한다.
- 화면 모드 추가: `UI_MODE_PRE`는 `USAGI!`와 남은 시간(`ui_fmt_countdown`), `UI_MODE_RINGING`은 알람 이름과 `HOLD TO STOP`, `UI_MODE_SNOOZED`는 `SNOOZE`와 다시 울릴 시각.

- [ ] **Step 1: 구현**
  `main.c` 부팅 순서(핀은 모두 `board_pins.h`에서 인자로 넘김): 로그 → `ds3231_init` → `timekeeper_init` → `display_ui_start` → `audio_start` → `alarm_service_start(&alarm_settings_default())` → `button_gpio_start(PIN_BUTTON, settings.long_press_ms, …)` → `dev_console_start`. Task 5·6의 임시 확인 코드는 지운다.

- [ ] **Step 2: PC 테스트와 빌드**
  Run(WSL): `./tools/run_host_tests.sh` → 전부 PASS
  Run: `idf.py build` → `Project build complete`

- [ ] **Step 3: [실기] 버튼 배선**
  KW-10 COM→GND, NO→GPIO47(내부 풀업). 콘솔에서 짧게/길게 눌러 로그(`button: SHORT`, `button: LONG`)가 나오는지 본다.

- [ ] **Step 4: [실기] 알람 시나리오 확인**
  지금 시각이 10:00이라면 아래처럼 넣고 차례로 확인한다(시각은 그때에 맞춰 바꾼다).
  1. `alarm add 1 10:03 7f 2 TEST` → 10:01:00에 화면 `USAGI!`와 `2:00`, 매초 짧은 삑 → 10:03:00에 반복 비프, 화면 밝기 최대
  2. 짧게 누름 → 소리 멈춤, 화면 `SNOOZE`와 10:08 → 10:08:00에 다시 반복 비프(미리 알림 없음)
  3. 2초 누름 → 손을 떼기 전에 확인음, 소리 멈춤, 시계 화면으로
  4. `alarm add 2 10:12 0 1` → 미리 알림 중 짧게 누름 → 미리 알림만 멈추고 10:12:00에 울림 → 그대로 10분 둠 → 10:22:00에 자동 정지, 로그에 `DISABLE_ONCE id=2`
  5. 버튼을 누른 채 리셋 → 부팅 후 손을 떼도 아무 동작 없음
  6. 울리는 중 USB를 뽑았다 다시 꽂음 → 부팅 뒤 지난 알람이 울리지 않음(설계 5.1)
  Expected: 1~6이 모두 설명대로 동작한다. 다르면 로그와 함께 기록하고 고친다.

- [ ] **Step 5: [실기] 밤샘 시험**
  `alarm add 3 07:00 7f 5 MORNING`을 넣고 밤새 둔다. 다음 날 아침 미리 알림과 울림을 확인하고, 콘솔 로그에서 리셋이나 워치독 경고가 없었는지 본다(설계 6장).

- [ ] **Step 6: Commit + push, CI 녹색 확인**

  ```bash
  git add components main
  git commit -m "feat: 버튼·알람 서비스·화면을 연결해 알람 흐름 실기 동작"
  git push
  ```

---

## 계획 2 완료 기준

- 시계 한 대가 브레드보드에서 Task 7 Step 4의 시나리오 1~6과 밤샘 시험을 통과한다.
- PC 테스트 전부 통과, 순수 C 파일 줄 커버리지 90% 이상, cppcheck 경고 0, GitHub Actions 녹색
- `main/board_pins.h`와 이 문서의 핀 표가 실물 배선과 같다. **[실물 확인]** 표시를 모두 실제 값으로 바꾸거나 "확인함"으로 고친다.
- `docs/notes/jtag-ds3231.md`(사용자 실습 기록)가 저장소에 있다.
- 다음 단계: 계획 3(SD카드, MP3, "우! 나!" 클립)을 작성한다.
