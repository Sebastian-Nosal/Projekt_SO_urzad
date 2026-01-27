#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SANDBOX="$ROOT_DIR/tests/sandbox"

rm -rf "$SANDBOX"
mkdir -p "$SANDBOX/config" "$SANDBOX/utils" "$SANDBOX/workers" "$SANDBOX/tests"

cat > "$SANDBOX/config/config.h" <<'EOF'
#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <sys/types.h>

constexpr int SIMULATION_DURATION = 120;

constexpr int NUM_SA_OFFICERS = 2;
constexpr int NUM_SC_OFFICERS = 1;
constexpr int NUM_KM_OFFICERS = 1;
constexpr int NUM_ML_OFFICERS = 1;
constexpr int NUM_PD_OFFICERS = 1;

constexpr int MAX_SA_APPOINTMENTS = 40;
constexpr int MAX_SC_APPOINTMENTS = 20;
constexpr int MAX_KM_APPOINTMENTS = 20;
constexpr int MAX_ML_APPOINTMENTS = 20;
constexpr int MAX_PD_APPOINTMENTS = 20;

constexpr double PERCENT_SA = 0.6;
constexpr double PERCENT_SC = 0.1;
constexpr double PERCENT_KM = 0.1;
constexpr double PERCENT_ML = 0.1;
constexpr double PERCENT_PD = 0.1;

enum class DepartmentType { SA, SC, KM, ML, PD };

constexpr int PETENT_MAX_COUNT_IN_MOMENT = 10;
constexpr int DAILY_CLIENTS = 200;

constexpr int MAX_CLIENTS_IN_BUILDING = 10;
constexpr int TICKET_MACHINES_MAX = 3;
constexpr int TICKET_MACHINES_MIN = 1;
constexpr int TICKET_MACHINE_K = MAX_CLIENTS_IN_BUILDING / 3;
constexpr int TICKET_MACHINE_THIRD_CLOSE = (2 * MAX_CLIENTS_IN_BUILDING) / 3;
constexpr int CHILD_CHANCE_PERCENT = 3;
constexpr int VIP_CHANCE_PERCENT = 2;

const std::string LOG_FILE = "./urzad_log.txt";
const std::string REPORT_FILE = "./urzad_report.txt";

constexpr int INTERVAL_MIN = 1;
constexpr int INTERVAL_MAX = 10;
constexpr int ADD_PETENTS_MIN = 0;
constexpr int ADD_PETENTS_MAX = 20;
constexpr int WAIT_MIN = 1;
constexpr int WAIT_MAX = 3;
constexpr int WAIT_FRUSTRATED = 5;
constexpr int TIME_FRUSTRATED = WAIT_FRUSTRATED;
constexpr int URZEDNIK_DELAY_MS_MIN = 100;
constexpr int URZEDNIK_DELAY_MS_MAX = 500;

constexpr const char* SEMAPHORE_NAME = "/shm_semaphore";

constexpr const char* OTHER_QUEUE_SEM_NAME = "/mq_other_semaphore";
constexpr unsigned int OTHER_QUEUE_LIMIT = 50;

constexpr const char* PETENT_QUEUE_SEM_NAME = "/mq_petent_semaphore";
constexpr unsigned int ENTRY_QUEUE_LIMIT = 50;
constexpr unsigned int EXIT_QUEUE_LIMIT = 50;

constexpr key_t SHM_KEY = 155290;
constexpr key_t MQ_KEY_ENTRY = 290155;
constexpr key_t MQ_KEY_OTHER = 290156;
constexpr key_t MQ_KEY_EXIT = 290157;

#endif
EOF

cat > "$SANDBOX/config/config_all.h" <<'EOF'
#ifndef CONFIG_ALL_H
#define CONFIG_ALL_H

#include <string>
#include <sys/types.h>

constexpr int SIMULATION_DURATION = 120;

constexpr int NUM_SA_OFFICERS = 2;
constexpr int NUM_SC_OFFICERS = 1;
constexpr int NUM_KM_OFFICERS = 1;
constexpr int NUM_ML_OFFICERS = 1;
constexpr int NUM_PD_OFFICERS = 1;

constexpr int MAX_SA_APPOINTMENTS = 40;
constexpr int MAX_SC_APPOINTMENTS = 20;
constexpr int MAX_KM_APPOINTMENTS = 20;
constexpr int MAX_ML_APPOINTMENTS = 20;
constexpr int MAX_PD_APPOINTMENTS = 20;

