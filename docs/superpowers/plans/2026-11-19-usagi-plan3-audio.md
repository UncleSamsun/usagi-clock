# 우사기 알람시계 — 계획 3: 오디오 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **실행 방식 [사용자 결정, 2026-11-19]:** 코드와 테스트는 Claude가 작성한다. 사용자는 배선, SD카드 준비, 실기 확인(소리 듣기), 로직 분석기 관찰을 한다. 실기 Step은 사용자 보드에서만 끝낼 수 있다.
>
> **표기:** **[실물 확인]** 실기에서 값을 확인하고 다르면 이 문서를 고친다. **[제안]** Claude가 정한 것으로 바꿀 수 있다.

**Goal:** 알람이 SD카드의 MP3 노래로 울리고, 미리 알림은 클립 세트("우" → "나" → …)를 매초 경계에 맞춰 재생하며, SD카드나 파일에 문제가 있으면 비프음으로 대신 울린다.

**Architecture:** 계획 2의 `audio` 컴포넌트를 키운다. 무엇을 재생할지 정하는 판단(플레이어 상태 머신, 경로 검사, 클립 캐시, MP3 스트림 해독)은 모두 순수 C로 만들어 PC에서 테스트하고, `audio_esp.c`는 SD 파일 읽기와 I2S 출력만 한다. MP3 해독은 헤더 하나짜리 minimp3(CC0)를 저장소에 넣어 PC 테스트와 펌웨어가 같은 코드를 쓴다.

**Tech Stack:** ESP-IDF v6.1, `esp_driver_sdspi` + FATFS(`esp_vfs_fat_sdspi_mount`), `esp_driver_i2s`(std), minimp3(lieff/minimp3, CC0), FreeRTOS, Unity·gcovr·cppcheck, ffmpeg(테스트용 MP3 생성, 개발 PC에서만)

**Spec:** `docs/specs/2026-11-16-usagi-alarm-clock-design.md` (4장 SD·앰프, 5.2 알람 항목의 소리·볼륨, 5.3 "우! 나!" 재생 방식과 소리 실패 대비, 5.6 SD 폴더 구조, 6장 `audio`)
**이전 계획:** `docs/superpowers/plans/2026-11-19-usagi-plan2-hardware-bringup.md` (이 계획은 계획 2 완료 후 실행한다)

> **실행 메모 (2026-11-19, 부품 도착 전 코드 선작성)**
> - 코드·PC 테스트·펌웨어 빌드까지 끝났다. 남은 것은 **[실기]** Step과 ffmpeg 테스트 파일 만들기다.
> - 계획과 달라진 점
>   - MP3 테스트를 둘로 나눴다. `test_mp3_stream.c`는 **손으로 만든 무음 MPEG 프레임**으로 프레임 찾기·끝·오류 판정·ID3·스테레오 섞기를 확인해서 ffmpeg 없이 항상 돈다. 소리(주파수) 확인은 `test_mp3_fixtures.c`로 옮겼고, `fixtures/make_fixtures.sh`로 MP3를 만들면 CMake가 자동으로 포함한다(ffmpeg 설치는 사용자가 비밀번호 입력).
>   - minimp3는 동기를 잡을 때 뒤따르는 프레임 헤더를 여러 개 확인한다. 그래서 파일 끝에 쓰레기가 붙으면 그 바로 앞 몇 프레임(0.3초 이내)은 버려질 수 있다. 테스트는 이를 반영했다.
>   - `clip_cache_add`는 `mp3_stream_t` 대신 `clip_source_t`(read·rate·failed 콜백)를 받는다. 가짜 소스로 PC 테스트를 하기 위해서다. 펌웨어에서는 mp3_stream을 감싸 넘긴다.
>   - SD카드 함수는 `audio.h`가 아니라 `sdcard.h`(ESP 전용)에 있다. `AUDIO_NAME_MAX`는 `audio.h`에 있고 `audio_files.h`가 그것을 include한다.
>   - `audio_player`에 `PLAY_PREVIEW`(계획 4의 미리 듣기, 10초, 반복 없음)를 미리 넣었다.

## 범위

| 들어감 | 빠짐 |
|---|---|
| SD 마운트(SPI3), 폴더 규칙, 파일 목록 | 웹 업로드, 미리 듣기 API → 계획 4 |
| MP3 해독(모노 다운믹스), 볼륨, 반복 재생 | 전역 기본 볼륨 설정 → 계획 4(그 전에는 알람별 볼륨만) |
| 미리 알림 클립 세트(PSRAM 캐시, 1초 자르기) | 캐릭터 이미지 → 별도 |
| 실패 시 비프음 대체(SD 없음, 파일 없음, 해독 실패) | |
| 콘솔에서 알람에 노래·클립 세트 지정 | |

## 준비 작업 (코드 아님)

