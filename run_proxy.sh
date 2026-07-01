#!/bin/sh

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROXY_BIN="$SCRIPT_DIR/tcp_timestamp_proxy"

if [ ! -x "$PROXY_BIN" ]; then
  PROXY_BIN="$SCRIPT_DIR/build/tcp_timestamp_proxy"
fi

SOURCE_HOST="${SOURCE_HOST:-127.0.0.1}"
SOURCE_PORT="${SOURCE_PORT:-9002}"
LISTEN_PORT="${LISTEN_PORT:-9102}"

if [ ! -x "$PROXY_BIN" ]; then
  echo "Proxy binary not found."
  echo "Expected one of:"
  echo "  $SCRIPT_DIR/tcp_timestamp_proxy"
  echo "  $SCRIPT_DIR/build/tcp_timestamp_proxy"
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
