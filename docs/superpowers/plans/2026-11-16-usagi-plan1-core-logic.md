# 우사기 알람시계 — 계획 1: 개발 기반 + 알람 핵심 로직 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **이 프로젝트의 실행 방식 [사용자 결정]:** 사용자가 학습하면서 Claude와 함께 짜는 페어 방식이다. 각 Task의 "작성" 줄을 따른다. "사용자" Task는 Claude가 개념을 설명하고 사용자가 코드를 쓴 뒤 Claude가 리뷰한다. 서브에이전트가 대신 구현하지 않는다.

**Goal:** 하드웨어 없이 PC에서 검증할 수 있는 알람 판단 로직(`alarm_core`)과 버튼 판별 로직(`button_fsm`)을 테스트와 함께 완성한다. ESP-IDF 프로젝트 뼈대, PC 테스트, CI(빌드, 테스트, 커버리지, 정적 분석)도 함께 갖춘다.

**Architecture:** 알람과 버튼 로직은 ESP-IDF 헤더를 전혀 쓰지 않는 순수 C 컴포넌트로 만든다. 같은 소스를 ESP-IDF 빌드(`idf_component_register`)와 PC용 CMake 테스트 빌드(Unity) 양쪽에서 컴파일한다. 시간은 "로컬 epoch 초"(`int64_t`, UTC+9를 더한 1970-01-01 00:00 기준 초)로 다룬다. 그래서 libc 시간대 설정과 무관하게 결정적으로 테스트할 수 있다.

**Tech Stack:** ESP-IDF(설치 시점 최신 안정판, v5.5 이상), C11, Unity(ThrowTheSwitch, PC 테스트), CMake + Ninja, gcovr(커버리지), cppcheck, GitHub Actions, PC 테스트 환경은 WSL2 Ubuntu

**Spec:** `docs/specs/2026-11-16-usagi-alarm-clock-design.md`

## 전체 로드맵 (계획 파일은 단계마다 따로 작성)

| 계획 | 내용 | 작성 시점 |
|---|---|---|
| **1 (이 문서)** | 개발 기반, `alarm_core`, `button_fsm`, CI | 지금 |
| 2 | 하드웨어 기동: LCD+LVGL 시계 화면, DS3231 드라이버(직접 작성), 버튼 GPIO, `timekeeper`(RTC↔시스템 시간), FreeRTOS 작업 구조와 `alarm_core` 연결, 비프음, **JTAG 디버깅 실습**(내장 USB-JTAG + OpenOCD + GDB, VS Code F5. DS3231 읽기 함수에 브레이크포인트를 걸고 BCD→시각 변환을 따라가며 로직 분석기 I2C 파형과 비교) [사용자 결정] | 부품 도착 후(핀 배정을 실물로 확정해야 해서) |
| 3 | 오디오: SD 마운트(별도 SPI 버스), MP3 디코딩, I2S DMA, 볼륨, 미리 알림 클립 세트, 비프음 대체 | 계획 2 완료 후 |
| 4 | 네트워크와 웹: Wi-Fi, SNTP, mDNS, REST API, 웹 페이지, NVS 저장, 야간 밝기, 업로드, 캡티브 포털, 웹 펌웨어 업데이트 | 계획 3 완료 후 |
| 5 | 통합: 두 대 조립, 케이스 모델링과 출력, 장시간 시험, README | 계획 4와 병행 |

## 준비 작업 (코드 아님, 이번 주)

- [x] 부품 주문 — **2026-11-16 구매 완료** (디바이스마트 + 엘레파츠 앰프). 2대분, 예비 없음. 상세는 `docs/parts-list.md`:
  - WeMos LOLIN S3 V1.0.0(16MB/8MB PSRAM) ×2
  - 2.8인치 ILI9341 SPI TFT(SD 슬롯 내장, THL-CD11) ×2
  - MAX98357A(엘레파츠 ShenzenAV) ×2, 3W 4Ω 스피커 ×2
  - DS3231 모듈(ZS-042 호환, 충전 저항 제거) ×2 + CR2032 5알
  - 스누즈 바용 KW-10 마이크로 스위치 ×3, 470uF 콘덴서 ×10
  - 브레드보드 ×2, 듀폰선 3종(테스트용)
- [ ] 따로 준비: microSD 8~32GB ×2, 5V 2A 어댑터 ×2, USB-A to C 데이터 케이블 ×2
- [ ] 장비: 인두와 멀티미터(빌림, 부품 도착 무렵), 로직 분석기(구매함). 캘리퍼스는 빼기로 함
- [x] GitHub 공개 저장소 생성: https://github.com/UncleSamsun/usagi-clock (2026-11-16)

