#!/usr/bin/env bash
set -e

COMMON_FLAGS=(
    -std=c++17
    -O3
    -sMODULARIZE=1
    -sINCOMING_MODULE_JS_API=arguments,print,printErr,locateFile
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_HEAP=268435456
)

em++ "tchisel_optimized.cpp" \
    "${COMMON_FLAGS[@]}" \
    -sEXPORT_NAME=createTchiselIntegerModule \
    -o solver_integer.js

em++ "tchisel_rational_optimized(3).cpp" \
    "${COMMON_FLAGS[@]}" \
    -sEXPORT_NAME=createTchiselRationalModule \
    -o solver_rational.js

em++ "tchisel_multiradical(2).cpp" \
    "${COMMON_FLAGS[@]}" \
    -sEXPORT_NAME=createTchiselMultiradicalModule \
    -o solver_multiradical.js

echo "Done. Generated:"
echo "  solver_integer.js / solver_integer.wasm"
echo "  solver_rational.js / solver_rational.wasm"
echo "  solver_multiradical.js / solver_multiradical.wasm"