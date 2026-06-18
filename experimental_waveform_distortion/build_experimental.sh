#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD_DIR="$ROOT_DIR/build"
UF2_DIR="$ROOT_DIR/uf2"
UF2_NAME="Fr330hfr33-waveform-distortion-experimental.uf2"

cleanup()
{
    cmake -E remove_directory "$BUILD_DIR"
}

trap cleanup EXIT
cleanup
mkdir -p "$UF2_DIR"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --parallel
mv -f "$BUILD_DIR/Fr330hfr33Experimental.uf2" "$UF2_DIR/$UF2_NAME"

echo "Built $UF2_DIR/$UF2_NAME"
