#!/usr/bin/env bash
set -euo pipefail
task_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ "$(uname -s)" != Darwin || "$(uname -m)" != arm64 ]]; then
    printf '%s\n' 'MafiaNative requires native Apple Silicon macOS. Run this script on your Mac.' >&2
    exit 2
fi
task_build="${MAFIA_BUILD_DIR:-$task_root/build/macos}"
task_deps="${MAFIA_DEPS_DIR:-$task_root/.deps}"
task_generated="${MAFIA_GENERATED_DIR:-$task_root/.work/generated}"
task_cmake="${CMAKE:-cmake}"
task_config="${MAFIA_BUILD_CONFIG:-RelWithDebInfo}"
"${PYTHON:-python3}" "$task_root/tools/verify_upstream.py" "$task_deps/PS2Recomp"
"$task_cmake" -S "$task_root" -B "$task_build" -C "$task_deps/fetchcontent.cmake" \
    -DCMAKE_BUILD_TYPE="$task_config" -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DMAFIA_BUILD_RUNTIME=ON -DMAFIA_ENABLE_FFMPEG=ON \
    -DMAFIA_PS2RECOMP_SOURCE_DIR="$task_deps/PS2Recomp" \
    -DMAFIA_GENERATED_DIR="$task_generated" "$@"
"$task_cmake" --build "$task_build" --config "$task_config" --target MafiaNative --parallel "${MAFIA_BUILD_JOBS:-2}"
task_binary="$("${PYTHON:-python3}" "$task_root/tools/resolve_build_path.py" "$task_build" "$task_config")"
task_app="${task_binary%/Contents/MacOS/MafiaNative}"
if [[ "$(lipo -archs "$task_binary")" != arm64 ]]; then
    printf '%s\n' 'Produced binary is not exclusively ARM64.' >&2
    exit 1
fi
printf 'Built %s\n' "$task_app"
printf '%s\n' 'This is a developer app: dynamic FFmpeg libraries must be installed locally.'
