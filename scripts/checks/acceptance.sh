#!/usr/bin/env bash
# visor acceptance: the check suite every batch must pass.
# Any failing step stops the script. Never report acceptance as passed
# with a check disabled, an exit code ignored, or a failing case skipped.
#
# Two lanes (2026-10-23): the default quick lane builds all three matrices
# and runs both static checks, but leaves the test binaries unexecuted --
# the judgment suites carry their own environments (a private Xvfb, a
# forked headless weston) and deserve one deliberate run, not one per
# iteration. --full runs everything, and a batch is finished only when the
# full lane is green; the quick lane is for the loops in between.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/../.."
check_jobs="${VISOR_CHECK_JOBS:-4}"
run_tests=0
if [[ "${1:-}" == "--full" ]]; then
    run_tests=1
    echo "lane: full (builds, checks and every judgment)"
else
    echo "lane: quick (builds and checks; judgments wait for --full)"
fi

run_lane() {
    local dir="$1"
    shift
    cmake "$@"
    cmake --build "$dir" --parallel "$check_jobs"
    if [[ "$run_tests" == "1" ]]; then
        ctest --test-dir "$dir" --output-on-failure
    fi
}

echo "== 1/6 rel: configure, build, test =="
run_lane build -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++

echo "== 2/6 asan/ubsan: configure, build, test =="
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    run_lane build-asan -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=g++ -DVISOR_SANITIZE=ON

echo "== 3/6 tsan: configure, build, test =="
TSAN_OPTIONS=halt_on_error=1 \
    run_lane build-tsan -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=g++ -DVISOR_TSAN=ON

echo "== 4/6 clang-format =="
cmake --build build --target visor.check-format

echo "== 5/6 clang-tidy =="
cmake --build build --target visor.check-tidy

echo "== 6/6 fontgen: standalone configure, build, test =="
cmake -S tools/fontgen -B build-fontgen -G Ninja
cmake --build build-fontgen --parallel "$check_jobs"
if [[ "$run_tests" == "1" ]]; then
    ctest --test-dir build-fontgen --output-on-failure
fi

if [[ "$run_tests" == "1" ]]; then
    echo "acceptance: all green (full lane)"
else
    echo "acceptance: quick lane green -- run --full before merging"
fi
