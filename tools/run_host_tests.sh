#!/usr/bin/env bash
# PC 단위 테스트를 한 번에 돌린다: 빌드 → ctest → 커버리지(gcovr) → 정적 분석(cppcheck)
# 하나라도 실패하면 0이 아닌 값으로 끝난다.
#
# 환경 변수
#   SKIP_COVERAGE_GATE=1  커버리지 90% 기준을 건너뛴다(측정할 코드가 아직 없는 Task 1 전용)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build-host"
cd "$ROOT"

echo "== 1/4 빌드"
cmake -S test/host -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DUSAGI_COVERAGE=ON
cmake --build "$BUILD"

echo "== 2/4 테스트"
# 이전 실행의 커버리지 카운터를 지운다
find "$BUILD" -name '*.gcda' -delete
ctest --test-dir "$BUILD" --output-on-failure

echo "== 3/4 커버리지"
mkdir -p "$BUILD/coverage"
GCOVR_ARGS=(-r "$ROOT" --object-directory "$BUILD" --filter 'components/'
    --html-details "$BUILD/coverage/index.html" --print-summary)
if [[ "${SKIP_COVERAGE_GATE:-0}" != "1" ]]; then
    GCOVR_ARGS+=(--fail-under-line 90)
else
    echo "(SKIP_COVERAGE_GATE=1: 90% 기준 건너뜀)"
fi
gcovr "${GCOVR_ARGS[@]}"

echo "== 4/4 정적 분석"
mapfile -t C_FILES < <(find components/alarm_core components/button -name '*.c' | sort)
if [[ ${#C_FILES[@]} -eq 0 ]]; then
    echo "(검사할 .c 파일이 아직 없음: 건너뜀)"
else
    cppcheck --enable=warning,style,performance --error-exitcode=1 --std=c11 \
        --inline-suppr --suppress=missingIncludeSystem --quiet \
        -I components/alarm_core/include -I components/button/include \
        "${C_FILES[@]}"
fi

echo "== 모두 통과"