## Global Constraints

- 언어: C11. `alarm_core`, `button` 컴포넌트에서 `esp_*`, `freertos/*`, `driver/*` 헤더 include 금지. 순수 C여야 한다.
- 시간대: KST 고정, `ALARM_TZ_OFFSET_S = 9 * 3600`, 서머타임 없음
- 알람 최대 개수: `ALARM_MAX = 32`
- 기본값: 스누즈 5분, 길게 누르기 2000ms, 자동 정지 10분, 미리 알림 5분
- 미리 알림 범위: 0~60분. 볼륨 범위: 0~100
- 시간 건너뜀 허용 폭: 120초. 그보다 크게 앞으로 건너뛰면 그 사이 알람은 울리지 않는다.
- 알람 시각에 전원이 꺼져 있었으면 울리지 않는다(부팅 후 따라잡기 없음).
- 저장소에 캐릭터 이미지/음성 파일 커밋 금지. `.gitignore`에 `assets/private/`, `sdcard/` 포함.
- 파티션 테이블은 처음부터 OTA 슬롯 2개(`ota_0`, `ota_1`) + `otadata` + `storage`(LittleFS)를 둔다.

## Review Focus

1. 미리 알림 구간 안에서 알람을 만들거나 켜면(6:57에 7:00 알람, 미리 알림 5분) 다음 틱에서 바로 "우! 나!"가 시작돼야 한다. → Task 3 `test_alarm_created_inside_pre_window_starts_pre`
2. 울린 직후 SNTP 보정으로 시간이 뒤로 가면(7:00:10 → 6:59:50) 같은 회차가 두 번 울리면 안 된다. → Task 3 `test_backward_jump_does_not_ring_twice`
3. 미리 알림이 자정과 요일 경계를 넘을 때(월요일 전용 00:02, 미리 알림 5분이면 일요일 23:57에 시작) 정확히 시작해야 한다. → Task 3 `test_pre_window_crosses_midnight_and_weekday`
4. 두 알람이 같은 시각이거나 겹치면 회차는 하나이고, 길게 한 번 누르면 둘 다 끝난다. 둘 다 "한 번만" 알람이면 둘 다 비활성화된다. → Task 4 `test_overlapping_alarms_merge_and_end_together`
5. 울리는 중에 알람 목록이 바뀌어도(해당 알람 삭제 포함) 진행 중인 회차는 유지되고, 길게 누르면 정상 종료된다. → Task 4 `test_session_survives_alarm_list_change`

---

## 파일 구조

```
usagi-clock/
├── CMakeLists.txt                  ESP-IDF 최상위 프로젝트
├── sdkconfig.defaults              PSRAM(옥탈), 플래시 16MB, 커스텀 파티션
├── partitions.csv                  nvs, otadata, phy_init, ota_0, ota_1, storage
├── main/
│   ├── CMakeLists.txt
│   └── main.c                      (계획 1: 부팅 로그만)
├── components/
│   ├── alarm_core/
│   │   ├── CMakeLists.txt          idf_component_register (REQUIRES 없음)
│   │   ├── include/alarm_core.h    공개 타입과 함수 전부
│   │   ├── alarm_time.c            로컬 epoch ↔ 요일, 다음 울림 시각 계산
│   │   ├── alarm_validate.c        알람/설정 값 검증
│   │   └── alarm_engine.c          틱 판단 + 회차 상태 머신
│   └── button/
│       ├── CMakeLists.txt
│       ├── include/button_fsm.h
│       └── button_fsm.c            디바운스, 짧게/길게 판별 (GPIO는 계획 2)
├── test/host/
│   ├── CMakeLists.txt              FetchContent로 Unity, 커버리지 옵션
│   ├── test_alarm_time.c
│   ├── test_alarm_engine_tick.c
│   ├── test_alarm_engine_session.c
│   └── test_button_fsm.c
├── tools/run_host_tests.sh         빌드 + ctest + gcovr + cppcheck 한 번에
├── .github/workflows/ci.yml
├── .gitignore
└── README.md
```

---

### Task 1: 개발 환경과 저장소 뼈대

**작성:** Claude가 작성하고 사용자에게 구조를 설명한다. 환경 설치는 사용자 PC에서 같이 진행한다.

