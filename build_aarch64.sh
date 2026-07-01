#!/bin/sh

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build/aarch64-static"
CXX="${CXX:-aarch64-linux-gnu-g++}"
READELF="${READELF:-aarch64-linux-gnu-readelf}"

if ! command -v "$CXX" >/dev/null 2>&1; then
  echo "Missing cross compiler: $CXX"
  echo "On Ubuntu, install it with:"
  echo "  sudo apt update"
  echo "  sudo apt install -y g++-aarch64-linux-gnu"
  exit 1
fi

mkdir -p "$BUILD_DIR"

COMMON_FLAGS="-std=c++17 -O2 -Wall -Wextra -static"

"$CXX" $COMMON_FLAGS -pthread \
  "$SCRIPT_DIR/tcp_timestamp_proxy.cpp" \
  -o "$BUILD_DIR/tcp_timestamp_proxy"

"$CXX" $COMMON_FLAGS \
  "$SCRIPT_DIR/stamped_frame_dump.cpp" \
  -o "$BUILD_DIR/stamped_frame_dump"

echo "Built:"
echo "  $BUILD_DIR/tcp_timestamp_proxy"
echo "  $BUILD_DIR/stamped_frame_dump"

if command -v file >/dev/null 2>&1; then
  file "$BUILD_DIR/tcp_timestamp_proxy" "$BUILD_DIR/stamped_frame_dump"
fi

if command -v "$READELF" >/dev/null 2>&1; then
  if "$READELF" -l "$BUILD_DIR/tcp_timestamp_proxy" | grep 'Requesting program interpreter'; then
    echo "Unexpected dynamic interpreter found. Static build failed."
    exit 1
  fi
  echo "Static build check passed: no dynamic interpreter."
fi
