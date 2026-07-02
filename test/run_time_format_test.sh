#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/time_format"
BINARY="$BUILD_DIR/TimeFormatTest"
mkdir -p "$BUILD_DIR"
SOURCES=(
  "$ROOT_DIR/test/time_format/test_time_format.cpp"
  "$ROOT_DIR/src/services/TimeFormat.cpp"
)
CXXFLAGS=(-std=c++20 -O2 -Wall -Wextra -pedantic -I"$ROOT_DIR")
c++ "${CXXFLAGS[@]}" "${SOURCES[@]}" -o "$BINARY"
"$BINARY" "$@"