**Files:**
- Create: 위 트리의 `CMakeLists.txt`, `sdkconfig.defaults`, `partitions.csv`, `main/*`, `components/alarm_core/CMakeLists.txt`, `components/alarm_core/include/alarm_core.h`(빈 선언), `components/button/CMakeLists.txt`, `test/host/CMakeLists.txt`, `test/host/test_alarm_time.c`(스모크 테스트 1개), `tools/run_host_tests.sh`, `.github/workflows/ci.yml`, `.gitignore`, `README.md`

**Interfaces:**
- Produces: `tools/run_host_tests.sh` — 인자 없이 실행. `build-host/`에 Debug + `--coverage`로 빌드하고 `ctest --output-on-failure`, `gcovr --fail-under-line 90 -r . --filter components/`, `cppcheck --enable=warning,style,performance --error-exitcode=1 components/alarm_core components/button`을 순서대로 실행한다. 하나라도 실패하면 0이 아닌 값으로 끝난다.
- Produces: `test/host/CMakeLists.txt`는 테스트 파일마다 실행 파일 하나를 만들고 `add_test`로 등록한다. 대상 소스는 `components/alarm_core/*.c`, `components/button/*.c`를 직접 포함한다.

- [x] **Step 1: ESP-IDF 설치(Windows)**
  ESP-IDF Windows 설치 프로그램 또는 VS Code ESP-IDF 확장으로 최신 안정판을 설치한다. 설치한 버전 문자열을 README의 "개발 환경" 절에 적는다.
  Run: `idf.py --version`
  Expected: `ESP-IDF v5.5` 이상 출력

- [x] **Step 2: PC 테스트 환경 설치(WSL2 Ubuntu)**
  Run: `sudo apt install -y build-essential cmake ninja-build gcovr cppcheck`
  Expected: `gcc --version`, `cmake --version`, `gcovr --version`, `cppcheck --version`이 모두 출력됨

- [x] **Step 3: 저장소 초기화와 ESP-IDF 뼈대 작성**
  `partitions.csv`(16MB 기준):

  ```
  # Name,   Type, SubType, Offset,  Size
  nvs,      data, nvs,     0x9000,  0x6000
  otadata,  data, ota,     0xf000,  0x2000
  phy_init, data, phy,     0x11000, 0x1000
  ota_0,    app,  ota_0,   0x20000, 0x400000
  ota_1,    app,  ota_1,   ,        0x400000
  storage,  data, spiffs,  ,        0x200000
  ```
  `sdkconfig.defaults`: `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`, `CONFIG_PARTITION_TABLE_CUSTOM=y`, `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"`, `CONFIG_SPIRAM=y`, `CONFIG_SPIRAM_MODE_OCT=y`, `CONFIG_IDF_TARGET="esp32s3"`.
  `main.c`는 `app_main`에서 `ESP_LOGI("usagi", "boot")`만 한다.

- [x] **Step 4: 펌웨어 빌드 확인**
  Run: `idf.py set-target esp32s3 && idf.py build`
  Expected: `Project build complete` 출력

- [x] **Step 5: PC 테스트 뼈대와 스모크 테스트**
  `test_alarm_time.c`에 `test_smoke`(`TEST_ASSERT_EQUAL_INT(9 * 3600, ALARM_TZ_OFFSET_S)`) 하나만 둔다. 이 상수는 `alarm_core.h`에 미리 정의한다.
  Run(WSL): `./tools/run_host_tests.sh`
  Expected: `100% tests passed`. 커버리지 단계는 아직 측정 대상 코드가 없으므로 이 Task에서만 `--fail-under-line`을 건너뛰는 `SKIP_COVERAGE_GATE=1` 환경 변수를 지원한다.

- [x] **Step 6: CI 작성**
  `.github/workflows/ci.yml`에 job 두 개를 둔다.
  - `host-tests`: ubuntu-latest, Step 2 패키지 설치 → `./tools/run_host_tests.sh` → gcovr HTML을 artifact로 업로드
  - `firmware`: `espressif/idf:<Step 1에서 고정한 버전 태그>` 컨테이너 → `idf.py set-target esp32s3 build`

- [x] **Step 7: Commit + push, CI 통과 확인**

  ```bash
  git add -A
  git commit -m "chore: ESP-IDF 프로젝트 뼈대, PC 테스트, CI"
  git push -u origin main
  ```
  Expected: GitHub Actions 두 job 모두 녹색

---

### Task 2: 시간 계산 — 요일과 다음 울림 시각

**작성:** 사용자(Claude 설명과 리뷰)

**Files:**
- Modify: `components/alarm_core/include/alarm_core.h`
- Create: `components/alarm_core/alarm_time.c`, `components/alarm_core/alarm_validate.c`
- Test: `test/host/test_alarm_time.c`

