# 우사기 알람시계 — 계획 4: 네트워크와 웹 설정 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **실행 방식 [사용자 결정, 2026-11-19]:** 코드·테스트·웹 페이지는 Claude가 작성한다. 사용자는 집 Wi-Fi 정보 입력, 아이폰 사파리에서 실사용 확인, 실기 시험을 한다. Wi-Fi 비밀번호는 사용자가 직접 입력하고 저장소·대화·로그에 남기지 않는다.
>
> **표기:** **[실물 확인]** 실기에서 확인 후 고친다. **[빌드 확인]** 처음 빌드할 때 ESP-IDF v6.1에서 이름이나 위치를 확인한다. **[제안]** Claude가 정한 것으로 바꿀 수 있다.

**Goal:** 아이폰 사파리로 `http://<기기이름>.local`에 접속해 알람·소리·설정을 바꿀 수 있고, 설정은 전원이 꺼져도 남으며, 시간은 SNTP로 맞춰지고, 밤에는 화면이 어두워진다. 뒤쪽 Task에서 소리 업로드, 설정 모드(캡티브 포털), 웹 펌웨어 업데이트를 더한다.

**Architecture:** 웹 요청 처리와 저장은 "검사·변환은 순수 C, 입출력은 `*_esp.c`" 규칙을 그대로 따른다. JSON ↔ 구조체 변환과 값 검사, 저장 형식(버전·CRC), Wi-Fi 모드 결정, 야간 밝기, DNS 응답 만들기를 PC에서 테스트한다. 웹 페이지는 프레임워크 없는 HTML/CSS/JS 한 장이고 LittleFS 파티션에 들어간다. 알람은 네트워크와 무관하게 계속 돈다(설계 5.5 "설정 모드 동안에도 알람은 계속 동작").

**Tech Stack:** ESP-IDF v6.1, `esp_wifi`, `esp_netif`, `esp_netif_sntp`, `espressif/mdns`, `esp_http_server`, cJSON, `joltwallet/littlefs`, NVS, `esp_ota_ops`(웹에서 올린 파일로 업데이트), lwIP 소켓(DNS), 바닐라 HTML/CSS/JS, Unity·gcovr·cppcheck

**Spec:** `docs/specs/2026-11-16-usagi-alarm-clock-design.md` (5.1 시간, 5.2 알람 항목, 5.4 야간 밝기, 5.5 웹 설정과 API, 5.6 저장, 7장 OTA, 8장 1차 필수와 2차 후보)
**이전 계획:** 계획 2(하드웨어 기동), 계획 3(오디오). 이 계획은 계획 3 완료 후 실행한다.

> **실행 메모 (2026-11-19, 부품 도착 전 코드 선작성)**
> - Part A·B 코드, 웹 페이지, PC 테스트, 펌웨어 빌드까지 끝났다. 남은 것은 **[실기]** Step(집 Wi-Fi 연결, 아이폰 확인, 설정 모드, OTA·롤백 시험)이다.
> - 계획과 달라진 점
>   - ESP-IDF v6.1에는 `json` 컴포넌트가 없어 `espressif/cjson`을 쓴다. PC 테스트는 cJSON v1.7.18 원본을 받아 빌드한다.
>   - Wi-Fi 정책: 설정 모드(AP)에 접속한 휴대폰에서 Wi-Fi를 저장하면, **집 Wi-Fi에 연결될 때까지 AP를 유지**한다(바로 내리면 휴대폰이 결과를 못 본다). `ap_until_connected` 필드와 테스트를 더했다.
>   - SNTP 시간은 ESP-IDF의 약한 심볼 `sntp_sync_time`을 대신해 `timekeeper_set_utc`(범위 검사·RTC 기록)를 거쳐서만 반영한다. 2000년 같은 이상한 시각은 무시한다.
>   - 업로드·삭제 경로의 파일 이름은 URL 디코드(`web_url_decode`, PC 테스트 포함) 후 규칙 검사를 거친다.
>   - 업로드 파일 이름 정리 순서: 금지 문자 바꾸기 → 앞뒤 공백 제거 → 확장자 떼기 → 본문 앞뒤 공백·점 제거 → 63바이트로 자르기. 그래서 `".mp3"`만 있는 이름은 거절된다.
>   - 캡티브 포털 리다이렉트는 Wi-Fi 모드가 STA가 아닐 때만 켠다(알람 작업이 1초마다 화면 상태를 만들 때 함께 갱신).
>   - 펌웨어 파일은 `build/usagi_clock.bin`(밑줄)이다.

## 범위와 순서

설계 8장의 우선순위를 그대로 따른다. **Part A**는 1차 필수라 빼지 않는다. **Part B**는 일정이 밀리면 2차로 넘길 후보이고, 넘길 때는 아래 대안을 쓴다.

| Part | Task | 내용 | 밀릴 때 대안 |
|---|---|---|---|
| A | 1 | 설정·알람 저장(NVS), 야간 밝기 | — |
| A | 2 | Wi-Fi(STA), SNTP, mDNS, 화면 Wi-Fi 표시 | — |
| A | 3 | REST API(알람·설정·상태·시간·Wi-Fi) | — |
| A | 4 | 웹 페이지(아이폰 사파리) | — |
| B | 5 | 소리 업로드·삭제·미리 듣기 | SD카드를 PC에 꽂아 복사 |
| B | 6 | 설정 모드(AP + 캡티브 포털), 공장 초기화 | Wi-Fi 정보를 콘솔 명령으로 입력 |
| B | 7 | 웹 펌웨어 업데이트(OTA)와 롤백 | 건네기 전에 USB로 최종 펌웨어 기록 |

## 준비 작업

- [ ] 집 공유기가 2.4GHz를 쓰는지 확인한다(ESP32-S3는 5GHz 불가). 공유기의 "AP 격리(클라이언트 격리)"가 꺼져 있어야 휴대폰에서 시계에 접속된다.
- [ ] 아이폰 사파리에서 `.local` 주소 접속이 되는지는 Task 2에서 확인한다(iOS는 mDNS를 기본 지원).

## Global Constraints

