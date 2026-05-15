#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/time_persistence"
BINARY="$BUILD_DIR/TimePersistenceTest"
mkdir -p "$BUILD_DIR"
SOURCES=(
  "$ROOT_DIR/test/time_persistence/test_time_persistence.cpp"
  "$ROOT_DIR/src/TimePersistenceStore.cpp"
)
CXXFLAGS=(-std=c++20 -O2 -Wall -Wextra -pedantic -I"$ROOT_DIR")
c++ "${CXXFLAGS[@]}" "${SOURCES[@]}" -o "$BINARY"
"$BINARY" "$@"