**Interfaces:**
- Produces (`alarm_core.h`):

```c
#define ALARM_TZ_OFFSET_S   (9 * 3600)
#define ALARM_MAX           32
#define ALARM_JUMP_LIMIT_S  120

/* days_mask: bit0=월 ... bit6=일, 0 = 한 번만 */
#define ALARM_DAY_MON  (1u << 0)
#define ALARM_DAY_SUN  (1u << 6)
#define ALARM_DAYS_WEEKDAYS 0x1Fu
#define ALARM_DAYS_EVERYDAY 0x7Fu

typedef struct {
    uint8_t id;            /* 1..255, 0은 미사용 */
    bool    enabled;
    uint8_t hour;          /* 0..23 (UI가 12시간제로 변환) */
    uint8_t minute;        /* 0..59 */
    uint8_t days_mask;
    uint8_t pre_minutes;   /* 0..60 */
    uint8_t pre_volume;    /* 0..100 */
    uint8_t main_volume;   /* 0..100 */
    char    label[24];
    char    clipset[32];   /* SD /clips/ 아래 세트 이름 */
    char    song[64];      /* SD /sounds/ 아래 파일 이름 */
} alarm_t;

typedef struct {
    uint16_t snooze_min;     /* 기본 5 */
    uint16_t long_press_ms;  /* 기본 2000 */
    uint16_t auto_stop_min;  /* 기본 10 */
} alarm_settings_t;

int64_t alarm_local_from_utc(int64_t utc_s);
int     alarm_weekday(int64_t local_s);            /* 0=월 .. 6=일 */
int64_t alarm_next_ring(const alarm_t *a, int64_t after_local_s); /* after보다 큰 가장 이른 울림 시각, 비활성이면 -1 */
bool    alarm_validate(const alarm_t *a);
alarm_settings_t alarm_settings_default(void);
```

- [x] **Step 1: 실패하는 테스트 작성** (`test_alarm_time.c`)
  상수: `MON_1116_0000 = 1794787200`(2026-11-16 월 00:00 로컬 epoch), `MON_0600 = 1794808800`, `MON_0700 = 1794812400`, `TUE_1117_0700 = 1794898800`, `FRI_1120_0800 = 1795161600`, `MON_1123_0700 = 1795417200`

  ```c
  void test_weekday_epoch_is_thursday(void)   { TEST_ASSERT_EQUAL_INT(3, alarm_weekday(0)); }
  void test_weekday_2026_11_16_is_monday(void){ TEST_ASSERT_EQUAL_INT(0, alarm_weekday(MON_1116_0000)); }
  void test_local_from_utc_adds_9h(void)      { TEST_ASSERT_EQUAL_INT64(MON_0700, alarm_local_from_utc(MON_0700 - 9*3600)); }
  void test_daily_same_day(void)        /* everyday 07:00, after MON_0600 → MON_0700 */
  void test_strictly_after(void)        /* everyday 07:00, after MON_0700 → TUE_1117_0700 */
  void test_weekdays_skip_weekend(void) /* weekdays 07:00, after FRI_1120_0800 → MON_1123_0700 */
  void test_once_rolls_to_tomorrow(void)/* days_mask 0, 07:00, after MON_0700+3600 → TUE_1117_0700 */
  void test_disabled_returns_minus1(void)
  void test_validate_rejects(void)      /* hour 24, minute 60, pre 61, volume 101, days_mask 0x80 각각 false */
  void test_settings_default(void)      /* 5, 2000, 10 */
  ```

- [x] **Step 2: 실행해서 실패 확인**
  Run: `./tools/run_host_tests.sh`
  Expected: 링크 오류(`undefined reference to alarm_weekday`) 또는 FAIL

- [x] **Step 3: 구현**
  요일은 `floor(local_s / 86400)`에서 `(days + 3) % 7`로 구한다(1970-01-01이 목요일). `alarm_next_ring`은 `after`가 속한 날의 hh:mm부터 최대 8일을 앞으로 보며 `days_mask`(0이면 아무 요일)와 `> after`를 만족하는 첫 시각을 반환한다.

- [x] **Step 4: 통과 확인**
  Run: `./tools/run_host_tests.sh`
  Expected: 모든 테스트 PASS, `alarm_time.c` 줄 커버리지 90% 이상

- [x] **Step 5: Commit**

  ```bash
  git add components/alarm_core test/host/test_alarm_time.c
  git commit -m "feat(alarm_core): 요일·다음 울림 시각 계산과 값 검증"
  ```

---