- [ ] microSD 8~32GB를 PC에서 **FAT32**로 포맷한다. 폴더를 만든다: `/sounds`, `/clips/default`
- [ ] `/sounds`에 시험용 MP3 2곡, `/clips/default`에 "우" 클립과 "나" 클립(각 1초 미만, 파일명은 `01_u.mp3`, `02_na.mp3`처럼 순서대로)을 넣는다. 캐릭터 음성은 저장소에 넣지 않는다.
- [ ] WSL에 ffmpeg 설치(테스트용 MP3 생성): `sudo apt install -y ffmpeg` (비밀번호는 사용자가 입력)
- [ ] **[실물 확인]** 계획 2에서 배선해 둔 SD 핀(SCK 7, MOSI 6, MISO 5, CS 4)과 디스플레이 모듈의 SD 핀 이름을 다시 확인한다.

## Global Constraints

- 계획 2의 Global Constraints를 모두 따른다(파일 규칙 `*_esp.c`, 쓰지 않는 GPIO, 알람 경로 `malloc` 금지, 볼륨 상한 `AUDIO_PEAK_MAX`).
- SD 폴더: 노래 `/sd/sounds/*.mp3`, 클립 세트 `/sd/clips/<세트 이름>/*.mp3`. 마운트 지점은 `/sd`.
- **SD카드를 절대 포맷하지 않는다**(`format_if_mount_failed = false`). 사용자 파일이 들어 있다.
- 알람의 `song`이 빈 값이면 `/sd/sounds`의 첫 번째 곡(파일명 바이트 순 정렬), `clipset`이 빈 값이면 `default` 세트(설계 5.2 기본값).
- 파일 이름 규칙: `alarm_t.song`(64바이트)과 `clipset`(32바이트)은 이름만 담는다. `/`, `\`, `..`가 들어 있거나 빈 문자열이거나 `.`으로 시작하면 쓰지 않는다. 확장자는 `.mp3`(대소문자 무시)만 인정한다.
- 클립 세트: 최대 `CLIPSET_MAX = 16`개, 클립마다 최대 1초. 1초를 넘는 부분은 캐시할 때 잘라 둔다(설계 5.3 "다음 초에서 끊는다").
- 클립 캐시는 `audio_start`에서 PSRAM에 한 번 잡는다: `CLIP_CACHE_SAMPLES = 16 * 48000`(int16 모노, 약 1.5MB). 실행 중에 다시 할당하지 않는다.
- 소리 실패 대비(설계 5.3): 본 알람은 어떤 경우에도 소리가 나야 한다. 노래를 못 열거나 해독이 실패하면 계획 2의 비프 반복으로 바꾼다. 미리 알림 클립이 없거나 실패하면 계획 2의 짧은 비프로 바꾼다.
- 오디오 작업 스택 24KB(minimp3가 프레임마다 스택에 약 16KB 작업 공간을 쓴다), 우선순위는 `alarm_service`보다 높게.
- 서드파티 코드는 `components/audio/third_party/` 아래에만 두고, 커버리지·cppcheck 대상에서 뺀다. 라이선스 파일을 함께 넣는다.
- 테스트용 MP3는 ffmpeg로 직접 만든 사인파만 커밋한다(저작물 아님).

## Review Focus

1. 노래 파일이 MP3가 아니거나 중간부터 깨져 있으면 소리가 끊긴 채로 있으면 안 되고 비프음으로 계속 울려야 한다. → Task 2 `test_not_mp3_reports_error`, `test_truncated_file_ends_without_hang`, Task 3 `test_song_error_falls_back_to_beep_loop`
2. SD카드가 없거나 알람 중에 뽑히면 비프음으로 울려야 한다. → Task 3 `test_sd_missing_uses_beep_loop`, `test_sd_removed_during_song_falls_back`
3. 알람에 지정한 노래가 지워졌거나 이름이 바뀌었으면 첫 번째 곡으로, 노래가 하나도 없으면 비프음으로 울려야 한다. → Task 3 `test_missing_song_uses_first_song`, `test_no_songs_uses_beep_loop`
4. 노래 이름에 `../`나 `/`가 들어 있으면(계획 4의 웹 입력에서 올 수 있음) SD 밖이나 다른 폴더의 파일을 열면 안 된다. → Task 1 `test_path_rejects_traversal`
5. 클립이 1초보다 길면 다음 초에 끊고 다음 클립으로 넘어가야 하고, 클립이 하나도 없으면 비프로 박자를 유지해야 한다. → Task 4 `test_clip_truncated_to_one_second`, Task 3 `test_pre_tick_without_clips_uses_beep_tick`

---

## 파일 구조

```
components/audio/
├── CMakeLists.txt                 SRCS 추가, third_party 경고 완화
├── include/audio.h                audio_cmd_t를 순수 부분으로 옮기고 이름 필드 추가 (Task 3)
├── include/audio_files.h          경로 검사, mp3 이름 필터·정렬 (Task 1)
├── include/mp3_stream.h           (Task 2)
├── include/audio_player.h         (Task 3)
├── include/clip_cache.h           (Task 4)
├── audio_tone.c                   계획 2, audio_apply_volume 추가 (Task 2)
├── audio_files.c                  순수 (Task 1)
├── mp3_stream.c                   순수, minimp3 감싸기 (Task 2)
├── audio_player.c                 순수, 무엇을 재생할지 결정 (Task 3)
├── clip_cache.c                   순수, 클립 PCM 묶음 (Task 4)
├── sdcard_esp.c                   SD 마운트, 디렉터리 읽기 (Task 1)
├── audio_esp.c                    I2S 출력 작업, 소스 실행 (Task 5)
└── third_party/
    ├── minimp3.h                  lieff/minimp3 (CC0)
    ├── minimp3_impl.c             #define MINIMP3_IMPLEMENTATION 후 include
    └── LICENSE-minimp3            CC0 전문, 가져온 커밋 해시
