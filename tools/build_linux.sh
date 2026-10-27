#!/usr/bin/env bash
set -euo pipefail
task_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ "$(uname -s)" != Linux ]]; then
    printf '%s\n' 'Run this script on Linux. Apple Silicon uses tools/build_macos.sh.' >&2
    exit 2
fi
case "$(uname -m)" in
    x86_64|aarch64|arm64) ;;
    *) printf '%s\n' 'The Linux host supports x86-64 with SSE4.1 or ARM64 with NEON.' >&2; exit 2 ;;
esac
task_build="${MAFIA_BUILD_DIR:-$task_root/build/linux}"
task_deps="${MAFIA_DEPS_DIR:-$task_root/.deps}"
task_source="${MAFIA_PS2RECOMP_SOURCE_DIR:-$task_deps/PS2Recomp}"
task_cache="${MAFIA_FETCHCONTENT_CACHE:-$task_deps/fetchcontent.cmake}"
task_generated="${MAFIA_GENERATED_DIR:-$task_root/.work/generated}"
task_cmake="${CMAKE:-cmake}"
task_config="${MAFIA_BUILD_CONFIG:-RelWithDebInfo}"
"${PYTHON:-python3}" "$task_root/tools/verify_upstream.py" "$task_source"
"$task_cmake" -S "$task_root" -B "$task_build" -C "$task_cache" \
    -DCMAKE_BUILD_TYPE="$task_config" \
    -DMAFIA_BUILD_RUNTIME=ON -DMAFIA_ENABLE_FFMPEG=ON \
    -DMAFIA_PS2RECOMP_SOURCE_DIR="$task_source" \
    -DMAFIA_GENERATED_DIR="$task_generated" "$@"
"$task_cmake" --build "$task_build" --config "$task_config" --target MafiaNative --parallel "${MAFIA_BUILD_JOBS:-2}"
task_binary="$("${PYTHON:-python3}" "$task_root/tools/resolve_build_path.py" "$task_build" "$task_config" --platform linux)"
printf 'Built %s\n' "$task_binary"
printf '%s\n' 'A desktop session is needed for the window; --headless and --capture-frame work without a display server.'