### Task 3: 알람 엔진 — 틱 판단 (미리 알림 시작, 울림, 시간 건너뜀)

**작성:** 사용자(Claude 설명과 리뷰)

**Files:**
- Modify: `components/alarm_core/include/alarm_core.h`
- Create: `components/alarm_core/alarm_engine.c`
- Test: `test/host/test_alarm_engine_tick.c`

**Interfaces:**
- Consumes: Task 2 전부
- Produces:

```c
typedef enum {
    ALARM_STATE_IDLE = 0,
    ALARM_STATE_PRE,         /* "우! 나!" 재생 중 */
    ALARM_STATE_PRE_MUTED,   /* 미리 알림 음소거, 정각 대기 */
    ALARM_STATE_RINGING,     /* 본 알람 */
    ALARM_STATE_SNOOZED,
} alarm_state_t;

/* 출력 동작 비트 */
#define ALARM_ACT_PRE_CLIP     (1u << 0)  /* 이번 틱에 미리 알림 클립 1개 재생 (pre_clip_seq 번째) */
#define ALARM_ACT_MAIN_START   (1u << 1)  /* 본 알람 노래 재생 시작(반복) */
#define ALARM_ACT_AUDIO_STOP   (1u << 2)
#define ALARM_ACT_CONFIRM_BEEP (1u << 3)  /* 길게 누르기 확인음 */
#define ALARM_ACT_DISABLE_ONCE (1u << 4)  /* disable_ids 의 알람을 비활성화해 저장할 것 */

typedef struct {
    uint32_t actions;
    uint8_t  alarm_id;                   /* 회차의 대표 알람(노래·볼륨 조회용) */
    uint16_t pre_clip_seq;               /* 0부터 증가 */
    uint8_t  disable_ids[ALARM_MAX];
    uint8_t  n_disable;
} alarm_output_t;

typedef struct {
    alarm_state_t state;
    uint8_t  alarm_id;
    int64_t  ring_at;        /* 대표 알람의 정각(로컬 epoch) */
    int64_t  snooze_until;   /* SNOOZED일 때만 */
} alarm_view_t;

typedef enum { ALARM_INPUT_SHORT = 1, ALARM_INPUT_LONG = 2 } alarm_input_t;

typedef struct { /* 내부 필드는 구현자가 정한다. 동적 할당 금지 */ ... } alarm_engine_t;

void           alarm_engine_init(alarm_engine_t *e, const alarm_settings_t *s);
void           alarm_engine_set_alarms(alarm_engine_t *e, const alarm_t *alarms, size_t n);
alarm_output_t alarm_engine_tick(alarm_engine_t *e, int64_t now_local_s);   /* 1초마다 호출 */
alarm_output_t alarm_engine_input(alarm_engine_t *e, alarm_input_t in, int64_t now_local_s);
alarm_view_t   alarm_engine_view(const alarm_engine_t *e);
```

**틱 판단 규칙** (시그니처만으로 정해지지 않으므로 명시):
- 엔진은 `last_tick`을 기억한다. 첫 틱에서는 `last_tick = now - 1`로 둔다(부팅 후 따라잡기 없음).
- 각 활성 알람에 대해 `T = alarm_next_ring(a, last_tick)`를 구한다. 이미 처리했거나 진행 중인 회차(알람별 `handled_ring_at`)와 같은 `T`는 무시한다.
- **울림:** `T <= now`이고 `0 < now - last_tick <= ALARM_JUMP_LIMIT_S`이면 RINGING으로 가고 `MAIN_START`를 낸다.
- **미리 알림:** `T > now`이고 `pre_minutes > 0`이고 `T - pre_minutes*60 <= now`이면 PRE로 간다. 이 조건은 "구간 안에 있는지"로 판단하므로 구간 안에서 알람을 만들어도 바로 시작된다.
- PRE 상태에서는 틱마다 `PRE_CLIP`과 증가하는 `pre_clip_seq`를 낸다. `now >= ring_at`이면 RINGING으로 가고 `MAIN_START`를 낸다.
- 틱이 끝나면 `last_tick = now`. 시간이 뒤로 가도(`now < last_tick`) 그대로 갱신한다. 이중 울림은 `handled_ring_at`이 막는다.

