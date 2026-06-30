#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

mkdir -p "$BUILD_DIR"

g++ -std=c++17 -O2 -Wall -Wextra -pthread \
  "$SCRIPT_DIR/tcp_timestamp_proxy.cpp" \
  -o "$BUILD_DIR/tcp_timestamp_proxy"

g++ -std=c++17 -O2 -Wall -Wextra \
  "$SCRIPT_DIR/stamped_frame_dump.cpp" \
  -o "$BUILD_DIR/stamped_frame_dump"

echo "Built:"
echo "  $BUILD_DIR/tcp_timestamp_proxy"
echo "  $BUILD_DIR/stamped_frame_dump"