test/host/
├── CMakeLists.txt                 minimp3 라이브러리, FIXTURE_DIR 정의
├── fixtures/
│   ├── make_fixtures.sh           ffmpeg로 아래 파일 생성
│   ├── tone440_44k_mono.mp3
│   ├── tone440_44k_stereo_id3.mp3
│   ├── tone1k_22k_mono_2s.mp3
│   ├── truncated.mp3
│   └── not_mp3.mp3
├── test_audio_files.c
├── test_mp3_stream.c
├── test_audio_player.c
└── test_clip_cache.c
```

---

### Task 1: SD카드 마운트와 파일 규칙

**작성:** Claude. 배선 확인과 실기는 사용자.

**먼저 설명:** Claude가 SD카드의 SPI 모드(CMD0으로 SPI 진입, CMD8·ACMD41 초기화, 512바이트 블록 읽기)와 FAT32(클러스터, 디렉터리 항목, 긴 파일 이름)를 설명하고, 로직 분석기로 마운트 직후 CMD0~ACMD41 명령을 400kHz에서 캡처해 본다.

**Files:**
- Create: `components/audio/include/audio_files.h`, `components/audio/audio_files.c`, `components/audio/sdcard_esp.c`
- Modify: `components/audio/CMakeLists.txt`, `sdkconfig.defaults`, `main/dev_console_esp.c`, `main/main.c`
- Test: `test/host/test_audio_files.c`

**Interfaces:**
- Produces (`audio_files.h`, 순수):

  ```c
  #define AUDIO_NAME_MAX 64     /* alarm_t.song 크기와 같음 */
  #define AUDIO_PATH_MAX 128

  bool   audio_name_ok(const char *name);   /* 빈 값, '/', '\\', "..", '.'으로 시작 → false */
  bool   audio_is_mp3_name(const char *name);  /* ".mp3"로 끝남(대소문자 무시), 숨김 파일(.로 시작) 제외 */
  /* dir + "/" + name. name이 audio_name_ok가 아니거나 결과가 n을 넘으면 false, out은 빈 문자열 */
  bool   audio_path_join(char *out, size_t n, const char *dir, const char *name);
  /* 이름 배열을 바이트 순(strcmp)으로 정렬. 같은 이름은 그대로 */
  void   audio_sort_names(char names[][AUDIO_NAME_MAX], size_t count);
  ```
- Produces (`audio.h`에 추가, ESP):

  ```c
  #define SD_MOUNT_POINT "/sd"
  typedef struct { int sck, mosi, miso, cs; } sdcard_pins_t;

  esp_err_t sdcard_start(const sdcard_pins_t *pins);  /* SPI3 버스 생성 + 마운트 시도. 카드가 없어도 ESP_OK(상태만 기록) */
  bool      sdcard_ensure_mounted(void);   /* 마운트돼 있으면 true. 아니면 마지막 시도 30초 뒤부터 다시 시도 */
  void      sdcard_mark_failed(void);      /* 읽기 오류가 나면 부름: 언마운트하고 다음 ensure에서 다시 시도 */
  /* dir 안의 .mp3 이름을 정렬해 최대 max개 채운다. 폴더가 없거나 마운트 안 됐으면 0 */
  size_t    sdcard_list_mp3(const char *dir, char names[][AUDIO_NAME_MAX], size_t max);
  ```
  `sdcard_list_mp3`의 `names` 버퍼는 호출하는 쪽이 정적으로 가진다(알람 경로 `malloc` 금지).

- [ ] **Step 1: 실패하는 테스트 작성** (`test_audio_files.c`)

  ```c
  void test_name_ok(void)
  /* "song.mp3" true, "우사기 아침.mp3" true(UTF-8) */

  void test_path_rejects_traversal(void)   /* Review Focus 4 */
  /* audio_name_ok: "", "../x.mp3", "a/b.mp3", "a\\b.mp3", "..", ".hidden.mp3" 모두 false;
     audio_path_join(out, 128, "/sd/sounds", "../boot.mp3") → false, out[0] == '\0' */

  void test_path_join(void)
  /* ("/sd/sounds", "a.mp3") → "/sd/sounds/a.mp3"; 결과가 n을 넘으면 false */

  void test_is_mp3_name(void)
  /* "a.mp3" true, "B.MP3" true, "c.Mp3" true, "a.wav" false, "mp3" false, ".a.mp3" false, "a.mp3.txt" false */

  void test_sort_names(void)
  /* {"02_na.mp3","01_u.mp3","10_x.mp3"} → {"01_u.mp3","02_na.mp3","10_x.mp3"} */
  ```

- [ ] **Step 2: 실행해서 실패 확인**
  Run(WSL): `./tools/run_host_tests.sh` → FAIL

- [ ] **Step 3: `audio_files.c` 구현** (정렬은 `qsort` + `strcmp`)

- [ ] **Step 4: 통과 확인**
  Run(WSL): `./tools/run_host_tests.sh` → 전부 PASS

- [ ] **Step 5: `sdcard_esp.c` 구현**
  SPI3(`SPI3_HOST`) 버스, `esp_vfs_fat_sdspi_mount`(최대 파일 4개, 할당 단위 16KB, `format_if_mount_failed = false`), SD 클럭 20MHz **[제안, 배선이 길어 오류가 나면 10MHz]**. `sdkconfig.defaults`에 `CONFIG_FATFS_LFN_HEAP=y`, `CONFIG_FATFS_MAX_LFN=255`, `CONFIG_FATFS_API_ENCODING_UTF_8=y`, `CONFIG_FATFS_CODEPAGE_949=y`(한글 파일 이름).
  콘솔 명령 추가: `sd`(마운트 상태, 용량, 남은 공간), `ls <dir>`(예: `ls /sd/sounds`).

- [ ] **Step 6: [실기] 마운트와 목록 확인**
  Run: `idf.py -p <포트> flash monitor` → `usagi> ls /sd/sounds`, `usagi> ls /sd/clips/default`
  Expected: 준비 작업에서 넣은 파일이 정렬돼 나온다. 한글 파일 이름도 깨지지 않는다. 카드를 빼고 부팅하면 `sd: no card` 경고만 나오고 나머지는 정상 부팅한다.

- [ ] **Step 7: Commit**

  ```bash
  git add components/audio sdkconfig.defaults main test/host/test_audio_files.c
  git commit -m "feat(audio): SD카드 마운트(SPI3)와 파일 이름 규칙"
  ```

---

### Task 2: MP3 스트림 해독 (minimp3)

**작성:** Claude.

**Files:**
- Create: `components/audio/third_party/minimp3.h`, `components/audio/third_party/minimp3_impl.c`, `components/audio/third_party/LICENSE-minimp3`, `components/audio/include/mp3_stream.h`, `components/audio/mp3_stream.c`, `test/host/fixtures/make_fixtures.sh`, `test/host/fixtures/*.mp3`
- Modify: `components/audio/audio_tone.c`, `components/audio/include/audio.h`, `components/audio/CMakeLists.txt`, `test/host/CMakeLists.txt`, `tools/run_host_tests.sh`
- Test: `test/host/test_mp3_stream.c`, `test/host/test_audio_tone.c`

**Interfaces:**
- Produces (`mp3_stream.h`, 순수):

  ```c
  typedef size_t (*mp3_read_fn)(void *ctx, uint8_t *buf, size_t n);   /* 0 = 파일 끝, 읽기 오류도 0 */

  typedef enum { MP3_OK = 0, MP3_EOF, MP3_ERR_NO_FRAMES } mp3_status_t;

  typedef struct {
      /* 구현자가 정함. minimp3의 mp3dec_t, 입력 버퍼(MP3_IN_BUF = 4096바이트),
         해독된 프레임 버퍼(최대 1152×2 샘플), 남은 샘플 위치, 상태를 담는다. 동적 할당 금지 */
      ...
  } mp3_stream_t;

  void         mp3_stream_init(mp3_stream_t *s, mp3_read_fn read, void *ctx);
  /* 모노 int16을 최대 max개 채운다. 스테레오는 (L+R)/2. 반환 0이면 mp3_stream_status로 이유를 본다 */
  size_t       mp3_stream_read(mp3_stream_t *s, int16_t *out, size_t max);
  int          mp3_stream_sample_rate(const mp3_stream_t *s);   /* 첫 프레임 전에는 0 */
  mp3_status_t mp3_stream_status(const mp3_stream_t *s);
  ```
  판정 규칙: 처음부터 또는 마지막 정상 프레임 이후 **16KB**를 읽는 동안 프레임을 하나도 못 찾으면 `MP3_ERR_NO_FRAMES`. 정상 프레임을 하나라도 낸 뒤 입력이 끝나면 `MP3_EOF`, 하나도 못 낸 채 끝나면 `MP3_ERR_NO_FRAMES`. ID3v2 태그(`"ID3"` + 싱크세이프 크기 10바이트 헤더)는 첫 읽기에서 건너뛴다.
- Produces (`audio.h`, 순수 추가):

  ```c
  /* buf의 샘플을 volume(0..100, 초과는 100)에 맞춰 줄인다. 볼륨 100에서 최대 진폭은 AUDIO_PEAK_MAX */
  void audio_apply_volume(int16_t *buf, size_t n, uint8_t volume);
  ```

- [ ] **Step 1: minimp3 가져오기와 빌드 설정**
  [lieff/minimp3](https://github.com/lieff/minimp3)의 `minimp3.h`를 `third_party/`에 복사하고, 가져온 커밋 해시를 `LICENSE-minimp3` 맨 위에 적는다. `minimp3_impl.c`는 `#define MINIMP3_IMPLEMENTATION` + `#include "minimp3.h"`만 둔다.
  - PC 테스트: `test/host/CMakeLists.txt`에 `add_library(minimp3 STATIC .../third_party/minimp3_impl.c)`(경고를 오류로 보지 않음), `usagi_core`가 링크. include에 `third_party` 추가. `target_compile_definitions(... FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")`.
  - `tools/run_host_tests.sh`: gcovr에 `--exclude '.*/third_party/.*'`, cppcheck 대상 `find`에 `-not -path '*/third_party/*'`. (`test/host/CMakeLists.txt`의 `components/*/*.c` 글롭은 하위 폴더를 보지 않으므로 `minimp3_impl.c`는 위 별도 라이브러리로만 들어간다.)
  - 펌웨어: `components/audio/CMakeLists.txt` SRCS에 `third_party/minimp3_impl.c`, 그 파일만 `-Wno-error`.