- [ ] **Step 1: 실패하는 테스트 작성** (`test_alarm_engine_tick.c`)
  상수는 Task 2와 같고, 추가로 `SUN_1122_2357 = 1795391820`, `MON_1123_0002 = 1795392120`이 있다. 헬퍼 `alarm_t mk(uint8_t id, int h, int m, uint8_t days, uint8_t pre)`(볼륨 50, 활성)를 둔다.

  ```c
  void test_idle_before_pre_window(void)
  /* everyday 07:00 pre 5. tick(MON_0700 - 301) → actions 0, state IDLE */

  void test_pre_starts_at_window_and_emits_clips(void)
  /* 같은 알람. tick(MON_0700-301), tick(MON_0700-300) → PRE_CLIP, seq 0, state PRE;
     tick(MON_0700-299) → PRE_CLIP, seq 1 */

  void test_rings_at_exact_time(void)
  /* PRE 상태에서 tick(MON_0700) → MAIN_START, state RINGING, view.ring_at == MON_0700 */

  void test_pre_zero_rings_without_pre(void)
  /* pre 0. tick(MON_0700-1), tick(MON_0700) → MAIN_START, PRE_CLIP 없음 */

  void test_small_forward_jump_still_rings(void)
  /* pre 0. tick(MON_0700-30), tick(MON_0700+60) → MAIN_START */

  void test_large_forward_jump_skips(void)
  /* pre 0. tick(MON_0700-30), tick(MON_0700+121) → actions 0, state IDLE */

  void test_first_tick_after_alarm_time_does_not_ring(void)
  /* 첫 틱이 MON_0700+5 → actions 0 (부팅 후 따라잡기 없음) */

  void test_alarm_created_inside_pre_window_starts_pre(void)    /* Review Focus 1 */
  /* 빈 목록으로 tick(MON_0700-180); set_alarms(07:00 pre 5); tick(MON_0700-179) → PRE_CLIP, state PRE */

  void test_backward_jump_does_not_ring_twice(void)             /* Review Focus 2 */
  /* pre 0. tick(MON_0700-1), tick(MON_0700) → MAIN_START; input LONG; tick(MON_0700+10);
     tick(MON_0700-10) (뒤로 보정); tick(MON_0700-9) ... tick(MON_0700) → MAIN_START 없음 */

  void test_pre_window_crosses_midnight_and_weekday(void)       /* Review Focus 3 */
  /* days ALARM_DAY_MON, 00:02, pre 5. tick(SUN_1122_2357-1) → 0; tick(SUN_1122_2357) → PRE_CLIP;
     … tick(MON_1123_0002) → MAIN_START */

  void test_disabled_alarm_never_fires(void)
  ```

- [ ] **Step 2: 실행해서 실패 확인**
  Run: `./tools/run_host_tests.sh`
  Expected: 링크 오류 또는 FAIL

- [ ] **Step 3: 위 "틱 판단 규칙"대로 `alarm_engine_init/set_alarms/tick/view` 구현**
  `alarm_engine_t`는 알람 배열 사본(`alarm_t alarms[ALARM_MAX]`)과 알람별 `handled_ring_at`을 고정 크기로 가진다. `set_alarms`는 id가 같은 알람의 `handled_ring_at`을 보존한다.

- [ ] **Step 4: 통과 확인**
  Run: `./tools/run_host_tests.sh`
  Expected: 전부 PASS

- [ ] **Step 5: Commit**

  ```bash
  git add components/alarm_core test/host/test_alarm_engine_tick.c
  git commit -m "feat(alarm_core): 틱 판단 — 미리 알림 시작, 울림, 시간 건너뜀 처리"
  ```

---

### Task 4: 알람 엔진 — 버튼 입력과 회차 상태 머신

**작성:** 사용자(Claude 설명과 리뷰)

**Files:**
- Modify: `components/alarm_core/alarm_engine.c`
- Test: `test/host/test_alarm_engine_session.c`

**Interfaces:**
- Consumes: Task 3의 `alarm_engine_input`, `alarm_output_t`, `alarm_view_t`
- Produces: 계획 2가 FreeRTOS 작업에서 그대로 호출할 완성된 엔진

**상태 전이** (설계 문서 5.3):

| 현재 | 입력/조건 | 다음 | actions |
|---|---|---|---|
| PRE | SHORT | PRE_MUTED | AUDIO_STOP |
| PRE, PRE_MUTED | 정각 도달(틱) | RINGING | MAIN_START |
| RINGING | SHORT | SNOOZED (`snooze_until = now + snooze_min*60`) | AUDIO_STOP |
| SNOOZED | 틱 `now >= snooze_until` | RINGING (연속 울림 시간 다시 시작) | MAIN_START |
| RINGING | 틱: 마지막 MAIN_START 이후 `auto_stop_min*60`초 경과 | IDLE (회차 종료) | AUDIO_STOP (+DISABLE_ONCE) |
| PRE, PRE_MUTED, RINGING, SNOOZED | LONG | IDLE (회차 종료) | AUDIO_STOP \| CONFIRM_BEEP (+DISABLE_ONCE) |
| IDLE | SHORT / LONG | IDLE | 0 |

