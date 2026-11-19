#!/usr/bin/env bash
# Build the emulator into this folder and serve it for local testing.
#   emulator/local/build.sh          # build, then http://127.0.0.1:8765/
#   emulator/local/build.sh --no-serve
# Needs emcc/emcmake on PATH (brew install emscripten, or source emsdk_env.sh),
# managed_components/{lvgl__lvgl,espressif__cjson} and a firmware/sdkconfig
# (idf.py reconfigure, or gen_standin_sdkconfig.py here when ESP-IDF is absent).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
command -v emcmake >/dev/null || { echo "emcmake not found: brew install emscripten, or source <emsdk>/emsdk_env.sh" >&2; exit 1; }
emcmake cmake -S "$here/.." -B "$here/../build" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=MinSizeRel -DEMU_OUT_DIR="$here" >/dev/null
cmake --build "$here/../build" -j "$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
[ "${1:-}" = "--no-serve" ] && exit 0
echo "http://127.0.0.1:8765/?theme=3"
cd "$here" && exec python3 -m http.server 8765 --bind 127.0.0.1