- [ ] **Step 2: 테스트용 MP3 만들기** (`make_fixtures.sh`, WSL에서 1회 실행 후 결과를 커밋)

  ```bash
  ffmpeg -y -f lavfi -i "sine=frequency=440:duration=1" -ar 44100 -ac 1 -b:a 64k tone440_44k_mono.mp3
  ffmpeg -y -f lavfi -i "sine=frequency=440:duration=1" -ar 44100 -ac 2 -b:a 128k -metadata title=usagi-test -id3v2_version 3 tone440_44k_stereo_id3.mp3
  ffmpeg -y -f lavfi -i "sine=frequency=1000:duration=2" -ar 22050 -ac 1 -b:a 32k tone1k_22k_mono_2s.mp3
  head -c 6000 tone440_44k_mono.mp3 > truncated.mp3 && head -c 3000 /dev/zero | tr '\0' 'Z' >> truncated.mp3
  printf 'this is not an mp3 file\n%.0s' $(seq 1 1000) > not_mp3.mp3
  ```
  Expected: 다섯 파일 모두 30KB 이하

- [ ] **Step 3: 실패하는 테스트 작성** (`test_mp3_stream.c`, 파일 읽기는 `fread`를 감싼 `mp3_read_fn`)

  ```c
  void test_mono_44k_decodes_one_second(void)
  /* tone440_44k_mono: sample_rate 44100, 전체 샘플 수 44100 이상 47000 이하, status MP3_EOF */

  void test_frequency_preserved(void)
  /* 같은 파일 앞 44100샘플에서 음→양 교차 횟수 435~445 */

  void test_stereo_with_id3_downmixed(void)
  /* tone440_44k_stereo_id3: sample_rate 44100, 샘플 수 44100~47000, 최대 진폭이 mono 파일의 80~120% */

  void test_other_sample_rate(void)
  /* tone1k_22k_mono_2s: sample_rate 22050, 샘플 수 44100~47000, 교차 횟수(앞 22050샘플) 995~1005 */

  void test_small_reads_equal_big_read(void)
  /* mono 파일을 max=7로 반복해서 읽은 결과 == max=4096으로 읽은 결과 */

  void test_not_mp3_reports_error(void)          /* Review Focus 1 */
  /* not_mp3: 첫 read가 0, status MP3_ERR_NO_FRAMES, sample_rate 0 */

  void test_truncated_file_ends_without_hang(void)  /* Review Focus 1 */
  /* truncated: 반복 read가 1000번 안에 0을 반환하며 끝남, 그 전까지 샘플 10000개 이상 나옴 */
  ```
  `test_audio_tone.c`에 추가:

  ```c
  void test_apply_volume(void)
  /* {32767, -32768, 1000} volume 100 → |값| <= AUDIO_PEAK_MAX, 첫 값 >= AUDIO_PEAK_MAX*99/100;
     volume 0 → 전부 0; volume 200 == volume 100 결과 */
  ```

