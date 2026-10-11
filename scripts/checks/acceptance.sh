#!/usr/bin/env bash
# visor acceptance: the full check suite every batch must pass.
# Any failing step stops the script. Never report acceptance as passed
# with a check disabled, an exit code ignored, or a failing case skipped.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/../.."
check_jobs="${VISOR_CHECK_JOBS:-4}"

echo "== 1/6 rel: configure, build, test =="
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build --parallel "$check_jobs"
ctest --test-dir build --output-on-failure

echo "== 2/6 asan/ubsan: configure, build, test =="
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DVISOR_SANITIZE=ON
cmake --build build-asan --parallel "$check_jobs"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    ctest --test-dir build-asan --output-on-failure

echo "== 3/6 tsan: configure, build, test =="
cmake -S . -B build-tsan -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DVISOR_TSAN=ON
cmake --build build-tsan --parallel "$check_jobs"
TSAN_OPTIONS=halt_on_error=1 ctest --test-dir build-tsan --output-on-failure

echo "== 4/6 clang-format =="
cmake --build build --target visor.check-format

echo "== 5/6 clang-tidy =="
cmake --build build --target visor.check-tidy

echo "== 6/6 fontgen: standalone configure, build, test =="
cmake -S tools/fontgen -B build-fontgen -G Ninja
cmake --build build-fontgen --parallel "$check_jobs"
ctest --test-dir build-fontgen --output-on-failure

echo "acceptance: all green"
