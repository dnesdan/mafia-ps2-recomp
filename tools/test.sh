#!/usr/bin/env bash
set -euo pipefail
task_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
task_build="${MAFIA_TEST_BUILD_DIR:-$task_root/build/tests}"
task_python="${PYTHON:-python3}"
task_cmake="${CMAKE:-cmake}"
task_config="${MAFIA_TEST_CONFIG:-Debug}"
"$task_python" -m unittest discover -s "$task_root/tests/python" -p 'test_*.py' -v
"$task_cmake" -S "$task_root" -B "$task_build" -DCMAKE_BUILD_TYPE="$task_config" -DBUILD_TESTING=ON -DMAFIA_BUILD_RUNTIME=OFF
"$task_cmake" --build "$task_build" --config "$task_config" --parallel "${MAFIA_BUILD_JOBS:-2}"
"${CTEST:-ctest}" --test-dir "$task_build" -C "$task_config" --output-on-failure
