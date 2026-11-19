# usagi-clock

ESP32-S3로 만드는 탁상 알람시계예요. 알람 몇 분 전부터 1초마다 짧은 클립("우! 나!")을 반복하다가, 정각에 본 알람 노래를 울려요. 알람·소리·볼륨은 같은 Wi-Fi 안에서 휴대폰 웹 페이지로 설정해요.

> 캐릭터 이미지와 음성 파일은 저작물이라 이 저장소에 넣지 않아요. 사용자가 microSD 카드로 직접 넣어요.

## 하드웨어

| 부품 | 모델 | 연결 |
|---|---|---|
| MCU | WeMos LOLIN S3 V1.0.0 (ESP32-S3, 16MB 플래시, 8MB 옥탈 PSRAM) | — |
| 디스플레이 | 2.8인치 ILI9341 240×320 IPS (SD 슬롯 내장) | SPI |
| 오디오 앰프 | MAX98357A + 40mm 4Ω 3W 스피커 | I2S |
| RTC | DS3231 + CR2032 | I2C |
| 버튼 | 스누즈 바 + KW-10 마이크로 스위치 | GPIO |
| 전원 | USB-C 5V | — |

## 구조

```
components/
  alarm_core/     알람 판단: 다음 울림 시각, 회차 상태 머신(미리 알림·스누즈·자동 정지·합치기)
  snooze_button/  스누즈 바: 디바운스와 짧게/길게 판별, GPIO 폴링
  ds3231/         RTC 드라이버(직접 작성): BCD 레지스터 변환, I2C
  timekeeper/     RTC ↔ 시스템 시간 ↔ SNTP, 유효 시간 범위
  ui_text/        화면 문자열(12시간제, 날짜, 남은 시간)
  display_ui/     ILI9341 + LVGL 화면, 백라이트 PWM
  audio/          SD카드, MP3 해독(minimp3), 재생 결정(실패 시 비프), 클립 캐시, I2S
  app_config/     설정 값·검사·야간 밝기, NVS 저장 형식(버전·CRC)
  net/            Wi-Fi 정책(STA/설정 모드), SNTP, mDNS, 캡티브 포털 DNS
  web_api/        REST API, JSON 검사, 업로드, 웹 펌웨어 업데이트
main/             부팅 순서, 알람 서비스(FreeRTOS 작업), 개발용 콘솔, 핀 배정
web/              아이폰용 설정 페이지(HTML/CSS/JS, LittleFS에 들어감)
test/host/        PC 단위 테스트(Unity)
tools/            테스트 스크립트
docs/             설계 문서와 구현 계획
```

**파일 규칙:** ESP-IDF 헤더를 쓰는 파일은 이름이 `_esp.c`로 끝나요. 나머지 `components/*/*.c`는 ESP-IDF 없이 컴파일되는 순수 C라서, 같은 소스를 펌웨어와 PC 테스트에서 함께 컴파일하고 커버리지와 정적 분석을 받아요.

## 개발 환경

- ESP-IDF **v6.1** (Windows: ESP-IDF Installation Manager v0.21.1, 설치 경로 `C:\esp\v6.1\esp-idf`, 기능 `core` + `ide`)
- 편집기: VS Code + ESP-IDF 확장 (`ESP-IDF: Select Current ESP-IDF Version`에서 v6.1 선택)
- PC 테스트: WSL2 Ubuntu 24.04 + `build-essential cmake ninja-build gcovr cppcheck`

## 빌드와 테스트

펌웨어(ESP-IDF 터미널):

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

PC 단위 테스트(WSL):

```bash
./tools/run_host_tests.sh
```

빌드 → 테스트 → 커버리지(gcovr, 라인 90% 이상) → 정적 분석(cppcheck)을 차례로 돌려요. 커버리지 리포트는 `build-host/coverage/index.html`에 생겨요.

실제 MP3로 음 높이까지 확인하는 테스트는 테스트용 사인파 파일이 있어야 돌아요(없으면 건너뜀).

```bash
sudo apt install -y ffmpeg
test/host/fixtures/make_fixtures.sh
```

## 개발용 콘솔

`idf.py monitor`에서 `usagi>` 프롬프트로 명령을 쓸 수 있어요. 웹 설정이 없어도 시험할 수 있게 만든 거예요.

| 명령 | 예 |
|---|---|
| 시간 설정(UTC 초) | `settime 1794780000` (PowerShell: `[DateTimeOffset]::UtcNow.ToUnixTimeSeconds()`) |
| 지금 시각 | `time` |
| 알람 | `alarm add 1 07:00 1f 5 work` (요일 비트: 월=01 … 일=40, 매일 7f, 평일 1f, 한 번 0), `alarm list`, `alarm del 1`, `alarm song 1 a.mp3`, `alarm clips 1 default` |

## SD카드

FAT32로 포맷하고 아래처럼 넣어요. 파일 이름은 한글도 돼요.

```
/sounds/<노래>.mp3            본 알람 노래
/clips/default/01_u.mp3      미리 알림 클립 세트 "default" (이름 순서대로 1초마다 하나씩)
/clips/default/02_na.mp3
```

SD카드가 없거나 파일이 깨져 있어도 알람은 비프음으로 울려요.

## 라이선스

추후 결정