- [ ] **Step 4: 실행해서 실패 확인**
  Run(WSL): `./tools/run_host_tests.sh` → FAIL

- [ ] **Step 5: `mp3_stream.c`, `audio_apply_volume` 구현**
  입력 버퍼에 남은 바이트가 `MP3_IN_BUF/2` 미만이면 앞으로 당겨 채운 뒤 `mp3dec_decode_frame`. 반환 샘플 0이고 `frame_bytes > 0`이면 건너뛴 쓰레기 바이트로 세고, 둘 다 0이면 입력을 더 읽는다.

- [ ] **Step 6: 통과 확인**
  Run(WSL): `./tools/run_host_tests.sh` → 전부 PASS, 커버리지 90% 이상(third_party 제외), cppcheck 통과

- [ ] **Step 7: Commit**

  ```bash
  git add components/audio test/host tools/run_host_tests.sh
  git commit -m "feat(audio): minimp3 스트림 해독과 볼륨 적용"
  ```

---

### Task 3: 플레이어 결정 로직 (소리 실패 대비)

**작성:** Claude.

**Files:**
- Create: `components/audio/include/audio_player.h`, `components/audio/audio_player.c`
- Test: `test/host/test_audio_player.c`

**Interfaces:**
- Consumes: Task 1의 `audio_name_ok`, `audio_path_join`
- Modify (`audio.h`): 계획 2에서 ESP 부분에 있던 `audio_op_t`와 `audio_cmd_t`를 `#ifdef ESP_PLATFORM` 밖(순수 부분)으로 옮기고, `audio_cmd_t`에 이름 필드를 더한다. `audio.h`는 `audio_files.h`를 include한다.

  ```c
  typedef struct {
      audio_op_t op;
      uint8_t    volume;
      char       name[AUDIO_NAME_MAX];  /* ALARM_LOOP: 노래 이름, PRE_TICK: 클립 세트 이름, 그 외 무시 */
  } audio_cmd_t;
  ```
