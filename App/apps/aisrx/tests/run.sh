#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
task_tmp=$(mktemp -d)
trap 'rm -rf "$task_tmp"' EXIT
flags=(-std=c11 -Wall -Wextra -Werror -fno-builtin -fsanitize=address,undefined)
cc "${flags[@]}" tests/unit_test.c -o "$task_tmp/unit"
"$task_tmp/unit"
cc "${flags[@]}" tests/app_test.c -o "$task_tmp/app"
"$task_tmp/app"
cc "${flags[@]}" tests/decoder_test.c -o "$task_tmp/replay"
"$task_tmp/replay" tests/long-beach.s8
"$task_tmp/replay" tests/helsinki.s8
python3 tests/verify_replay.py "$task_tmp/replay"
