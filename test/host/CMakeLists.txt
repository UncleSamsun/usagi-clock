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

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# 테스트 대상: 순수 C 컴포넌트의 .c 파일 전부
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS
    "${REPO_ROOT}/components/alarm_core/*.c"
    "${REPO_ROOT}/components/button/*.c"
)
set(CORE_INCLUDES
    "${REPO_ROOT}/components/alarm_core/include"
    "${REPO_ROOT}/components/button/include"
)

if(CORE_SOURCES)
    add_library(usagi_core STATIC ${CORE_SOURCES})
    target_include_directories(usagi_core PUBLIC ${CORE_INCLUDES})
    target_compile_options(usagi_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(USAGI_COVERAGE)
        target_compile_options(usagi_core PRIVATE --coverage -O0)
        target_link_options(usagi_core PUBLIC --coverage)
    endif()
else()
    # 아직 .c 파일이 없을 때(Task 1): 헤더만 제공
    add_library(usagi_core INTERFACE)
    target_include_directories(usagi_core INTERFACE ${CORE_INCLUDES})
endif()

enable_testing()

# test_*.c 파일 하나가 실행 파일 하나, ctest 항목 하나가 된다.
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_*.c")
foreach(test_src ${TEST_SOURCES})
    get_filename_component(test_name "${test_src}" NAME_WE)
    add_executable(${test_name} "${test_src}")
    target_link_libraries(${test_name} PRIVATE usagi_core unity)
    target_compile_options(${test_name} PRIVATE -Wall -Wextra -Werror)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