- Produces (`audio_player.h`, 순수):

  ```c
  typedef enum {
      PLAY_NONE = 0,
      PLAY_SONG,          /* path의 MP3를 끝나면 처음부터 반복 */
      PLAY_CLIP,          /* 클립 캐시의 clip_index번째를 한 번 */
      PLAY_BEEP_LOOP,     /* 계획 2의 본 알람 비프 반복 */
      PLAY_BEEP_TICK,     /* 계획 2의 짧은 비프 한 번 */
      PLAY_CONFIRM,       /* 계획 2의 확인음 */
  } play_kind_t;

  typedef struct {
      play_kind_t kind;
      uint8_t     volume;
      char        path[AUDIO_PATH_MAX];  /* PLAY_SONG일 때만 */
      uint8_t     clip_index;            /* PLAY_CLIP일 때만 */
  } play_action_t;

  /* 재생 결정에 필요한 바깥 상태. audio_esp.c가 채워 준다 */
  typedef struct {
      bool        sd_ok;
      bool        song_exists;     /* 요청한 노래 파일이 있는지 */
      const char *first_song;      /* /sd/sounds 첫 곡 이름, 없으면 NULL */
      size_t      n_clips;         /* 요청한 클립 세트의 캐시된 클립 수 */
  } play_env_t;

  typedef struct { /* 구현자가 정함: 다음 클립 번호, 현재 재생 중인 것 */ ... } audio_player_t;

  void          audio_player_init(audio_player_t *p);
  play_action_t audio_player_on_cmd(audio_player_t *p, const audio_cmd_t *cmd, const play_env_t *env);
  /* 지금 재생 중인 소스가 끝났거나(error=false) 실패했을 때(error=true) 다음에 할 일 */
  play_action_t audio_player_on_end(audio_player_t *p, bool error);
  ```
  결정 규칙:

  | 상황 | 결과 |
  |---|---|
  | `STOP` | `PLAY_NONE`, 클립 번호 0으로 |
  | `ALARM_LOOP`, SD 정상, 요청 곡 있음 | `PLAY_SONG`(`/sd/sounds/<이름>`) |
  | `ALARM_LOOP`, 요청 곡이 없거나 이름이 규칙 위반, 첫 곡 있음 | `PLAY_SONG`(첫 곡) |
  | `ALARM_LOOP`, SD 없음 또는 곡이 하나도 없음 | `PLAY_BEEP_LOOP` |
  | 곡 재생 중 `on_end(false)` | 같은 `PLAY_SONG`(처음부터 반복) |
  | 곡 재생 중 `on_end(true)` | `PLAY_BEEP_LOOP`(같은 회차에서는 다시 곡을 시도하지 않음) |
  | `PRE_TICK`, 클립 1개 이상 | `PLAY_CLIP`, 번호는 0,1,…,n-1,0,… (STOP 때 0으로) |
  | `PRE_TICK`, 클립 0개 | `PLAY_BEEP_TICK` |
  | 클립·비프 틱·확인음 `on_end` | `PLAY_NONE` |
  | `CONFIRM` | `PLAY_CONFIRM` |

