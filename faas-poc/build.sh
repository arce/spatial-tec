#!/usr/bin/env bash
# Compila un comando de Spatial TEC a WebAssembly (Emscripten) -> faas-poc/wasm/
# Requiere Emscripten activo (emcc/em++ en el PATH, p. ej. `source emsdk_env.sh`).
# Uso:  faas-poc/build.sh [comando]      (por defecto: spatial_info)
set -euo pipefail
CMD="${1:-spatial_info}"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
# PascalCase para el nombre de la fábrica JS: spatial_info -> createSpatialInfo
FACTORY="create$(echo "$CMD" | sed -E 's/(^|_)([a-z])/\U\2/g')"

em++ -std=c++17 -O2 -fexceptions "$ROOT/src/$CMD.cpp" -o "$HERE/wasm/$CMD.mjs" \
  -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME="$FACTORY" \
  -sINVOKE_RUN=0 -sEXIT_RUNTIME=0 \
  -sFORCE_FILESYSTEM=1 -sEXPORTED_RUNTIME_METHODS=callMain,FS \
  -sALLOW_MEMORY_GROWTH=1 -sENVIRONMENT=web,worker,node
# -fexceptions: el código usa try/catch alrededor de std::stod (flujo normal, no errores).
ls -la "$HERE/wasm/$CMD".*