constexpr double PERCENT_SA = 0.6;
constexpr double PERCENT_SC = 0.1;
constexpr double PERCENT_KM = 0.1;
constexpr double PERCENT_ML = 0.1;
constexpr double PERCENT_PD = 0.1;

enum class DepartmentType { SA, SC, KM, ML, PD };

constexpr int PETENTS_AMOUNT = 200;

constexpr int MAX_CLIENTS_IN_BUILDING = 10;
constexpr int TICKET_MACHINES_MAX = 3;
constexpr int TICKET_MACHINES_MIN = 1;
constexpr int TICKET_MACHINE_K = MAX_CLIENTS_IN_BUILDING / 3;
constexpr int TICKET_MACHINE_THIRD_CLOSE = (2 * MAX_CLIENTS_IN_BUILDING) / 3;
constexpr int CHILD_CHANCE_PERCENT = 3;
constexpr int VIP_CHANCE_PERCENT = 2;

const std::string LOG_FILE = "./urzad_log.txt";
const std::string REPORT_FILE = "./urzad_report.txt";

constexpr int INTERVAL_MIN = 1;
constexpr int INTERVAL_MAX = 10;
constexpr int ADD_PETENTS_MIN = 0;
constexpr int ADD_PETENTS_MAX = 20;
constexpr int WAIT_MIN = 1;
constexpr int WAIT_MAX = 3;
constexpr int WAIT_FRUSTRATED = 5;
constexpr int TIME_FRUSTRATED = WAIT_FRUSTRATED;
constexpr int URZEDNIK_DELAY_MS_MIN = 100;
constexpr int URZEDNIK_DELAY_MS_MAX = 500;

constexpr const char* SEMAPHORE_NAME = "/shm_semaphore";

constexpr const char* OTHER_QUEUE_SEM_NAME = "/mq_other_semaphore";
constexpr unsigned int OTHER_QUEUE_LIMIT = 50;

constexpr const char* PETENT_QUEUE_SEM_NAME = "/mq_petent_semaphore";
constexpr unsigned int ENTRY_QUEUE_LIMIT = 50;
constexpr unsigned int EXIT_QUEUE_LIMIT = 50;

constexpr key_t SHM_KEY = 155290;
constexpr key_t MQ_KEY_ENTRY = 290155;
constexpr key_t MQ_KEY_OTHER = 290156;
constexpr key_t MQ_KEY_EXIT = 290157;

#endif
EOF

cd "$ROOT_DIR"

UTILS_SRCS=""
for f in utils/*.cpp; do
	if [ -f "$f" ] && [ "$(basename "$f")" != "monitoring.cpp" ]; then
		UTILS_SRCS+="$f "
	fi
done

echo "Kompiluję binarki do sandbox..."
mkdir -p "$SANDBOX/workers"
for dir in workers/*/; do
	name=$(basename "$dir")
	srcs=$(find "$dir" -name '*.cpp')
	if [ -n "$srcs" ]; then
		mkdir -p "$SANDBOX/workers/$name"
		g++ -std=c++11 -Wall -O2 $srcs $UTILS_SRCS -I"$SANDBOX" -I. -Iheaders -Iutils -I"$dir" -o "$SANDBOX/workers/$name/$name"
	fi
done

g++ -std=c++11 -Wall -O2 main_all.cpp $UTILS_SRCS -I"$SANDBOX" -I. -Iheaders -Iutils -o "$SANDBOX/main_all"
g++ -std=c++11 -Wall -O2 loader_all.cpp $UTILS_SRCS -I"$SANDBOX" -I. -Iheaders -Iutils -o "$SANDBOX/loader_all"
g++ -std=c++11 -Wall -O2 utils/monitoring.cpp $UTILS_SRCS -I"$SANDBOX" -I. -Iheaders -Iutils -o "$SANDBOX/monitoring"

echo "Kompiluję testy workerów..."
g++ -std=c++11 -Wall -O2 tests/worker_ipc_tests.cpp -I"$SANDBOX" -I. -Iheaders -Iutils -o "$SANDBOX/tests/worker_ipc_tests"

echo "Uruchamiam testy workerów w sandbox..."
cd "$SANDBOX"
./tests/worker_ipc_tests