- [ ] **Step 1: 실패하는 테스트 작성** (`test_audio_player.c`)

  ```c
  void test_song_plays_requested_file(void)
  /* sd_ok, song_exists, cmd ALARM_LOOP name "a.mp3" vol 70 → PLAY_SONG, path "/sd/sounds/a.mp3", volume 70 */

  void test_missing_song_uses_first_song(void)     /* Review Focus 3 */
  /* song_exists false, first_song "01.mp3" → PLAY_SONG "/sd/sounds/01.mp3"; 이름 "../x.mp3"도 같은 결과 */

  void test_empty_song_name_uses_first_song(void)
  /* name "" → 첫 곡 */

  void test_no_songs_uses_beep_loop(void)          /* Review Focus 3 */
  /* sd_ok, first_song NULL, song_exists false → PLAY_BEEP_LOOP, volume 유지 */

  void test_sd_missing_uses_beep_loop(void)        /* Review Focus 2 */
  /* sd_ok false → PLAY_BEEP_LOOP */

  void test_song_end_loops(void)
  /* PLAY_SONG 뒤 on_end(false) → 같은 path의 PLAY_SONG */

  void test_song_error_falls_back_to_beep_loop(void)   /* Review Focus 1 */
  /* PLAY_SONG 뒤 on_end(true) → PLAY_BEEP_LOOP; 이어서 on_end(false) → PLAY_BEEP_LOOP(비프도 반복) */

  void test_sd_removed_during_song_falls_back(void)    /* Review Focus 2 */
  /* PLAY_SONG 뒤 on_end(true) (읽기 오류) → PLAY_BEEP_LOOP */

  void test_pre_ticks_cycle_clips(void)
  /* n_clips 2: PRE_TICK 다섯 번 → clip_index 0,1,0,1,0; STOP 뒤 PRE_TICK → 0 */

  void test_pre_tick_without_clips_uses_beep_tick(void)  /* Review Focus 5 */
  /* n_clips 0 → PLAY_BEEP_TICK */

  void test_clip_end_is_silence(void)
  /* PLAY_CLIP 뒤 on_end(false) → PLAY_NONE */

  void test_stop_and_confirm(void)
  /* STOP → PLAY_NONE; CONFIRM vol 80 → PLAY_CONFIRM, volume 80 */
  ```

- [ ] **Step 2: 실행해서 실패 확인** → FAIL
- [ ] **Step 3: `audio_player.c` 구현** (위 결정 규칙 표 그대로)
- [ ] **Step 4: 통과 확인** → 전부 PASS, 커버리지 90% 이상
- [ ] **Step 5: Commit**

  ```bash
  git add components/audio test/host/test_audio_player.c
  git commit -m "feat(audio): 재생 결정 로직과 비프음 대체 규칙"
  ```

---

### Task 4: 클립 캐시

**작성:** Claude.

**Files:**
- Create: `components/audio/include/clip_cache.h`, `components/audio/clip_cache.c`
- Test: `test/host/test_clip_cache.c`

**Interfaces:**
- Consumes: Task 2의 `mp3_stream_t`
- Produces (`clip_cache.h`, 순수):

  ```c
  #define CLIPSET_MAX        16
  #define CLIP_CACHE_SAMPLES (16 * 48000)

  typedef struct {
      int16_t *pcm;                  /* 바깥에서 준 버퍼(펌웨어는 PSRAM, 테스트는 정적 배열) */
      size_t   capacity;             /* 샘플 수 */
      size_t   used;
      char     set_name[32];         /* 지금 담긴 세트, 비어 있으면 없음 */
      uint8_t  count;
      size_t   offset[CLIPSET_MAX];
      size_t   length[CLIPSET_MAX];  /* 샘플 수, 최대 1초(= rate) */
      int      rate[CLIPSET_MAX];
  } clip_cache_t;

  void clip_cache_init(clip_cache_t *c, int16_t *pcm, size_t capacity);
  void clip_cache_clear(clip_cache_t *c, const char *set_name);
  /* 스트림을 해독해 한 클립으로 넣는다. 1초(첫 프레임의 샘플레이트 기준)를 넘는 부분은 버린다.
     해독 실패, 클립 수 초과, 버퍼 부족이면 false(이미 넣은 클립은 유지) */
  bool clip_cache_add(clip_cache_t *c, mp3_stream_t *s);
  ```

- [ ] **Step 1: 실패하는 테스트 작성** (`test_clip_cache.c`, 버퍼는 `static int16_t buf[3 * 48000]`)

  ```c
  void test_add_short_clip(void)
  /* tone440_44k_mono(1초 남짓) 추가 → true, count 1, rate[0] 44100, length[0] == 44100 */

  void test_clip_truncated_to_one_second(void)    /* Review Focus 5 */
  /* tone1k_22k_mono_2s 추가 → length == 22050 (2초 중 1초만) */

  void test_offsets_are_packed(void)
  /* 클립 두 개 추가 → offset[1] == length[0], used == length[0] + length[1] */

  void test_bad_clip_rejected(void)
  /* not_mp3 추가 → false, count 변화 없음 */

  void test_buffer_full_rejected(void)
  /* capacity 50000으로 init, 1초 클립 두 번 → 첫 번째 true, 두 번째 false, count 1 */

  void test_clear_resets(void)
  /* 추가 후 clear("other") → count 0, used 0, set_name "other" */
  ```

- [ ] **Step 2: 실행해서 실패 확인** → FAIL
- [ ] **Step 3: `clip_cache.c` 구현**
- [ ] **Step 4: 통과 확인** → 전부 PASS
- [ ] **Step 5: Commit**

  ```bash
  git add components/audio test/host/test_clip_cache.c
  git commit -m "feat(audio): 미리 알림 클립 캐시(1초 자르기)"
  ```

---

### Task 5: 오디오 출력 통합 (I2S, MP3, 클립, 비프 대체)

**작성:** Claude. 실기 확인은 사용자.

**Files:**
- Modify: `components/audio/include/audio.h`, `components/audio/audio_esp.c`, `main/alarm_service_esp.c`, `main/dev_console_esp.c`, `main/main.c`

