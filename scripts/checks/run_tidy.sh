#!/usr/bin/env bash
# One clang-tidy process per file, fanned out across every core: a single
# process walks its whole file list in series and pins one core for minutes
# while the build lanes already run wide. The file list arrives on stdin,
# one path per line; any failing file fails the run.
set -euo pipefail

tidy="$1"
build_dir="$2"
jobs="$(nproc)"
xargs -P "$jobs" -n 1 "$tidy" --quiet -p "$build_dir"
