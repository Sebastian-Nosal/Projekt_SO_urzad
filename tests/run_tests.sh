#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="$ROOT_DIR/tests/out"

PIPE_NAME="/tmp/sn_155290_biletomat_wez"
SHM_NAME="/sn_155290_biletomat_lista"
SEM_NAME="/sn_155290_biletomat_sem1"
SEM_TICKET_ASSIGNED="/sn_155290_biletomat_ticket_assigned"
KASA_PIPE="/tmp/sn_155290_kasa_pipe"
URZEDNIK_EXHAUST_SHM="/sn_155290_urzednik_exhaust"

mkdir -p "$OUT_DIR" "$ROOT_DIR/temp"

log() { echo "[test] $*"; }

die() {
  echo "[test] FAIL: $*" >&2
  return 1
}

check_can_fork() {
  set +e
  ( : ) >/dev/null 2>&1 &
  local rc=$?
  local pid=$!
  set -e

  if [[ $rc -ne 0 || -z "${pid:-}" ]]; then
    echo "[test] FAIL: Brak zasobów na fork() (Resource temporarily unavailable)." >&2
    echo "[test] To oznacza, że środowisko osiągnęło limit procesów (RLIMIT_NPROC / cgroup pids)." >&2
    echo "[test] Zwolnij procesy albo zwiększ limit i uruchom ponownie." >&2
    echo "[test] Pomocne komendy (w tej samej powłoce):" >&2
    echo "[test]   ulimit -u" >&2
    echo "[test]   cat /sys/fs/cgroup/pids.max 2>/dev/null; cat /sys/fs/cgroup/pids.current 2>/dev/null" >&2
    echo "[test]   ps -e | wc -l" >&2
    echo "[test]   pkill -f './workers/'  # jeśli zostały zombie/procesy z poprzednich uruchomień" >&2
    exit 1
  fi

  wait "$pid" >/dev/null 2>&1 || true
}

require_bin() {
  local p="$1"
  [[ -x "$p" ]] || die "Brak binarki: $p (zbuduj: ./build_workers.sh)"
}

HAVE_STDBUF=0
if command -v stdbuf >/dev/null 2>&1; then
  HAVE_STDBUF=1
fi

run_line_buffered() {
  if [[ "$HAVE_STDBUF" == "1" ]]; then
    stdbuf -oL -eL "$@"
  else
    "$@"
  fi
}

cleanup_ipc() {
  rm -f "$PIPE_NAME" "$KASA_PIPE" 2>/dev/null || true
  if [[ -d /dev/shm ]]; then
    rm -f "/dev/shm${SHM_NAME}" "/dev/shm${URZEDNIK_EXHAUST_SHM}" 2>/dev/null || true
    rm -f "/dev/shm/sem${SEM_NAME}" "/dev/shm/sem${SEM_TICKET_ASSIGNED}" 2>/dev/null || true
    rm -f /dev/shm/sem.sn_155290_biletomat_sem1 /dev/shm/sem.sn_155290_biletomat_ticket_assigned 2>/dev/null || true
    rm -f /dev/shm/sn_155290_biletomat_lista /dev/shm/sn_155290_urzednik_exhaust 2>/dev/null || true
  fi
}