- 계획 2·3의 Global Constraints를 모두 따른다.
- **외부 인터넷 접속 기능은 만들지 않는다**(설계 1장 비목표). 시계는 같은 Wi-Fi 안에서만 접근한다. SNTP만 바깥으로 나간다.
- 웹 인증은 MVP에서 없다(설계 5.5). 그래서 모든 쓰기 API는 입력을 엄격히 검사하고, 파일 경로는 계획 3의 `audio_name_ok`를 거친다.
- NVS 네임스페이스: `usagi`(설정, 알람, Wi-Fi), `factory`(AP 비밀번호, 공장 초기화에도 지우지 않음).
- 저장 형식은 버전과 CRC32를 가진다. 읽을 수 없는 값은 버리고 기본값으로 시작한다(부팅이 멈추면 안 된다).
- 설정 값 범위 **[제안]**: 스누즈 1~30분, 길게 누르기 1000~5000ms, 자동 정지 1~60분, 기본 볼륨 0~100, 낮·밤 밝기 1~100%, 야간 시작·끝 0~1439분(자정부터 분), 기기 이름은 `a-z`·`0-9`·`-` 1~24자이고 `-`로 시작하거나 끝나지 않는다.
- 기본값: 기본 볼륨 70, 낮 밝기 80, 밤 밝기 10, 야간 23:00(1380)~07:00(420), 기기 이름 `usagi-<MAC 끝 2바이트 hex>`(설계 5.4, 5.5).
- HTTP 요청 본문: JSON API는 4KB 이하, 업로드는 파일당 20MB 이하(설계 5.5). 넘으면 413.
- API 오류 응답은 `{"error":"<사람이 읽을 영어 한 줄>"}`과 4xx 코드. 성공 응답은 바뀐 대상의 JSON.
- 웹 페이지 파일은 `web/` 폴더에 두고 빌드할 때 LittleFS 이미지로 만든다. 외부 CDN을 쓰지 않는다(시계가 인터넷 없이도 페이지를 보여야 한다).
- 저작물 금지(이전 계획과 같음). 웹 아이콘은 단순 도형 SVG로 Claude가 만든다.

## Review Focus

1. 설정을 저장하는 순간 전원이 끊기거나 저장 내용이 깨져도 알람 목록이 엉뚱한 값으로 살아나면 안 된다. 깨진 저장값은 버리고 그 사실을 로그로 남긴다. → Task 1 `test_alarms_decode_rejects_bad_crc`, `test_alarms_decode_rejects_truncated`, `test_alarms_decode_drops_invalid_alarm`
2. 웹에서 범위 밖 값이나 잘못된 JSON(시 24, 이름 30바이트, 숫자 자리에 문자열, 필드 누락)을 보내면 400을 돌려주고 기존 알람은 그대로여야 한다. → Task 3 `test_alarm_json_rejects_out_of_range`, `test_alarm_json_rejects_wrong_type`, `test_alarm_json_label_too_long`, `test_alarm_json_missing_field`
3. 야간 시간대가 자정을 넘을 때(23:00~07:00) 경계 분이 맞아야 하고, 알람이 울리는 동안은 밝기를 낮추지 않아야 한다. → Task 1 `test_night_window_wraps_midnight`, `test_ringing_overrides_night`
4. 집 Wi-Fi가 5분 넘게 끊기면 설정 모드(AP)를 띄우고, 다시 연결되면 AP를 내린다. 그동안 알람은 계속 돈다. → Task 2 `test_sta_failure_5min_enters_ap`, `test_sta_recovers_leaves_ap`
5. 업로드 파일 이름에 FAT 금지 문자, 한글, 아주 긴 이름이 와도 안전한 이름으로 바꾸고, UTF-8 글자 중간에서 자르지 않는다. → Task 5 `test_sanitize_replaces_forbidden`, `test_sanitize_truncates_on_utf8_boundary`

---

## 파일 구조

```
components/
├── app_config/                     순수 (Task 1)
│   ├── include/app_config.h        app_settings_t, 기본값, 검사, 야간 밝기
│   ├── app_config.c
│   ├── include/store_codec.h       알람·설정 저장 형식(버전, CRC32)
│   ├── store_codec.c
│   └── storage_esp.c               NVS 읽기·쓰기 (Task 1)
├── net/
│   ├── include/net.h
│   ├── net_policy.c                순수: STA/AP 결정 (Task 2)
│   ├── dns_reply.c                 순수: 캡티브 포털 DNS 응답 (Task 6)
│   ├── net_esp.c                   Wi-Fi, SNTP, mDNS (Task 2)
│   └── captive_esp.c               DNS 서버 작업 (Task 6)
├── web_api/
│   ├── include/web_api.h
│   ├── web_json.c                  순수: JSON ↔ 구조체, 알람 id 고르기 (Task 3)
│   ├── upload_name.c               순수: 업로드 파일 이름 정리 (Task 5)
│   ├── web_server_esp.c            esp_http_server 라우팅, 정적 파일 (Task 3, 4)
│   ├── web_sounds_esp.c            업로드·삭제·미리 듣기 (Task 5)
│   └── web_ota_esp.c               펌웨어 업데이트 (Task 7)
└── audio/ …                        AUDIO_OP_PREVIEW 추가 (Task 5)
web/
├── index.html  app.js  style.css   단일 페이지 (Task 4)
├── manifest.webmanifest  icon.svg  홈 화면 추가
test/host/
├── test_app_config.c  test_store_codec.c  test_net_policy.c
├── test_web_json.c  test_upload_name.c  test_dns_reply.c
partitions.csv                      변경 없음(storage 2MB를 LittleFS로 사용)
```

---

## Part A — 1차 필수

### Task 1: 설정·알람 저장과 야간 밝기

**작성:** Claude.

**Files:**
- Create: `components/app_config/` 아래 위 파일 전부
- Modify: `components/alarm_core/alarm_engine.c`·`include/alarm_core.h`(`alarm_engine_set_settings`), `components/button/button_gpio_esp.c`, `main/alarm_service_esp.c`(DISABLE_ONCE 저장, 기본 볼륨, 설정 변경), `components/display_ui/display_ui_esp.c`(밝기 정책), `main/dev_console_esp.c`, `main/main.c`
- Test: `test/host/test_app_config.c`, `test/host/test_store_codec.c`, `test/host/test_alarm_engine_session.c`