**Interfaces:**
- Consumes: Task 1~4 전부, 계획 2의 `audio_tone_fill`
- Uses: Task 3에서 이름 필드를 더한 `audio_cmd_t`. `alarm_service`는 `MAIN_START`일 때 대표 알람의 `song`, `PRE_CLIP`일 때 `clipset`을 넣는다(알람이 목록에서 지워졌으면 빈 값 → 기본값).
- 오디오 작업 동작:
  1. 명령을 받으면 `play_env_t`를 채운다. `sd_ok = sdcard_ensure_mounted()`. 노래 존재는 `stat`, 첫 곡은 `sdcard_list_mp3("/sd/sounds", …, 1)`. `PRE_TICK`인데 캐시의 `set_name`이 요청 세트와 다르면 그 자리에서 캐시를 다시 채운다(세트 이름이 빈 값이면 `default`).
  2. `audio_player_on_cmd` 결과대로 소스를 바꾼다. 새 명령은 지금 소스를 즉시 끊는다(클립이 1초보다 길어도 다음 초 명령에서 끊김).
  3. 소스의 샘플레이트가 I2S 현재 값과 다르면 `i2s_channel_disable` → `i2s_channel_reconfig_std_clock` → `i2s_channel_enable`. 비프는 16kHz.
  4. 10ms 분량씩 채우고 `audio_apply_volume` 후 `i2s_channel_write`. `PLAY_SONG`은 `fread`를 감싼 `mp3_read_fn`으로 `mp3_stream_read`. `fread`가 오류(`ferror`)면 `sdcard_mark_failed()` 후 `audio_player_on_end(true)`.
  5. 소스가 끝나면 `audio_player_on_end`. 아무것도 없으면 0을 보낸다.
- I2S DMA: `dma_desc_num = 8`, `dma_frame_num = 576`(44.1kHz에서 약 100ms 여유). SD 읽기가 잠깐 늦어도 소리가 끊기지 않게 하기 위해서다.
- 콘솔 명령 추가: `alarm song <id> <name>`, `alarm clips <id> <set>`, `play <name>`(노래를 볼륨 50으로 10초 재생, 확인용), `stop`.

- [ ] **Step 1: 구현**
- [ ] **Step 2: PC 테스트와 빌드**
  Run(WSL): `./tools/run_host_tests.sh` → 전부 PASS
  Run: `idf.py build` → `Project build complete`

- [ ] **Step 3: [실기] 노래 재생과 볼륨**
  `usagi> play <곡 이름>` → 노래가 끊김 없이 들린다. 다른 샘플레이트 곡(44.1kHz와 48kHz)을 번갈아 재생해도 음 높이가 맞다. 해독 시간 로그(프레임당 평균 µs)를 남기고 CPU 사용률이 30% 미만인지 본다 **[실물 확인]**.

- [ ] **Step 4: [실기] 알람 시나리오** (계획 2 Task 7 Step 4 방식으로 시각은 그때에 맞춘다)
  1. `alarm add 1 <3분 뒤> 7f 2 TEST` + `alarm song 1 <곡>` → 미리 알림 2분 동안 매초 "우" → "나" → "우" … 정각에 노래 반복
  2. 짧게 누름 → 스누즈 → 5분 뒤 노래가 처음부터 다시
  3. 길게 누름 → 확인음, 정지
  4. 지정 곡을 PC에서 지운 SD로 다시 시험 → 첫 번째 곡으로 울림
  5. 노래 재생 중 SD카드를 뽑음 → 1초 안에 비프 반복으로 바뀌고 계속 울림
  6. SD카드 없이 부팅해서 알람 → 미리 알림은 짧은 비프, 정각에는 비프 반복
  7. `/clips/default`에 2초짜리 클립 하나만 넣고 시험 → 매초 클립 앞 1초만 들림
  Expected: 1~7이 설명대로 동작. 5·6이 가장 중요하다(설계 목표 1).

- [ ] **Step 5: [실기] 로직 분석기로 I2S 확인**
  BCLK·WS·DIN 캡처. 44.1kHz 곡 재생 중 WS 주기가 22.7µs인지, 16kHz 비프로 바뀔 때 62.5µs로 바뀌는지 본다.

- [ ] **Step 6: Commit + push, CI 녹색 확인**

  ```bash
  git add components/audio main
  git commit -m "feat(audio): MP3 알람·클립 미리 알림·비프음 대체 통합"
  git push
  ```

---

## 계획 3 완료 기준

- Task 5 Step 4의 시나리오 1~7이 실기에서 통과한다(특히 5·6: SD 문제에도 알람이 울림).
- PC 테스트 전부 통과, 순수 C 줄 커버리지 90% 이상(third_party 제외), cppcheck 경고 0, GitHub Actions 녹색
- 저장소에 들어간 MP3는 `test/host/fixtures/`의 사인파뿐이다.
- 다음 단계: 계획 4(Wi-Fi, SNTP, 웹 설정, NVS 저장, 야간 밝기, 업로드, 캡티브 포털, 웹 펌웨어 업데이트)를 작성한다.