cleanup_logs() {
  rm -rf "$OUT_DIR"/*
}

kill_quiet() {
  local sig="$1"; shift
  for pid in "$@"; do
    if [[ -n "${pid:-}" ]] && kill -0 "$pid" 2>/dev/null; then
      kill "$sig" "$pid" 2>/dev/null || true
    fi
  done
}

wait_for_file() {
  local path="$1" timeout_s="$2"
  local i
  for ((i=0; i<timeout_s*10; i++)); do
    [[ -e "$path" ]] && return 0
    sleep 0.1
  done
  return 1
}

wait_for_grep() {
  local pattern="$1" file="$2" timeout_s="$3"
  local i
  for ((i=0; i<timeout_s*10; i++)); do
    if [[ -f "$file" ]] && grep -E "$pattern" "$file" >/dev/null 2>&1; then
      return 0
    fi
    sleep 0.1
  done
  return 1
}

wait_for_exit() {
  local pid="$1" timeout_s="$2"
  local i
  for ((i=0; i<timeout_s*10; i++)); do
    if ! kill -0 "$pid" 2>/dev/null; then
      return 0
    fi
    sleep 0.1
  done
  return 1
}

# --- TEST 1 ---
# Sprawdzanie wydawania biletów przez biletomat
TEST_1_ticket_issuing() {
  log "TEST 1: biletomat wydaje bilet"
  cleanup_ipc

  require_bin "$ROOT_DIR/workers/biletomat/biletomat"
  require_bin "$ROOT_DIR/workers/petent/petent"

  local blog="$OUT_DIR/test1_biletomat.log"
  local plog="$OUT_DIR/test1_petent.log"
  : >"$blog"; : >"$plog"

  (cd "$ROOT_DIR"; run_line_buffered ./workers/biletomat/biletomat 3 >"$blog" 2>&1) &
  local bpid=$!

  if ! wait_for_file "$PIPE_NAME" 5; then
    kill_quiet -TERM "$bpid"
    die "Biletomat nie utworzył FIFO ($PIPE_NAME)" || true
    return 1
  fi

  (cd "$ROOT_DIR"; run_line_buffered ./workers/petent/petent 0 5 0 1 >"$plog" 2>&1) &
  local ppid=$!

  if ! wait_for_grep "Przydzielono ticket|Otrzymałem bilet" "$blog" 5 && ! wait_for_grep "Otrzymałem bilet" "$plog" 5; then
    kill_quiet -TERM "$ppid" "$bpid"
    die "Nie wykryto wydania biletu (brak logów w $blog / $plog)" || true
    return 1
  fi

  # sprzątanie
  kill_quiet -USR2 "$ppid"
  kill_quiet -USR1 "$bpid"
  wait_for_exit "$ppid" 5 || true
  wait_for_exit "$bpid" 5 || true
  wait "$ppid" >/dev/null 2>&1 || true
  wait "$bpid" >/dev/null 2>&1 || true
  cleanup_ipc
  return 0
}

# --- TEST 2 ---
# Sprawdzenie przekroczenia maksymalnej liczby procesów (petentów na inputcie)
TEST_2_softcap_process_limit() {
  log "TEST 2: main ogranicza liczbę petentów (softcap)"
  cleanup_ipc

  require_bin "$ROOT_DIR/main"

  local mlog="$OUT_DIR/test2_main.log"
  : >"$mlog"

  # Dry-run: sprawdzamy samą logikę softcap bez start_simulation (bez forków).
  (cd "$ROOT_DIR"; SIM_DRY_RUN=1 ./main --dry-run >"$mlog" 2>&1 < <(printf "999999\n"))

  if ! wait_for_grep "OSTRZEŻENIE: Podana ilość" "$mlog" 8; then
    die "Main nie wypisał ostrzeżenia o przekroczeniu softcap (log: $mlog)" || true
    return 1
  fi
  if ! wait_for_grep "Ograniczam do" "$mlog" 8; then
    die "Main nie ograniczył liczby petentów (brak 'Ograniczam do' w $mlog)" || true
    return 1
  fi

  if ! wait_for_grep "\(dry-run\) Pomijam start_simulation\(\)" "$mlog" 8; then
    die "Main nie wszedł w dry-run (log: $mlog)" || true
    return 1
  fi

  cleanup_ipc
  return 0
}

# --- TEST 3 ---
# Sprawdzenie czy petent najpierw zbiera bilet, a potem jest obsługiwany
TEST_3_petent_ticket_and_handling() {
  log "TEST 3: petent zbiera bilet, a potem jest obsługiwany"
  cleanup_ipc

  require_bin "$ROOT_DIR/main"

  local sim_log="$OUT_DIR/test3_simulation.log"
  : >"$sim_log"

  # Uruchomienie symulacji z 1 petentem
  (cd "$ROOT_DIR"; ./main 1 >"$sim_log" 2>&1) &
  local sim_pid=$!

  # Sprawdzenie czy logi występują w odpowiedniej kolejności
  if ! wait_for_grep "Pobiera bilet" "$sim_log" 5; then
    kill_quiet -TERM "$sim_pid"
    die "Nie znaleziono logu 'Pobiera bilet' w symulacji (log: $sim_log)" || true
    return 1
  fi

  if ! wait_for_grep "obsłużył petenta" "$sim_log" 5; then
    kill_quiet -TERM "$sim_pid"
    die "Nie znaleziono logu 'obsłużył petenta' w symulacji (log: $sim_log)" || true
    return 1
  fi

  # Sprawdzenie czy proces się nie zawiesił
  if ! wait_for_exit "$sim_pid" 5; then
    log "Symulacja zawiesiła się, wywołuję Ctrl+C"
    kill_quiet -INT "$sim_pid"
    wait "$sim_pid" >/dev/null 2>&1 || true
    die "Symulacja zawiesiła się na ponad 5 sekund (log: $sim_log)" || true
    return 1
  fi

  wait "$sim_pid" >/dev/null 2>&1 || true
  cleanup_ipc
  return 0
}

# --- TEST 4 ---
# Sprawdzenie czy na sygnał dyrektora wszyscy kończą pracę
TEST_4_director_signal_ends_all() {
  log "TEST 4: sygnał zamknięcia kończy procesy"
  cleanup_ipc

  require_bin "$ROOT_DIR/workers/biletomat/biletomat"
  require_bin "$ROOT_DIR/workers/kasa/kasa"
  require_bin "$ROOT_DIR/workers/urzednik/urzednik"
  require_bin "$ROOT_DIR/workers/petent/petent"

  local blog="$OUT_DIR/test4_biletomat.log"
  local klog="$OUT_DIR/test4_kasa.log"
  local ulog="$OUT_DIR/test4_urzednik.log"
  local plog="$OUT_DIR/test4_petent.log"
  : >"$blog"; : >"$klog"; : >"$ulog"; : >"$plog"

  (cd "$ROOT_DIR"; run_line_buffered ./workers/biletomat/biletomat 5 >"$blog" 2>&1) &
  local bpid=$!
  wait_for_file "$PIPE_NAME" 5 || die "Biletomat nie utworzył FIFO w teście 4"

  (cd "$ROOT_DIR"; run_line_buffered ./workers/kasa/kasa >"$klog" 2>&1) &
  local kpid=$!

  # urzędnik: typ 0 (SC)
  (cd "$ROOT_DIR"; run_line_buffered ./workers/urzednik/urzednik 0 >"$ulog" 2>&1) &
  local upid=$!

  (cd "$ROOT_DIR"; run_line_buffered ./workers/petent/petent 0 5 0 1 >"$plog" 2>&1) &
  local ppid=$!

  # daj chwilę na inicjalizację
  sleep 2

  # Symulujemy "sygnał dyrektora": zamknięcie urzędu (SIGUSR2) dla biletomatu/urzędnika/petenta,
  # a dla kasy (brak SIGUSR2 handlera) kończymy SIGUSR1.
  kill_quiet -USR2 "$ppid" "$upid" "$bpid"
  kill_quiet -USR1 "$kpid"

  wait_for_exit "$ppid" 8 || die "Petent nie zakończył pracy po sygnale (log: $plog)"
  wait_for_exit "$upid" 8 || die "Urzędnik nie zakończył pracy po sygnale (log: $ulog)"
  wait_for_exit "$bpid" 8 || die "Biletomat nie zakończył pracy po sygnale (log: $blog)"
  wait_for_exit "$kpid" 8 || die "Kasa nie zakończyła pracy po sygnale (log: $klog)"

  wait "$ppid" >/dev/null 2>&1 || true
  wait "$upid" >/dev/null 2>&1 || true
  wait "$bpid" >/dev/null 2>&1 || true
  wait "$kpid" >/dev/null 2>&1 || true

  cleanup_ipc
  return 0
}

cleanup_logs() {
  rm -rf "$OUT_DIR"/*
}

run_test() {
  local test_name="$1"
  shift
  log "Running $test_name"
  if "$@"; then
    log "$test_name OK"
  else
    log "$test_name FAILED"
  fi
  # Ensure all processes are terminated after the test
  pkill -f "$ROOT_DIR/workers" || true
}

main() {
  log "Repo: $ROOT_DIR"
  log "Wyniki: $OUT_DIR"

  # Wyczyść poprzednie logi na start
  cleanup_logs

  check_can_fork

  run_test "TEST 1: biletomat wydaje bilet" TEST_1_ticket_issuing
  run_test "TEST 2: main ogranicza liczbę petentów (softcap)" TEST_2_softcap_process_limit
  run_test "TEST 3: petent czeka na bilet, zanim biletomat ruszy" TEST_3_petent_ticket_and_handling
  run_test "TEST 4: sygnał zamknięcia kończy procesy" TEST_4_director_signal_ends_all

  cleanup_ipc
  # Usuń logi po zakończeniu wszystkich testów
  cleanup_logs
}

main "$@"
