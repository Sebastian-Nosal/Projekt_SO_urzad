#ifndef CONFIG_ALL_H
#define CONFIG_ALL_H

#include <string>
#include <sys/types.h>

constexpr int SIMULATION_DURATION = 1800; // Czas trwania symulacji w sekundach

constexpr int NUM_SA_OFFICERS = 2; // Wydział Spraw Administracyjnych
constexpr int NUM_SC_OFFICERS = 1; // Urząd Stanu Cywilnego
constexpr int NUM_KM_OFFICERS = 1; // Wydział Ewidencji Pojazdów i Kierowców
constexpr int NUM_ML_OFFICERS = 1; // Wydział Mieszkalnictwa
constexpr int NUM_PD_OFFICERS = 1; // Wydział Podatków i Opłat


constexpr int MAX_SA_APPOINTMENTS = 40; // Przykładowa wartość dla SA
constexpr int MAX_SC_APPOINTMENTS = 20; // Przykładowa wartość dla SC
constexpr int MAX_KM_APPOINTMENTS = 20; // Przykładowa wartość dla KM
constexpr int MAX_ML_APPOINTMENTS = 20; // Przykładowa wartość dla ML
constexpr int MAX_PD_APPOINTMENTS = 20; // Przykładowa wartość dla PD

constexpr double PERCENT_SA = 0.6; // 60% dla SA
constexpr double PERCENT_SC = 0.1; // 10% dla SC
constexpr double PERCENT_KM = 0.1; // 10% dla KM
constexpr double PERCENT_ML = 0.1; // 10% dla ML
constexpr double PERCENT_PD = 0.1; // 10% dla PD

enum class DepartmentType {
    SA, // Wydział Spraw Administracyjnych
    SC, // Urząd Stanu Cywilnego
    KM, // Wydział Ewidencji Pojazdów i Kierowców
    ML, // Wydział Mieszkalnictwa
    PD  // Wydział Podatków i Opłat
};

constexpr int PETENTS_AMOUNT = 5000;

constexpr int MAX_CLIENTS_IN_BUILDING = 10;
constexpr int TICKET_MACHINES_MAX = 3;
constexpr int TICKET_MACHINES_MIN = 1;
constexpr int TICKET_MACHINE_K = MAX_CLIENTS_IN_BUILDING / 3;
constexpr int TICKET_MACHINE_THIRD_CLOSE = (2 * MAX_CLIENTS_IN_BUILDING) / 3;
constexpr int CHILD_CHANCE_PERCENT = 3;
constexpr int VIP_CHANCE_PERCENT = 2;

const std::string LOG_FILE = "./urzad_log.txt";
const std::string REPORT_FILE = "./urzad_report.txt";

// Zakresy dla losowania
constexpr int INTERVAL_MIN = 1;
constexpr int INTERVAL_MAX = 10;
constexpr int ADD_PETENTS_MIN = 0;
constexpr int ADD_PETENTS_MAX = 20;
constexpr int WAIT_MIN = 1;
constexpr int WAIT_MAX = 3;
constexpr int WAIT_FRUSTRATED = 5;
constexpr int TIME_FRUSTRATED = WAIT_FRUSTRATED;
constexpr int URZEDNIK_DELAY_MS_MIN = 500;
constexpr int URZEDNIK_DELAY_MS_MAX = 2000;

// Nazwa semafora dla sekcji krytycznej
constexpr const char* SEMAPHORE_NAME = "/shm_semaphore";

// Semafor liczacy dla kolejki other processes queue
constexpr const char* OTHER_QUEUE_SEM_NAME = "/mq_other_semaphore";
constexpr unsigned int OTHER_QUEUE_LIMIT = 50;

// Semafor liczacy dla kolejki petentow (legacy)
constexpr const char* PETENT_QUEUE_SEM_NAME = "/mq_petent_semaphore";
constexpr unsigned int ENTRY_QUEUE_LIMIT = 50;
constexpr unsigned int EXIT_QUEUE_LIMIT = 50;

// Klucze IPC
constexpr key_t SHM_KEY = 155290;
constexpr key_t MQ_KEY_ENTRY = 290155;
constexpr key_t MQ_KEY_OTHER = 290156;
constexpr key_t MQ_KEY_EXIT = 290157;

#endif // CONFIG_ALL_H
