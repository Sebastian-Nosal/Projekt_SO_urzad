/**
 * @file config.h
 * @brief Parametry konfiguracyjne symulacji (tryb standardowy).
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <sys/types.h>

/// Czas trwania symulacji w sekundach.
constexpr int SIMULATION_DURATION = 120;

/// Liczba urzędników w wydziałach.
constexpr int NUM_SA_OFFICERS = 2;
constexpr int NUM_SC_OFFICERS = 1;
constexpr int NUM_KM_OFFICERS = 1;
constexpr int NUM_ML_OFFICERS = 1;
constexpr int NUM_PD_OFFICERS = 1;


/// Limity obsługi w wydziałach.
constexpr int MAX_SA_APPOINTMENTS = 40;
constexpr int MAX_SC_APPOINTMENTS = 20;
constexpr int MAX_KM_APPOINTMENTS = 20;
constexpr int MAX_ML_APPOINTMENTS = 20;
constexpr int MAX_PD_APPOINTMENTS = 20;

/// Procentowy rozkład wydziałów.
constexpr double PERCENT_SA = 0.6;
constexpr double PERCENT_SC = 0.1;
constexpr double PERCENT_KM = 0.1;
constexpr double PERCENT_ML = 0.1;
constexpr double PERCENT_PD = 0.1;

/// Typy wydziałów.
enum class DepartmentType {
    SA,
    SC,
    KM,
    ML,
    PD
};

/// Limity petentów.
constexpr int PETENT_MAX_COUNT_IN_MOMENT = 10;
constexpr int DAILY_CLIENTS = 200;

/// Parametry budynku i biletomatów.
constexpr int MAX_CLIENTS_IN_BUILDING = 10;
constexpr int TICKET_MACHINES_MAX = 3;
constexpr int TICKET_MACHINES_MIN = 1;
constexpr int TICKET_MACHINE_K = MAX_CLIENTS_IN_BUILDING / 3;
constexpr int TICKET_MACHINE_THIRD_CLOSE = (2 * MAX_CLIENTS_IN_BUILDING) / 3;
constexpr int CHILD_CHANCE_PERCENT = 3;
constexpr int VIP_CHANCE_PERCENT = 2;

/// Pliki wyjściowe.
const std::string LOG_FILE = "./urzad_log.txt";
const std::string REPORT_FILE = "./urzad_report.txt";

/// Zakresy dla losowania.
constexpr int INTERVAL_MIN = 1;
constexpr int INTERVAL_MAX = 10;
constexpr int ADD_PETENTS_MIN = 0;
constexpr int ADD_PETENTS_MAX = 20;
constexpr int WAIT_MIN = 1;
constexpr int WAIT_MAX = 3;
constexpr int WAIT_FRUSTRATED = 40;
constexpr int TIME_FRUSTRATED = WAIT_FRUSTRATED;
constexpr int URZEDNIK_DELAY_MS_MIN = 500;
constexpr int URZEDNIK_DELAY_MS_MAX = 2000;

/// Nazwa semafora dla sekcji krytycznej.
constexpr const char* SEMAPHORE_NAME = "/shm_semaphore";

/// Semafor liczacy dla kolejki procesów pomocniczych.
constexpr const char* OTHER_QUEUE_SEM_NAME = "/mq_other_semaphore";
constexpr unsigned int OTHER_QUEUE_LIMIT = 50;

/// Semafor liczacy dla kolejki wejściowej petentów (legacy).
constexpr const char* PETENT_QUEUE_SEM_NAME = "/mq_petent_semaphore";
constexpr unsigned int ENTRY_QUEUE_LIMIT = 50;
constexpr unsigned int EXIT_QUEUE_LIMIT = 50;

/// Klucze IPC.
constexpr key_t SHM_KEY = 155290;
constexpr key_t MQ_KEY_ENTRY = 290155;
constexpr key_t MQ_KEY_OTHER = 290156;
constexpr key_t MQ_KEY_EXIT = 290157;

#endif // CONFIG_H