**Interfaces:**
- Produces (`app_config.h`, 순수):

  ```c
  typedef struct {
      alarm_settings_t alarm;        /* 스누즈, 길게 누르기, 자동 정지 (계획 1) */
      uint8_t  default_volume;
      uint8_t  day_brightness;
      uint8_t  night_brightness;
      uint16_t night_start_min;      /* 0..1439 */
      uint16_t night_end_min;        /* 0..1439, start == end 이면 야간 없음 */
      char     device_name[25];      /* NUL 포함 */
  } app_settings_t;

  app_settings_t app_settings_default(const uint8_t mac[6]);   /* device_name = "usagi-" + mac[4], mac[5] hex 소문자 */
  bool    app_settings_validate(const app_settings_t *s);
  bool    app_device_name_ok(const char *name);
  bool    app_night_active(int64_t local_s, uint16_t start_min, uint16_t end_min);
  uint8_t app_brightness(const app_settings_t *s, int64_t local_s, bool ringing);  /* ringing이면 100 */
  ```
- Produces (`store_codec.h`, 순수):

  ```c
  #define STORE_MAGIC_ALARMS   0x41475355u   /* "USGA" */
  #define STORE_MAGIC_SETTINGS 0x53475355u   /* "USGS" */
  #define STORE_VERSION        1
  /* 헤더(16바이트): magic u32, version u16, count u16, payload_len u32, crc32 u32 (리틀 엔디언) + payload */
  #define STORE_ALARMS_MAX_BYTES (16 + ALARM_MAX * sizeof(alarm_t))

  uint32_t store_crc32(const uint8_t *p, size_t n);   /* IEEE 802.3, "123456789" → 0xCBF43926 */
  size_t   store_encode_alarms(const alarm_t *a, size_t n, uint8_t *out, size_t cap);   /* 쓴 바이트, 실패 0 */
  /* 헤더·CRC·길이가 맞지 않으면 false. 알람 중 alarm_validate 실패는 빼고 *dropped에 개수 */
  bool     store_decode_alarms(const uint8_t *in, size_t len, alarm_t *out, size_t *n, size_t *dropped);
  size_t   store_encode_settings(const app_settings_t *s, uint8_t *out, size_t cap);
  bool     store_decode_settings(const uint8_t *in, size_t len, app_settings_t *out);  /* 검사 실패도 false */
  ```
  `_Static_assert(sizeof(alarm_t) == 128, …)`를 둔다. 구조체가 바뀌면 `STORE_VERSION`을 올리고 이전 버전 변환을 추가해야 한다는 뜻이다.
- Produces (`alarm_core.h`, 순수 추가): `void alarm_engine_set_settings(alarm_engine_t *e, const alarm_settings_t *s);` — 설정만 바꾸고 회차 상태는 그대로 둔다. 진행 중 스누즈의 `snooze_until`은 바꾸지 않고, 다음 스누즈·다음 자동 정지 판단부터 새 값을 쓴다.
- Produces (ESP 추가): `void alarm_service_set_settings(const alarm_settings_t *s);`(뮤텍스 안에서 위 함수 호출), `void button_gpio_set_long_ms(uint32_t long_ms);`(다음 누름부터 적용).
- Produces (`app_config.h`, ESP):

  ```c
  esp_err_t storage_init(void);                                 /* nvs_flash_init, 필요하면 erase 후 재시도 */
  void      storage_load_settings(app_settings_t *out);         /* 없거나 깨졌으면 기본값 + 경고 로그 */
  esp_err_t storage_save_settings(const app_settings_t *s);
  size_t    storage_load_alarms(alarm_t *out, size_t max);      /* 없거나 깨졌으면 0 + 경고 로그 */
  esp_err_t storage_save_alarms(const alarm_t *a, size_t n);
  const app_settings_t *app_settings_get(void);                 /* 현재 설정(읽기 전용, 뮤텍스로 보호한 사본) */
  esp_err_t app_settings_set(const app_settings_t *s);          /* 검사 → 저장 → 적용(alarm_service 설정, 밝기) */
  ```

- [ ] **Step 1: 실패하는 테스트 작성**
  `test_app_config.c`:

  ```c
  void test_defaults_are_valid(void)
  /* mac {..,0xAB,0xCD} → device_name "usagi-abcd", validate true, default_volume 70, night 1380~420 */

  void test_validate_ranges(void)
  /* snooze 0/31, long_press 999/5001, auto_stop 0/61, brightness 0/101, night_start 1440 각각 false; 경계값 1/30, 1000/5000 true */

  void test_device_name_rules(void)
  /* "usagi-mj" true, "a" true, "" false, "-a" false, "a-" false, "Usagi" false, "usagi_mj" false, 25자 false */

  void test_night_window_wraps_midnight(void)      /* Review Focus 3 */
  /* 1380~420: 22:59 false, 23:00 true, 03:00 true, 06:59 true, 07:00 false */

  void test_night_window_same_day_and_disabled(void)
  /* 60~120: 01:00 true, 02:00 false; start == end(0~0) 항상 false */

  void test_ringing_overrides_night(void)          /* Review Focus 3 */
  /* 03:00: app_brightness(ringing false) == 10, (ringing true) == 100; 12:00 → 80 */
  ```
  `test_store_codec.c`:

  ```c
  void test_crc32_known_vector(void)
  /* "123456789" → 0xCBF43926 */

  void test_alarms_round_trip(void)
  /* 알람 3개 encode → decode: n 3, dropped 0, memcmp 같음 */

  void test_alarms_round_trip_empty(void)
  /* 0개 encode → decode true, n 0 */

  void test_alarms_decode_rejects_bad_crc(void)     /* Review Focus 1 */
  /* payload 1바이트를 바꿈 → false */

  void test_alarms_decode_rejects_truncated(void)   /* Review Focus 1 */
  /* 길이 - 1, 헤더보다 짧은 8바이트, 0바이트 → 모두 false */

  void test_alarms_decode_rejects_wrong_magic_and_version(void)
  /* magic을 SETTINGS로 / version 2 → false */

  void test_alarms_decode_drops_invalid_alarm(void) /* Review Focus 1 */
  /* 3개 중 1개의 hour를 24로 만들고 CRC를 다시 계산해 encode → decode true, n 2, dropped 1 */

  void test_settings_round_trip_and_invalid(void)
  /* 기본값 round trip 같음; snooze 0인 설정을 encode한 값 → decode false */
  ```
  `test_alarm_engine_session.c`에 추가:

  ```c
  void test_settings_change_applies_to_next_snooze(void)
  /* 울림 → set_settings(snooze 7) → SHORT at MON_0700+10 → snooze_until == MON_0700+10+420 */

  void test_settings_change_keeps_current_snooze(void)
  /* 울림 → SHORT(5분 스누즈) → set_settings(snooze 7) → snooze_until 그대로, state SNOOZED */
  ```

