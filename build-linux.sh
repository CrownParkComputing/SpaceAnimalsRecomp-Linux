#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATA="${RECOMP_DATA_ROOT:-$ROOT}"
JOBS="${JOBS:-$(nproc)}"
export RECOMP_CXX="${RECOMP_CXX:-clang++}"
: "${RECOMP_NUP:?Set RECOMP_NUP to your Star Fox 2.0.0 .nup file}"

for tool in python3 "$RECOMP_CXX" g++ ld.lld glslangValidator pkg-config; do
    command -v "$tool" >/dev/null || { echo "Missing tool: $tool" >&2; exit 1; }
done
pkg-config --exists sdl3 vulkan
python3 -c 'import capstone, lz4.block, zstandard'
[[ -f "$RECOMP_NUP" ]] || { echo "NUP not found: $RECOMP_NUP" >&2; exit 1; }
for dir in work extracted; do
    [[ -d "$DATA/$dir" ]] || { echo "Missing extracted inputs: $DATA/$dir" >&2; exit 1; }
    if [[ ! -e "$ROOT/$dir" && ! -L "$ROOT/$dir" ]]; then
        ln -s "$(realpath "$DATA/$dir")" "$ROOT/$dir"
    fi
done
python3 "$ROOT/recomp/fullbuild.py" subsdk3 subsdk0 subsdk1 subsdk2 sdk main -j "$JOBS"
python3 "$ROOT/recomp/link.py" -O 2 -j "${RUNTIME_JOBS:-4}"
python3 "$ROOT/recomp/shaders/romfs_shaders.py" --nup "$RECOMP_NUP"
python3 "$ROOT/recomp/shaders/shaderc.py" -j "${SHADER_JOBS:-4}"
echo "Linux build complete. Run: $ROOT/run-linux.sh"
