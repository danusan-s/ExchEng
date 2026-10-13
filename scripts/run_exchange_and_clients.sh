#!/bin/bash
# Run the exchange, then the clients, then shut the exchange down.
# Usage: ./scripts/run_exchange_and_clients.sh [warmup_seconds]
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXCHANGE_BIN="$ROOT/build/ExchEng"
CLIENT_SCRIPT="$ROOT/scripts/run_clients.sh"
WARMUP_SECS="${1:-10}"

if [[ ! -x "$EXCHANGE_BIN" ]]; then
  echo "Exchange binary not found/executable: $EXCHANGE_BIN (run 'make build' first)" >&2
  exit 1
fi
if [[ ! -f "$CLIENT_SCRIPT" ]]; then
  echo "Client script not found: $CLIENT_SCRIPT" >&2
  exit 1
fi

echo "RUNNING EXCHANGE"
"$EXCHANGE_BIN" &
EXCHANGE_PID=$!

cleanup() {
  if kill -0 "$EXCHANGE_PID" 2>/dev/null; then
    kill -INT "$EXCHANGE_PID" 2>/dev/null
    wait "$EXCHANGE_PID" 2>/dev/null
  fi
}
trap cleanup INT TERM

sleep "$WARMUP_SECS"

if ! kill -0 "$EXCHANGE_PID" 2>/dev/null; then
  echo "Exchange exited before clients started" >&2
  wait "$EXCHANGE_PID"
  exit 1
fi

echo "RUNNING CLIENT"
bash "$CLIENT_SCRIPT"
CLIENT_STATUS=$?

echo "Client script exited with status $CLIENT_STATUS, shutting down exchange (SIGINT)"
if kill -0 "$EXCHANGE_PID" 2>/dev/null; then
  kill -INT "$EXCHANGE_PID"
  wait "$EXCHANGE_PID"
  EXCHANGE_STATUS=$?
else
  wait "$EXCHANGE_PID"
  EXCHANGE_STATUS=$?
  echo "Exchange had already exited with status $EXCHANGE_STATUS"
fi

trap - INT TERM
exit "$CLIENT_STATUS"