- [ ] **Step 2: 실행해서 실패 확인** — Run(WSL): `./tools/run_host_tests.sh` → FAIL
- [ ] **Step 3: 순수 부분 구현** (CRC32는 표 없이 비트 단위 반복으로 충분하다. 알람 128바이트 × 32개라 속도 문제없음)
- [ ] **Step 4: 통과 확인** — 전부 PASS, 커버리지 90% 이상
- [ ] **Step 5: ESP 부분 구현과 연결**
  - NVS 키: `settings`, `alarms`(blob). `nvs_set_blob` 뒤 `nvs_commit`. NVS는 키 단위로 원자적으로 써서 전원이 끊겨도 이전 값이나 새 값 둘 중 하나가 남는다.
  - 부팅 순서에 `storage_init` → `storage_load_settings` → `storage_load_alarms` → `alarm_service_start(&settings.alarm)` → `alarm_service_set_alarms`를 넣는다.
  - `alarm_service`: 알람 목록이 바뀔 때(콘솔, 이후 웹)와 `DISABLE_ONCE`가 나올 때 `alarm_service_get_alarms` 결과를 `storage_save_alarms`로 저장한다. 저장은 알람 작업이 아니라 별도 저장 작업에서 한다(길이 1 큐, 요청이 몰려도 마지막 것만 저장). 알람이 지워진 회차의 볼륨은 계획 2의 60 대신 `default_volume`.
  - 화면: 1초마다 `app_brightness(settings, now, mode == RINGING)`로 밝기를 정한다.
  - 콘솔: `settings`(현재 값 출력), `set <키> <값>`(예: `set night_brightness 5`).

- [ ] **Step 6: [실기] 저장 확인**
  `alarm add …` 두 개, `set snooze_min 7` → 전원을 뽑았다 꽂음 → `alarm list`, `settings`가 그대로. 한 번만 알람이 울리고 끝난 뒤 재부팅 → 꺼진 상태로 남아 있음. 야간 시각으로 `settime`을 바꾸면 화면이 어두워짐.

- [ ] **Step 7: Commit**

  ```bash
  git add components/app_config components/display_ui main test/host
  git commit -m "feat(config): 설정·알람 NVS 저장(버전·CRC)과 야간 밝기"
  ```

---

### Task 2: Wi-Fi, SNTP, mDNS

**작성:** Claude. 집 Wi-Fi 입력과 실기는 사용자.

**먼저 설명:** Claude가 ESP-IDF의 Wi-Fi 이벤트 흐름(`WIFI_EVENT_STA_START` → 연결 → `IP_EVENT_STA_GOT_IP`, 끊김과 재시도), SNTP가 시간을 맞추는 방식, mDNS가 `.local` 이름을 알리는 방식을 설명한다.

**Files:**
- Create: `components/net/include/net.h`, `components/net/net_policy.c`, `components/net/net_esp.c`
- Modify: `components/display_ui/`(Wi-Fi 아이콘), `main/main.c`, `main/dev_console_esp.c`, `main/idf_component.yml`(mdns)
- Test: `test/host/test_net_policy.c`

**Interfaces:**
- Consumes: Task 1의 `app_settings_get`(기기 이름), 계획 2의 `timekeeper_set_utc`
- Produces (`net.h`, 순수):

  ```c
  typedef enum { NET_MODE_STA = 0, NET_MODE_APSTA, NET_MODE_AP } net_mode_t;
  typedef enum { NET_WHY_NORMAL = 0, NET_WHY_NO_CREDS, NET_WHY_STA_FAILED, NET_WHY_BUTTON } net_why_t;

  #define NET_STA_FAIL_TO_AP_S 300   /* 설계 5.5: 5분 이상 계속 실패 */

  typedef struct { /* 구현자가 정함 */ ... } net_policy_t;

  void       net_policy_init(net_policy_t *p, bool has_creds, bool setup_button, int64_t now_s);
  void       net_policy_on_connected(net_policy_t *p, int64_t now_s);
  void       net_policy_on_disconnected(net_policy_t *p, int64_t now_s);
  void       net_policy_on_creds_saved(net_policy_t *p, int64_t now_s);
  net_mode_t net_policy_mode(net_policy_t *p, int64_t now_s);   /* 매초 부름 */
  net_why_t  net_policy_why(const net_policy_t *p);
  ```
  결정 규칙:

  | 상황 | 모드 |
  |---|---|
  | 저장된 Wi-Fi 없음 | `AP`(NO_CREDS) |
  | 버튼을 누른 채 부팅(Task 6) | `APSTA`(BUTTON). 재부팅 전까지 유지 |
  | 저장된 Wi-Fi 있음, 연결됐거나 끊긴 지 300초 미만 | `STA` |
  | 마지막 연결(또는 부팅) 이후 300초 이상 계속 끊김 | `APSTA`(STA_FAILED). STA 재시도는 계속 |
  | STA_FAILED 중 연결됨 | `STA`(AP 내림) |
  | 새 Wi-Fi 저장 | 끊김 시계를 0부터 다시 잼, NO_CREDS였으면 `APSTA`로 바꿔 연결 시도 |
- Produces (`net.h`, ESP):

  ```c
  esp_err_t net_start(bool setup_button);      /* NVS의 Wi-Fi 정보로 시작, 매초 정책 반영, SNTP·mDNS 시작 */
  esp_err_t net_set_wifi(const char *ssid, const char *pass);  /* 검사(ssid 1~32, pass 0 또는 8~63) → NVS 저장 → 다시 연결 */
  typedef struct { net_mode_t mode; bool sta_connected; int8_t rssi; int64_t last_sync_utc; char ip[16]; } net_status_t;
  net_status_t net_status(void);
  ```
  SNTP: 서버 `pool.ntp.org`, `time.google.com`, 동기화 주기 1시간(설계 5.1), 동기화 콜백에서 `timekeeper_set_utc`(계획 2의 범위 검사를 거침). mDNS: 호스트 이름 = `device_name`, 서비스 `_http._tcp` 포트 80.

