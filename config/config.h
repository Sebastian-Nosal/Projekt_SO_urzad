#ifndef CONFIG_H
#define CONFIG_H

#include <string>

constexpr int SIMULATION_DURATION = 120; // Czas trwania symulacji w sekundach

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

int PETENT_MAX_COUNT_IN_MOMENT = 1000; // Maksymalna liczba petentów w urzędzie w danym momencie może być zmieniona w mainie z std::in
constexpr int DAILY_CLIENTS = 2000; 

constexpr int MAX_CLIENTS_IN_BUILDING = 50;

const std::string LOG_FILE = "logs/urzad_log.txt";
const std::string REPORT_FILE = "logs/urzad_report.txt";

// Zakresy dla losowania
constexpr int INTERVAL_MIN = 1;
constexpr int INTERVAL_MAX = 20;
constexpr int ADD_PETENTS_MIN = 0;
constexpr int ADD_PETENTS_MAX = 55;
constexpr int WAIT_MIN = 1;
constexpr int WAIT_MAX = 3;

// Nazwa semafora dla sekcji krytycznej
constexpr const char* SEMAPHORE_NAME = "/shm_semaphore";

// Klucze IPC
constexpr key_t SHM_KEY = 155290;
constexpr key_t MQ_KEY = 290155;

#endif // CONFIG_H
