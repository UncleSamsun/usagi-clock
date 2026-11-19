# PC(호스트) 단위 테스트. ESP-IDF 없이 일반 gcc로 순수 C 컴포넌트를 빌드한다.
#   cmake -S test/host -B build-host -G Ninja -DUSAGI_COVERAGE=ON
#   cmake --build build-host && ctest --test-dir build-host --output-on-failure
cmake_minimum_required(VERSION 3.22)
project(usagi_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(USAGI_COVERAGE "gcov 커버리지 계측을 켠다" OFF)

include(FetchContent)
FetchContent_Declare(
    unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG v2.6.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(unity)

# cJSON: 펌웨어는 espressif/cjson 컴포넌트, PC 테스트는 같은 원본을 직접 받는다
FetchContent_Declare(
    cjson_src
    GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
    GIT_TAG v1.7.18
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(cjson_src)
if(NOT cjson_src_POPULATED)
    FetchContent_Populate(cjson_src)   # cJSON의 CMake는 쓰지 않고 cJSON.c 하나만 빌드한다
endif()
add_library(cjson STATIC "${cjson_src_SOURCE_DIR}/cJSON.c")
target_include_directories(cjson PUBLIC "${cjson_src_SOURCE_DIR}")
target_compile_options(cjson PRIVATE -w)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# 테스트 대상: components/*/*.c 중 순수 C 파일 전부.
# 파일 규칙: ESP-IDF 헤더를 쓰는 파일은 이름이 _esp.c로 끝나고, PC 빌드에서 뺀다.
# 하위 폴더(third_party 등)는 글롭에 걸리지 않는다.
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS "${REPO_ROOT}/components/*/*.c")
list(FILTER CORE_SOURCES EXCLUDE REGEX "_esp\\.c$")
file(GLOB CORE_INCLUDES LIST_DIRECTORIES true "${REPO_ROOT}/components/*/include")

# 서드파티: 경고를 오류로 보지 않고, 커버리지·cppcheck 대상도 아니다
set(MINIMP3_DIR "${REPO_ROOT}/components/audio/third_party")
add_library(minimp3 STATIC "${MINIMP3_DIR}/minimp3_impl.c")
target_include_directories(minimp3 PUBLIC "${MINIMP3_DIR}")
target_compile_options(minimp3 PRIVATE -w)

add_library(usagi_core STATIC ${CORE_SOURCES})
target_include_directories(usagi_core PUBLIC ${CORE_INCLUDES})
target_compile_options(usagi_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(usagi_core PUBLIC minimp3 cjson m)   # m: sin, lrint (오디오)
if(USAGI_COVERAGE)
    target_compile_options(usagi_core PRIVATE --coverage -O0)
    target_link_options(usagi_core PUBLIC --coverage)
endif()

enable_testing()

# test_*.c 파일 하나가 실행 파일 하나, ctest 항목 하나가 된다.
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_*.c")

# MP3 테스트는 ffmpeg로 만든 사인파 파일(fixtures/make_fixtures.sh)이 있어야 돈다
set(FIXTURE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
if(NOT EXISTS "${FIXTURE_DIR}/tone440_44k_mono.mp3")
    message(WARNING "MP3 fixtures missing: run test/host/fixtures/make_fixtures.sh (needs ffmpeg). test_mp3_fixtures skipped")
    list(FILTER TEST_SOURCES EXCLUDE REGEX "test_mp3_fixtures\\.c$")
endif()

foreach(test_src ${TEST_SOURCES})
    get_filename_component(test_name "${test_src}" NAME_WE)
    add_executable(${test_name} "${test_src}")
    target_link_libraries(${test_name} PRIVATE usagi_core unity)
    target_compile_options(${test_name} PRIVATE -Wall -Wextra -Werror)
    target_compile_definitions(${test_name} PRIVATE FIXTURE_DIR="${FIXTURE_DIR}")
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