- [ ] **Step 1: 실패하는 테스트 작성** (`test_net_policy.c`)

  ```c
  void test_no_creds_starts_ap(void)
  /* init(has_creds false) → AP, why NO_CREDS */

  void test_creds_start_sta(void)
  /* init(true, false, 0) → STA */

  void test_sta_failure_5min_enters_ap(void)      /* Review Focus 4 */
  /* 연결 후 t=100 끊김 → mode(t=399) STA, mode(t=400) APSTA, why STA_FAILED */

  void test_never_connected_counts_from_boot(void)
  /* init(true, false, 0), 연결 없이 → mode(299) STA, mode(300) APSTA */

  void test_sta_recovers_leaves_ap(void)          /* Review Focus 4 */
  /* 위에서 t=500 연결 → STA */

  void test_button_setup_stays_until_reboot(void)
  /* init(true, true, 0) → APSTA, why BUTTON; 연결돼도 APSTA */

  void test_creds_saved_from_ap(void)
  /* init(false) → AP; on_creds_saved(10) → APSTA; 연결(20) → STA */
  ```

- [ ] **Step 2: 실행해서 실패 확인** → FAIL
- [ ] **Step 3: `net_policy.c` 구현**
- [ ] **Step 4: 통과 확인** → 전부 PASS
- [ ] **Step 5: `net_esp.c` 구현과 화면 연결**
  Wi-Fi 정보는 NVS `usagi`의 `wifi` 키(ssid·pass 구조체)에 저장하고 `esp_wifi_set_storage(WIFI_STORAGE_RAM)`으로 ESP-IDF 자체 저장은 쓰지 않는다(공장 초기화를 한 곳에서 하기 위해서). AP 시작은 Task 6에서 채우고, 이 Task에서는 `AP`/`APSTA` 결정이 나면 로그만 남긴다.
  `ui_state_t`에 `wifi` 필드(0 없음, 1 연결 중, 2 연결됨)를 더하고 시계 화면 오른쪽 위에 LVGL 기호 `LV_SYMBOL_WIFI`로 표시한다(설계 5.4).
  콘솔: `wifi <ssid> <pass>`(Part B 전까지 Wi-Fi 입력 수단), `net`(상태). **비밀번호는 로그에 찍지 않는다.**

- [ ] **Step 6: [실기] 연결·동기화·이름 확인**
  1. 콘솔에서 `wifi <집 SSID> <비밀번호>`(사용자가 직접 입력) → 로그 `got ip`, 화면 Wi-Fi 표시
  2. 1분 안에 `sntp: synced` 로그, RTC에도 기록됨(재부팅 후 `time` 확인)
  3. 아이폰 사파리에서 `http://usagi-xxxx.local` → 아직 페이지는 없으므로 연결 오류가 아닌 404가 나오면 성공(Task 3 이후 확인해도 됨)
  4. 공유기 전원을 6분 끔 → 5분 뒤 `net: AP mode (sta failed)` 로그 → 공유기를 켜면 다시 STA. 그동안 알람이 정상 동작하는지 테스트 알람으로 확인

- [ ] **Step 7: Commit**

  ```bash
  git add components/net components/display_ui main test/host/test_net_policy.c
  git commit -m "feat(net): Wi-Fi 연결 정책, SNTP 동기화, mDNS 이름"
  ```

---

### Task 3: REST API

**작성:** Claude.

**Files:**
- Create: `components/web_api/include/web_api.h`, `components/web_api/web_json.c`, `components/web_api/web_server_esp.c`
- Modify: `test/host/CMakeLists.txt`(cJSON), `main/main.c`
- Test: `test/host/test_web_json.c`

