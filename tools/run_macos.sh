#!/usr/bin/env bash
set -euo pipefail
task_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ "$(uname -s)" != Darwin || "$(uname -m)" != arm64 ]]; then
    printf '%s\n' 'Run this script on native Apple Silicon macOS.' >&2
    exit 2
fi
task_build="${MAFIA_BUILD_DIR:-$task_root/build/macos}"
task_config="${MAFIA_BUILD_CONFIG:-RelWithDebInfo}"
task_binary="$("${PYTHON:-python3}" "$task_root/tools/resolve_build_path.py" "$task_build" "$task_config")"
if [[ ! -x "$task_binary" ]]; then
    printf '%s\n' 'Build the app with tools/build_macos.sh first.' >&2
    exit 1
fi
if [[ $# -gt 0 ]]; then
    exec "$task_binary" "$@"
fi
task_work="${MAFIA_WORK_DIR:-$task_root/.work}"
task_elf="$("${PYTHON:-python3}" - "$task_work" <<'PY'
import json,pathlib,sys
root=pathlib.Path(sys.argv[1]).resolve()
disc=json.loads((root/'disc.json').read_text())
path=(root/'extracted'/disc['boot_elf']).resolve()
if not path.is_relative_to(root/'extracted'):
    raise SystemExit('Invalid boot path in disc report')
print(path)
PY
)"
task_args=(--elf "$task_elf" --data-root "$task_work/extracted" --report "$task_work/runtime-report.json")
if [[ -n "${MAFIA_PS2_ISO:-}" ]]; then
    task_args+=(--iso "$MAFIA_PS2_ISO")
fi
exec "$task_binary" "${task_args[@]}"
