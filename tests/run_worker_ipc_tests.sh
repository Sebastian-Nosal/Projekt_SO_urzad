#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

cd "$ROOT_DIR"

echo "Kompiluję testy workerów..."
g++ -std=c++11 -Wall -O2 tests/worker_ipc_tests.cpp config/config.cpp -I. -Iheaders -Iutils -o tests/worker_ipc_tests

echo "Uruchamiam testy workerów..."
./tests/worker_ipc_tests