**Interfaces:**
- Consumes: 계획 1의 `alarm_validate`, Task 1의 `app_settings_validate`, `app_settings_set`, Task 2의 `net_set_wifi`, `net_status`, 계획 2의 `alarm_service_*`, `timekeeper_*`
- cJSON: 펌웨어는 ESP-IDF의 `json` 컴포넌트, 없으면 `espressif/cjson` **[빌드 확인]**. PC 테스트는 `FetchContent`로 [DaveGamble/cJSON](https://github.com/DaveGamble/cJSON) `v1.7.18`을 받아 경고 완화 라이브러리로 빌드한다.
- JSON 형식:

  ```json
  알람: {"id":1,"enabled":true,"hour":7,"minute":0,"days":127,"pre_minutes":5,
         "pre_volume":70,"main_volume":70,"label":"출근","clipset":"default","song":"a.mp3"}
  설정: {"snooze_min":5,"long_press_ms":2000,"auto_stop_min":10,"default_volume":70,
         "day_brightness":80,"night_brightness":10,"night_start_min":1380,"night_end_min":420,
         "device_name":"usagi-mj"}
  ```
  `hour`는 0~23(12시간 표시는 웹 페이지가 한다), `days`는 계획 1의 비트마스크(bit0=월). 문자열 길이는 UTF-8 바이트로 잰다(label 23, clipset 31, song 63 바이트 이하).
- Produces (`web_api.h`, 순수):

  ```c
  /* 실패하면 false, err에 "hour out of range" 같은 영어 한 줄 */
  bool   web_alarm_from_json(const char *json, alarm_t *out, char *err, size_t err_n);
  bool   web_settings_from_json(const char *json, const app_settings_t *base, app_settings_t *out, char *err, size_t err_n);
  size_t web_alarm_to_json(const alarm_t *a, char *out, size_t n);           /* 쓴 길이, 모자라면 0 */
  size_t web_alarms_to_json(const alarm_t *a, size_t count, char *out, size_t n);  /* {"alarms":[…]} */
  size_t web_settings_to_json(const app_settings_t *s, char *out, size_t n);
  uint8_t web_pick_alarm_id(const alarm_t *a, size_t count);                  /* 쓰지 않는 가장 작은 1..255, 없으면 0 */
  ```
  알람 JSON은 모든 필드가 있어야 한다(`id`는 POST에서 무시). 설정 JSON은 **있는 필드만** `base`에 덮어쓴다(부분 수정).
- 라우팅(`web_server_esp.c`, 설계 5.5 API):

  | 메서드·경로 | 동작 | 성공 |
  |---|---|---|
  | `GET /api/alarms` | 목록 | 200 `{"alarms":[…]}` |
  | `POST /api/alarms` | id 배정 후 추가(32개면 409) | 201 알람 |
  | `PUT /api/alarms/{id}` | 바꿈(없으면 404) | 200 알람 |
  | `DELETE /api/alarms/{id}` | 지움 | 204 |
  | `GET/PUT /api/settings` | 설정 읽기·부분 수정 | 200 설정 |
  | `GET /api/status` | 시각, 시간 앎, 마지막 동기화, Wi-Fi, SD 용량, 펌웨어 버전 | 200 |
  | `POST /api/time` | `{"utc":<초>}` → `timekeeper_set_utc` | 204 |
  | `POST /api/wifi` | `{"ssid","pass"}` → `net_set_wifi` | 202 (응답 후 다시 연결) |
  알람 쓰기는 `alarm_service_get_alarms` → 수정 → `alarm_service_set_alarms` → 저장 요청 순서. 진행 중인 회차는 계획 1 엔진이 유지한다.

- [ ] **Step 1: 실패하는 테스트 작성** (`test_web_json.c`)

  ```c
  void test_alarm_json_round_trip(void)
  /* mk 알람 → to_json → from_json → 같음(label "출근" UTF-8 포함) */

  void test_alarm_json_rejects_out_of_range(void)   /* Review Focus 2 */
  /* hour 24, minute 60, days 128, pre_minutes 61, main_volume 101 각각 false, err 비어 있지 않음 */

  void test_alarm_json_rejects_wrong_type(void)     /* Review Focus 2 */
  /* "hour":"7", "enabled":1, "days":1.5 각각 false */

  void test_alarm_json_label_too_long(void)         /* Review Focus 2 */
  /* label 24바이트 false, 23바이트 true; 한글 8글자(24바이트) false */

  void test_alarm_json_missing_field(void)          /* Review Focus 2 */
  /* minute 없음 false; 깨진 JSON "{" false; 빈 문자열 false */

  void test_settings_partial_update(void)
  /* base 기본값 + {"snooze_min":7} → snooze 7, 나머지 base 그대로 */

  void test_settings_rejects_invalid(void)
  /* {"device_name":"Bad_Name"} false; {"night_start_min":1440} false */

  void test_pick_alarm_id(void)
  /* 빈 목록 1; {1,2,4} → 3; 1..255 모두 쓰면 0 */

  void test_alarms_to_json_buffer_too_small(void)
  /* n = 10 → 0 */
  ```

- [ ] **Step 2: 실행해서 실패 확인** → FAIL
- [ ] **Step 3: `web_json.c` 구현** (숫자는 `cJSON_IsNumber` + 정수인지 확인, bool은 `cJSON_IsBool`만 인정)
- [ ] **Step 4: 통과 확인** → 전부 PASS, 커버리지 90% 이상
- [ ] **Step 5: `web_server_esp.c` 구현**
  `httpd_start`(포트 80, URI 와일드카드 매칭, 최대 핸들러 24). 본문은 `Content-Length` 4KB 넘으면 413. 응답에 `Cache-Control: no-store`.
- [ ] **Step 6: [실기] PC에서 curl로 확인**

  ```bash
  curl http://usagi-xxxx.local/api/status
  curl -X POST http://usagi-xxxx.local/api/alarms -d '{"enabled":true,"hour":7,"minute":0,"days":31,"pre_minutes":5,"pre_volume":70,"main_volume":70,"label":"work","clipset":"","song":""}'
  curl -X PUT http://usagi-xxxx.local/api/settings -d '{"snooze_min":30}'
  curl -X PUT http://usagi-xxxx.local/api/settings -d '{"snooze_min":31}'
  ```
  Expected: 200/201/200, 마지막은 400 `{"error":"snooze_min out of range"}`. 재부팅 후에도 추가한 알람이 남음.
- [ ] **Step 7: Commit**

  ```bash
  git add components/web_api test/host main
  git commit -m "feat(web): REST API와 JSON 검사"
  ```

---

### Task 4: 웹 페이지

**작성:** Claude(설계 9.1 역할 분담에서도 Claude). 아이폰 확인은 사용자.

**Files:**
- Create: `web/index.html`, `web/app.js`, `web/style.css`, `web/manifest.webmanifest`, `web/icon.svg`
- Modify: `main/CMakeLists.txt`(`littlefs_create_partition_image(storage ../web FLASH_IN_PROJECT)`), `main/idf_component.yml`(`joltwallet/littlefs`), `components/web_api/web_server_esp.c`(정적 파일)

**Interfaces:**
- Consumes: Task 3 API 전부
- 화면 구성(설계 5.5), 탭 4개:
  - **알람**: 목록(시각 12시간제 + AM/PM, 요일 요약 "평일"/"매일"/"한 번", 켜기 스위치), 추가·수정 시트(시각, 요일 7개 버튼과 "매일"·"평일" 프리셋, 미리 알림 분 0~60, 미리 알림·본 알람 볼륨 슬라이더, 이름, 노래·클립 세트 선택), 삭제
  - **소리**: 곡과 클립 세트 목록(Task 5에서 업로드·삭제·미리 듣기 추가)
  - **설정**: 기본 볼륨, 스누즈, 길게 누르기, 자동 정지, 낮·밤 밝기, 야간 시간대, 기기 이름, Wi-Fi 변경, "휴대폰 시간으로 맞추기"(`Date.now()/1000` → `POST /api/time`)
  - **상태**: 현재 시각, 마지막 동기화, Wi-Fi 신호, SD 용량, 펌웨어 버전
- 규칙: 글꼴은 시스템 글꼴, 터치 대상 44px 이상(아이폰 기준), 화면 폭 375px에서 가로 스크롤 없음, 다크 모드(`prefers-color-scheme`) 지원. API 오류는 화면 위 토스트로 `error` 문구를 보여준다. 정적 파일 전체 100KB 이하.
- 정적 파일 응답: `/`는 `index.html`, 확장자로 Content-Type, `Cache-Control: no-cache`.

- [ ] **Step 1: 페이지 작성과 빌드**
  Run: `idf.py build` → `Project build complete`, `storage` 이미지 생성 로그
- [ ] **Step 2: [실기] 아이폰 사파리 확인 목록**
  1. `http://usagi-xxxx.local` 접속, 홈 화면에 추가 → 아이콘과 이름 표시
  2. 알람 추가(평일 7:00 AM, 미리 알림 5분) → 시계 화면의 `NEXT`가 바뀜
  3. 알람 수정·끄기·삭제가 바로 반영되고 재부팅 후에도 유지
  4. 범위 밖 입력(미리 알림 70분을 직접 입력) → 오류 토스트, 기존 값 유지
  5. 야간 시간대·밝기를 바꾸면 바로 화면 밝기 반영
  6. "휴대폰 시간으로 맞추기" → 시계 시각이 휴대폰과 1초 이내
  7. 다크 모드, 가로 스크롤 없음
  Expected: 1~7 통과. 어색한 부분은 사용자가 말하면 Claude가 고친다.
- [ ] **Step 3: Commit + push, CI 녹색 확인**

  ```bash
  git add web main components/web_api
  git commit -m "feat(web): 아이폰용 설정 페이지"
  git push
  ```

---

## Part B — 밀리면 2차로 넘길 후보 (설계 8장 순서)

### Task 5: 소리 업로드·삭제·미리 듣기

**작성:** Claude.

**Files:**
- Create: `components/web_api/upload_name.c`, `components/web_api/web_sounds_esp.c`
- Modify: `components/web_api/include/web_api.h`, `components/audio/`(`AUDIO_OP_PREVIEW`), `web/app.js`, `web/index.html`
- Test: `test/host/test_upload_name.c`

**Interfaces:**
- Consumes: 계획 3의 `audio_name_ok`, `audio_is_mp3_name`, `sdcard_list_mp3`, `sdcard_ensure_mounted`, `audio_post`
- Produces (`web_api.h`, 순수):

  ```c
  /* 업로드한 이름을 FAT에 안전한 이름으로: \ / : * ? " < > | 와 제어 문자 → '_',
     앞뒤 공백·점 제거, 확장자 .mp3로 맞춤(없으면 붙임), 전체 63바이트 이하(UTF-8 글자 경계에서 자름).
     결과가 audio_name_ok가 아니면 false */
  bool web_sanitize_upload_name(const char *in, char *out, size_t n);
  ```
- Produces (`audio.h`): `AUDIO_OP_PREVIEW` — `name`의 곡을 10초 재생(반복 없음, 실패하면 조용히 끝). 회차가 진행 중(IDLE 아님)이면 `alarm_service`가 미리 듣기를 거절한다.
- 라우팅(설계 5.5):

  | 메서드·경로 | 동작 |
  |---|---|
  | `GET /api/sounds` | `{"songs":[{"name","size"}],"clipsets":{"default":["01_u.mp3",…]}}` |
  | `POST /api/sounds?name=<이름>[&set=<세트>]` | 본문 = 파일 바이트. `set`이 있으면 `/sd/clips/<세트>/`, 없으면 `/sd/sounds/`. 임시 파일 `.upload.tmp`에 쓰고 끝나면 이름 바꿈. 남은 공간 < 본문 크기면 507, 20MB 넘으면 413 |
  | `DELETE /api/sounds/{name}[?set=<세트>]` | 삭제(없으면 404) |
  | `POST /api/sounds/{name}/preview[?set=<세트>]` | 미리 듣기(회차 중이면 409) |

  업로드는 8KB씩 받아 바로 쓴다(메모리에 파일 전체를 올리지 않는다). 세트 이름도 `audio_name_ok`를 거친다.

- [ ] **Step 1: 실패하는 테스트 작성** (`test_upload_name.c`)

  ```c
  void test_sanitize_keeps_normal(void)
  /* "Morning Song.mp3" → "Morning Song.mp3"; "아침 노래.MP3" → "아침 노래.MP3" */

  void test_sanitize_replaces_forbidden(void)      /* Review Focus 5 */
  /* "a:b*c?.mp3" → "a_b_c_.mp3"; "../x.mp3" → "_x.mp3" ('/'를 '_'로 바꾼 ".._x.mp3"에서 앞 점 제거); "song" → "song.mp3" */

  void test_sanitize_trims(void)
  /* "  a.mp3  " → "a.mp3"; "...a.mp3" → "a.mp3" */

  void test_sanitize_truncates_on_utf8_boundary(void)  /* Review Focus 5 */
  /* 한글 30글자(90바이트) + ".mp3" → 결과 63바이트 이하, ".mp3"로 끝남, 유효한 UTF-8(잘린 글자 없음) */

  void test_sanitize_rejects_empty(void)
  /* "", ".mp3", "   " → false */
  ```
  처리 순서는 금지 문자 바꾸기 → 앞뒤 공백·점 제거 → 확장자 맞추기 → 길이 자르기다.

- [ ] **Step 2: 실행해서 실패 확인** → FAIL
- [ ] **Step 3: 구현** (`upload_name.c`, `web_sounds_esp.c`, 페이지의 소리 탭: 파일 선택 → `fetch(POST, body=file)` 진행률 표시)
- [ ] **Step 4: 통과 확인** → 전부 PASS
- [ ] **Step 5: [실기]** 아이폰에서 MP3 업로드(10MB 내외) → 목록에 나타남 → 알람에 지정 → 울림. 클립 세트 새로 만들어 "우"/"나" 업로드 → 미리 알림에 반영. 알람 중 미리 듣기 → 409 토스트.
- [ ] **Step 6: Commit**

  ```bash
  git add components/web_api components/audio web test/host/test_upload_name.c
  git commit -m "feat(web): 소리 업로드·삭제·미리 듣기"
  ```

---

### Task 6: 설정 모드(AP + 캡티브 포털)와 공장 초기화

**작성:** Claude.

**Files:**
- Create: `components/net/dns_reply.c`, `components/net/captive_esp.c`
- Modify: `components/net/include/net.h`, `components/net/net_esp.c`, `components/web_api/web_server_esp.c`, `components/display_ui/`(설정 모드 화면), `main/main.c`
- Test: `test/host/test_dns_reply.c`

**Interfaces:**
- Produces (`net.h`, 순수):

  ```c
  /* DNS 질의 패킷(UDP 본문)에 대해 모든 A 질의를 ip로 답하는 응답을 만든다.
     질의가 1개가 아니거나, 질의(QR=1)가 아니거나, 길이가 맞지 않으면 0(응답하지 않음) */
  size_t dns_build_reply(const uint8_t *q, size_t qlen, const uint8_t ip[4], uint8_t *out, size_t out_n);

  typedef enum { BOOT_HOLD_NONE = 0, BOOT_HOLD_SETUP, BOOT_HOLD_FACTORY_RESET } boot_hold_t;
  #define BOOT_HOLD_FACTORY_MS 10000   /* 설계 5.5: 10초 */
  boot_hold_t boot_hold_classify(bool pressed_at_boot, uint32_t held_ms);   /* 누르지 않음 NONE, 10초 미만 SETUP, 이상 FACTORY_RESET */
  ```
- 설정 모드 동작(설계 5.5):
  - SSID `Usagi-Setup-<MAC 끝 2바이트 HEX>`, WPA2. 비밀번호는 처음 부팅 때 `esp_random`으로 만든 영문 소문자·숫자 10자를 NVS `factory` 네임스페이스에 저장해 계속 쓴다 **[제안]**. 설정 모드 화면에 SSID와 비밀번호를 표시하고, 콘솔 `apinfo`로도 출력한다(시계 바닥 스티커용).
  - AP IP `192.168.4.1`. DNS 서버(UDP 53)는 모든 이름을 이 주소로 답한다. HTTP 서버는 알 수 없는 경로(iOS의 `/hotspot-detect.html` 포함)를 `302 → http://192.168.4.1/`로 보낸다. 이렇게 하면 아이폰이 접속하자마자 설정 페이지를 띄운다.
  - 이 모드에서도 같은 웹 페이지와 API 전체를 쓴다. 알람은 계속 돈다.
  - 부팅 때 버튼이 눌려 있으면 최대 10초 동안 화면에 남은 초를 보여주며 기다린다. 10초 전에 떼면 설정 모드, 10초를 채우면 `usagi` 네임스페이스를 지우고 재부팅(SD 파일과 `factory`는 유지).

- [ ] **Step 1: 실패하는 테스트 작성** (`test_dns_reply.c`)

  ```c
  void test_reply_to_a_query(void)
  /* "captive.apple.com" A 질의(손으로 만든 바이트 배열) → 응답 길이 > qlen, ID 같음, QR=1, ANCOUNT 1, 마지막 4바이트 192.168.4.1 */

  void test_ignore_response_packet(void)
  /* QR=1인 패킷 → 0 */

  void test_ignore_malformed(void)
  /* 길이 5, 이름 레이블 길이가 패킷을 넘는 질의, QDCOUNT 2 → 모두 0 */

  void test_out_buffer_too_small(void)
  /* out_n = qlen → 0 */

  void test_boot_hold_classify(void)
  /* (false, 0) NONE; (true, 500) SETUP; (true, 9999) SETUP; (true, 10000) FACTORY_RESET */
  ```

- [ ] **Step 2: 실행해서 실패 확인** → FAIL
- [ ] **Step 3: 구현** (`dns_reply.c`, `captive_esp.c`는 UDP 소켓 작업 하나, `net_esp.c`의 AP 시작·종료, 부팅 버튼 확인)
- [ ] **Step 4: 통과 확인** → 전부 PASS
- [ ] **Step 5: [실기]**
  1. 버튼을 누른 채 부팅 → 3초에 손 뗌 → 화면에 SSID·비밀번호 → 아이폰에서 접속하면 설정 페이지가 자동으로 뜸 → 알람 추가 가능
  2. Wi-Fi 정보 입력 → 집 Wi-Fi 연결 확인(설정 모드는 재부팅 때까지 유지)
  3. 버튼을 10초 누른 채 부팅 → 재부팅 후 알람·Wi-Fi가 비어 있음, SD 파일은 남아 있음, AP 비밀번호는 같음
- [ ] **Step 6: Commit**

  ```bash
  git add components/net components/web_api components/display_ui main test/host/test_dns_reply.c
  git commit -m "feat(net): 설정 모드 캡티브 포털과 공장 초기화"
  ```

---

### Task 7: 웹 펌웨어 업데이트(OTA)와 롤백

**작성:** Claude.

**Files:**
- Create: `components/web_api/web_ota_esp.c`
- Modify: `sdkconfig.defaults`, `main/main.c`, `web/`(상태 탭에 업데이트)

**Interfaces:**
- 라우팅: `POST /api/ota` 본문 = `build/usagi-clock.bin`. `esp_ota_begin(다음 슬롯)` → 8KB씩 `esp_ota_write` → `esp_ota_end`(이미지 검증) → `esp_ota_set_boot_partition` → 응답 후 2초 뒤 재시작. 실패하면 `esp_ota_abort`하고 400, 지금 펌웨어는 그대로. 회차 진행 중이면 409.
- 롤백: `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`. 새 펌웨어는 부팅 후 60초 동안 알람 작업과 화면 작업이 살아 있고 워치독 리셋이 없으면 `esp_ota_mark_app_valid_cancel_rollback()`. 그 전에 리셋되면 부트로더가 이전 슬롯으로 되돌린다.
- 상태 API의 `fw_version`은 `esp_app_get_description()->version`(git describe).

- [ ] **Step 1: 구현과 빌드** — `idf.py build` → `Project build complete`
- [ ] **Step 2: [실기]**
  1. 버전 문자열만 바꾼 펌웨어를 아이폰(또는 PC `curl --data-binary @build/usagi-clock.bin`)으로 올림 → 재부팅 후 상태 탭 버전이 바뀜, 알람·설정 유지
  2. 부팅 직후 일부러 `abort()`하는 시험 펌웨어를 올림 → 이전 버전으로 자동 복귀
  3. 업로드 도중 Wi-Fi를 끊음 → 지금 펌웨어로 계속 동작
- [ ] **Step 3: Commit + push, CI 녹색 확인**

  ```bash
  git add components/web_api sdkconfig.defaults main web
  git commit -m "feat(web): 로컬 웹 펌웨어 업데이트와 자동 롤백"
  git push
  ```

---

## 계획 4 완료 기준

- Part A: 아이폰 사파리로 알람·설정을 바꾸고, 재부팅 후에도 남으며, SNTP로 시간이 맞고, 밤에 화면이 어두워진다(Task 4 Step 2 목록 1~7 통과).
- Part B: 넘기지 않은 Task는 각 실기 Step 통과. 넘긴 Task는 이 문서에 "2차로 넘김"과 날짜를 적는다.
- PC 테스트 전부 통과, 순수 C 줄 커버리지 90% 이상, cppcheck 경고 0, GitHub Actions 녹색
- 다음 단계: 계획 5(두 대 조립, 케이스 모델링과 출력, 장시간 시험, README)