- **회차 종료:** 회차에 속한 모든 알람의 `handled_ring_at`을 기록한다. 그중 `days_mask == 0`인 알람을 엔진 내부에서 비활성화하고 `disable_ids`에 담는다.
- **합치기:** 회차가 진행 중(IDLE 아님)일 때 다른 알람이 미리 알림이나 울림 조건을 만족하면 그 알람을 회차 구성원으로 추가만 한다. 상태와 소리는 바꾸지 않는다.
- **목록 변경:** 진행 중에 `set_alarms`가 와도 회차 상태, 대표 `alarm_id`, 구성원을 유지한다. 삭제된 알람은 종료 때 `disable_ids`에 넣지 않는다.

- [ ] **Step 1: 실패하는 테스트 작성**

  ```c
  void test_short_in_pre_mutes_but_rings_on_time(void)
  /* PRE 중 SHORT → AUDIO_STOP, state PRE_MUTED; 이후 틱 PRE_CLIP 없음; tick(ring_at) → MAIN_START */

  void test_short_in_ringing_snoozes_5min(void)
  /* RINGING(MON_0700) 중 tick(MON_0700+10), SHORT → AUDIO_STOP, state SNOOZED, view.snooze_until == MON_0700+10+300 */

  void test_snooze_returns_to_ringing_without_pre(void)
  /* 위에서 tick(MON_0700+309) → 0; tick(MON_0700+310) → MAIN_START (PRE_CLIP 없음) */

  void test_long_ends_session_with_beep(void)
  /* RINGING 중 LONG → AUDIO_STOP|CONFIRM_BEEP, state IDLE */

  void test_long_in_pre_cancels_occurrence(void)
  /* PRE 중 LONG → IDLE; tick(ring_at) → MAIN_START 없음; 다음 날 같은 시각에는 다시 PRE 시작 */

  void test_auto_stop_after_10min_continuous(void)
  /* MAIN_START at MON_0700; tick(MON_0700+599) → 0; tick(MON_0700+600) → AUDIO_STOP, state IDLE */

  void test_snooze_resets_auto_stop_timer(void)
  /* MON_0700 울림 → +590 SHORT → +890 MAIN_START → tick(+890+599) 여전히 RINGING; tick(+890+600) → IDLE */

  void test_once_alarm_disabled_after_session(void)
  /* days 0 알람, 울림 → LONG → DISABLE_ONCE, n_disable 1, disable_ids[0] == id; 다음 날 울리지 않음 */

  void test_repeating_alarm_not_disabled(void)
  /* everyday 알람 LONG 종료 → DISABLE_ONCE 비트 없음 */

  void test_overlapping_alarms_merge_and_end_together(void)     /* Review Focus 4 */
  /* id1 07:00 pre 5 once, id2 07:02 pre 5 once. 06:55 PRE(id1) → 06:57 id2 조건 만족 → state·alarm_id 그대로(1);
     07:00 MAIN_START → LONG → DISABLE_ONCE, n_disable 2 (1,2); 07:02에 MAIN_START 없음 */

  void test_session_survives_alarm_list_change(void)            /* Review Focus 5 */
  /* id1 RINGING 중 set_alarms(빈 목록) → state RINGING, alarm_id 1 유지; LONG → IDLE, n_disable 0 */

  void test_input_in_idle_does_nothing(void)
  ```

- [ ] **Step 2: 실행해서 실패 확인**
  Run: `./tools/run_host_tests.sh`
  Expected: 새 테스트 FAIL

- [ ] **Step 3: 위 상태 전이 표대로 `alarm_engine_input`과 틱의 SNOOZED/자동 정지 처리 구현**
  회차 구성원은 `uint8_t members[ALARM_MAX]` 고정 배열로 둔다.

- [ ] **Step 4: 통과 확인**
  Run: `./tools/run_host_tests.sh`
  Expected: 전부 PASS, `alarm_core` 줄 커버리지 90% 이상, cppcheck 경고 0

- [ ] **Step 5: Commit**

  ```bash
  git add components/alarm_core test/host/test_alarm_engine_session.c
  git commit -m "feat(alarm_core): 버튼 입력과 회차 상태 머신(스누즈·자동 정지·합치기)"
  ```

---

### Task 5: 버튼 판별 (디바운스, 짧게/길게)

