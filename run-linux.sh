#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GAME="$ROOT/recomp/build/full"
if [[ ! -x "$GAME/recomp_main" ]]; then
    echo "Build the Linux game first (see LINUX.md)." >&2
    exit 1
fi
cd "$GAME"
export RECOMP_SHADERS_ON_DEMAND="${RECOMP_SHADERS_ON_DEMAND:-0}"
case " $* " in
    *" --precompile "*|*" --save-dir "*) exec ./recomp_main "$@" ;;
esac
if [[ "${RECOMP_SKIP_PRECACHE:-0}" != 1 ]]; then
    echo "Checking Vulkan pipeline cache..."
    if ! ./recomp_main --precompile > precompile-linux.log 2>&1; then
        cat precompile-linux.log >&2
        exit 1
    fi
fi
exec ./recomp_main "$@"
