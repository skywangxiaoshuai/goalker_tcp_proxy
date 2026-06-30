#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROXY_BIN="$SCRIPT_DIR/build/tcp_timestamp_proxy"

SOURCE_HOST="${SOURCE_HOST:-127.0.0.1}"
SOURCE_PORT="${SOURCE_PORT:-9002}"
LISTEN_PORT="${LISTEN_PORT:-9102}"

if [ ! -x "$PROXY_BIN" ]; then
  echo "Proxy binary not found: $PROXY_BIN"
  echo "Run ./build.sh first."
  exit 1
fi

echo "Starting TCP timestamp proxy"
echo "  source: ${SOURCE_HOST}:${SOURCE_PORT}"
echo "  listen: 0.0.0.0:${LISTEN_PORT}"
echo "  record: disabled"

exec "$PROXY_BIN" \
  --source-host "$SOURCE_HOST" \
  --source-port "$SOURCE_PORT" \
  --listen-port "$LISTEN_PORT"
