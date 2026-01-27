#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_FILE="$ROOT_DIR/urzad_log.txt"

fail() {
  echo "[FAIL] $1" >&2
  exit 1
}

pass() {
  echo "[OK] $1"
}

cleanup_process_group() {
  local pid="$1"
  if kill -0 "$pid" 2>/dev/null; then
    kill -INT "$pid" 2>/dev/null || true
    sleep 1
  fi
  if kill -0 "$pid" 2>/dev/null; then
    kill -TERM "$pid" 2>/dev/null || true
    sleep 1
  fi
  if kill -0 "$pid" 2>/dev/null; then
    kill -KILL "$pid" 2>/dev/null || true
  fi
}

build_all() {
  (cd "$ROOT_DIR" && ./build_workers.sh)
}

prepare_log() {
  rm -f "$LOG_FILE"
}

run_main_with_input() {
  local pmax="$1"
  local daily="$2"
  local duration="$3"
  printf "%s\n%s\n%s\n" "$pmax" "$daily" "$duration" | (cd "$ROOT_DIR" && ./main)
}

run_loader_background() {
  (cd "$ROOT_DIR" && ./loader) &
  echo $!
}

# Test 1: Petent enters before ticket request
prepare_log
run_main_with_input 20 30 3
if [ ! -s "$LOG_FILE" ]; then
  fail "Log file missing or empty after main"
fi
awk '
  /petent [0-9]+ wszedł do budynku/ {
    match($0, /petent ([0-9]+)/, m);
    if (m[1] != "") entered[m[1]] = NR;
  }
  /petent=[0-9]+ czeka na bilet/ {
    match($0, /petent=([0-9]+)/, m);
    if (m[1] != "") {
      if (!(m[1] in entered) || entered[m[1]] > NR) {
        bad = 1;
      }
    }
  }
  END { exit bad ? 1 : 0; }
' "$LOG_FILE" || fail "Petent requested ticket before entering building"
pass "Petent enters before ticket"

# Test 2: SIGINT on loader causes frustrated petents
prepare_log
LOADER_PID=$(run_loader_background)
sleep 1
kill -INT "$LOADER_PID" 2>/dev/null || true
wait "$LOADER_PID" || true
if ! grep -q "petent wymuszone wyjście" "$LOG_FILE" && ! grep -q "sfrustrowany" "$LOG_FILE"; then
  fail "No frustrated petent logs after SIGINT on loader"
fi
pass "SIGINT triggers frustrated petents"

# Test 3: Log file created and non-empty
if [ ! -s "$LOG_FILE" ]; then
  fail "Log file not created or empty"
fi
pass "Log file created and non-empty"

# Test 4: All process types start
prepare_log
LOADER_PID=$(run_loader_background)
sleep 1
pgrep -f "$ROOT_DIR/loader" >/dev/null || fail "Loader not running"
pgrep -f "$ROOT_DIR/monitoring" >/dev/null || fail "Monitoring not running"
pgrep -f "$ROOT_DIR/workers/dyrektor/dyrektor" >/dev/null || fail "Dyrektor not running"
pgrep -f "$ROOT_DIR/workers/kasa/kasa" >/dev/null || fail "Kasa not running"
pgrep -f "$ROOT_DIR/workers/urzednik/urzednik" >/dev/null || fail "Urzędnik not running"
pgrep -f "$ROOT_DIR/workers/biletomat/biletomat" >/dev/null || fail "Biletomat not running"
cleanup_process_group "$LOADER_PID"
wait "$LOADER_PID" || true
pass "All process types are running"

echo "All tests passed."
