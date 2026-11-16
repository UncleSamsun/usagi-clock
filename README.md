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
  alarm_core/   알람 판단 로직(순수 C, 하드웨어 의존 없음)
  button/       버튼 디바운스와 짧게/길게 판별(순수 C)
main/           app_main
test/host/      PC 단위 테스트(Unity)
tools/          테스트 스크립트
docs/           설계 문서와 구현 계획
```

알람·버튼 로직은 ESP-IDF 헤더를 쓰지 않는 순수 C라서, 같은 소스를 펌웨어와 PC 테스트에서 함께 컴파일해요.

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

## 라이선스

추후 결정