**작성:** 사용자(Claude 설명과 리뷰)

**Files:**
- Create: `components/button/include/button_fsm.h`, `components/button/button_fsm.c`
- Test: `test/host/test_button_fsm.c`

**Interfaces:**
- Produces:

```c
typedef enum { BUTTON_EV_NONE = 0, BUTTON_EV_SHORT, BUTTON_EV_LONG } button_event_t;

typedef struct { /* 구현자가 정함, 동적 할당 금지 */ ... } button_fsm_t;

void           button_fsm_init(button_fsm_t *b, uint32_t debounce_ms, uint32_t long_ms);
button_event_t button_fsm_update(button_fsm_t *b, bool raw_pressed, uint32_t now_ms); /* 10ms마다 호출 */
```
- 계획 2에서 `main`이 `BUTTON_EV_SHORT → ALARM_INPUT_SHORT`, `BUTTON_EV_LONG → ALARM_INPUT_LONG`으로 매핑한다. 그래서 `button`과 `alarm_core`는 서로 의존하지 않는다.
- 값: `debounce_ms = 30`, `long_ms`는 `alarm_settings_t.long_press_ms`(기본 2000)

**판별 규칙:** 원시 입력이 `debounce_ms` 동안 같은 값을 유지해야 상태가 바뀐다. 눌림이 확정된 뒤 `long_ms`에 도달하는 순간 **손을 떼기 전에** LONG을 한 번만 낸다. LONG 없이 떼면 SHORT를 낸다. LONG 뒤에 떼면 아무것도 내지 않는다. `now_ms`는 `uint32_t` 랩어라운드에도 동작해야 한다(차이 계산).

- [ ] **Step 1: 실패하는 테스트 작성** (헬퍼 `feed(b, pressed, from_ms, to_ms)`: 10ms 간격으로 update하고 마지막 NONE이 아닌 이벤트와 개수를 반환)

  ```c
  void test_short_press(void)              /* 0~200ms 누름, 210ms 뗌 → SHORT 1개 */
  void test_bounce_is_ignored(void)        /* 10ms 간격 눌림/뗌 5회 반복(각 20ms 미만) → 이벤트 0 */
  void test_long_fires_while_held(void)    /* 누른 채 2030ms 시점까지 → LONG 1개 (떼기 전) */
  void test_long_fires_once(void)          /* 5000ms 계속 누름 → LONG 정확히 1개 */
  void test_release_after_long_is_silent(void) /* LONG 뒤 뗌 → 추가 이벤트 0 */
  void test_just_under_long_is_short(void) /* 1900ms 누름 뒤 뗌 → SHORT */
  void test_millis_wraparound(void)        /* now_ms를 0xFFFFFF00부터 시작해 2100ms 누름 → LONG 1개 */
  void test_pressed_at_init_needs_release_first(void) /* 처음부터 눌린 채 시작(부팅 시 누름) → 뗄 때까지 이벤트 0 */
  ```
  마지막 테스트는 "버튼을 누른 채 부팅"(계획 4의 설정 모드 진입)이 알람 입력으로 새지 않게 막는다.

- [ ] **Step 2: 실행해서 실패 확인**
  Run: `./tools/run_host_tests.sh`
  Expected: 링크 오류 또는 FAIL

- [ ] **Step 3: 위 판별 규칙대로 구현**

- [ ] **Step 4: 통과 확인**
  Run: `./tools/run_host_tests.sh`
  Expected: 전부 PASS, `button` 줄 커버리지 90% 이상

- [ ] **Step 5: 펌웨어 빌드에 두 컴포넌트가 들어가는지 확인**
  Run: `idf.py build`
  Expected: `Project build complete`. `alarm_core`와 `button`이 컴파일 목록에 있음

- [ ] **Step 6: Commit + push, CI 녹색 확인**

  ```bash
  git add components/button test/host/test_button_fsm.c
  git commit -m "feat(button): 디바운스와 짧게/길게 누르기 판별"
  git push
  ```

---

## 계획 1 완료 기준

- PC 테스트 전부 통과, `alarm_core`/`button` 줄 커버리지 90% 이상, cppcheck 경고 0
- `idf.py build` 성공, GitHub Actions 녹색
- 설계 문서 5.1(동작 중 시간 보정, 꺼져 있던 동안 알람 없음), 5.2(필드·범위), 5.3(상태 머신 전체)이 테스트로 고정됨
- 다음 단계: 부품이 도착하면 핀 배정을 실물로 확정하고 **계획 2(하드웨어 기동)**를 작성한다.
