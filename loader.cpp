#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <ctime>
#include <cstdlib> // Dla funkcji system()
#include <unistd.h> // Dla fork() i exec()
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <cstring> // Dla memset
#include <semaphore.h> // Dla semaforów POSIX
#include <fcntl.h> // Dla O_CREAT
#include <sys/stat.h> // Dla praw dostępu
#include "utils/losowosc.h"
#include "config/config.h"
#include "config/messages.h"

void startProcess(const std::string& name, const std::string& argument = "") {
    pid_t pid = fork();

    if (pid == 0) { // Proces potomny
        std::string executable = "./" + name;
        if (!argument.empty()) {
            execl(executable.c_str(), name.c_str(), argument.c_str(), nullptr);
        } else {
            execl(executable.c_str(), name.c_str(), nullptr);
        }

        // Jeśli exec się nie powiedzie
        std::cerr << "Error starting process: " << name << std::endl;
        _exit(EXIT_FAILURE);
    } else if (pid < 0) { // Błąd forka
        std::cerr << "Fork failed for process: " << name << std::endl;
    } else { // Proces macierzysty
        std::cout << "Started process: " << name << " with PID: " << pid << std::endl;
    }
}

void startOfficers() {
    for (int i = 0; i < NUM_SA_OFFICERS; ++i) {
        startProcess("urzednik", "SA");
    }

    for (int i = 0; i < NUM_SC_OFFICERS; ++i) {
        startProcess("urzednik", "SC");
    }

    for (int i = 0; i < NUM_KM_OFFICERS; ++i) {
        startProcess("urzednik", "KM");
    }

    for (int i = 0; i < NUM_ML_OFFICERS; ++i) {
        startProcess("urzednik", "ML");
    } 

    for (int i = 0; i < NUM_PD_OFFICERS; ++i) {
        startProcess("urzednik", "PD");
    } 
}

void zarzadzaniePetentami(bool& urzadOtwarty, int& iloscPetentow) {
    const int czasKoniec = 60; // Czas trwania symulacji w sekundach
    int czasStart = std::time(nullptr);

    while (std::time(nullptr) - czasStart < czasKoniec) {
        // Losowanie liczby petentów do wygenerowania
        int liczbaPetentow = losujIlosc(ADD_PETENTS_MIN, ADD_PETENTS_MAX);

        // Sprawdzanie, czy nowa liczba petentów nie przekroczy maksymalnej wartości
        if (iloscPetentow + liczbaPetentow > MAX_CLIENTS_IN_BUILDING) {
            liczbaPetentow = MAX_CLIENTS_IN_BUILDING - iloscPetentow;
        }

        // Tworzenie petentów
        for (int i = 0; i < liczbaPetentow; ++i) {
            std::cout << "Tworzenie petenta: " << (iloscPetentow + 1) << std::endl;
            ++iloscPetentow;
        }

        // Losowanie czasu oczekiwania na następne logowanie
        int czasOczekiwania = losujIlosc(INTERVAL_MIN, INTERVAL_MAX);
        std::this_thread::sleep_for(std::chrono::seconds(czasOczekiwania));
    }

    urzadOtwarty = false;
}

// Funkcja inicjalizująca pamięć współdzieloną
int initSharedMemory() {
    int shmid = shmget(SHM_KEY, sizeof(int), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget failed");
        exit(EXIT_FAILURE);
    }
    return shmid;
}

// Funkcja inicjalizująca kolejkę komunikatów
int initMessageQueue() {
    int mqid = msgget(MQ_KEY, IPC_CREAT | 0666);
    if (mqid == -1) {
        perror("msgget failed");
        exit(EXIT_FAILURE);
    }
    return mqid;
}

// Inicjalizacja semafora dla sekcji krytycznej
sem_t* initSemaphore() {
    sem_t* semaphore = sem_open(SEMAPHORE_NAME, O_CREAT, 0666, 1);
    if (semaphore == SEM_FAILED) {
        perror("sem_open failed");
        exit(EXIT_FAILURE);
    }
    return semaphore;
}

// Funkcja odbierająca komunikaty z MQ
void handleMessages(int mqid, int& clientsInBuilding, sem_t* semaphore) {
    Message msg;
    while (msgrcv(mqid, &msg, sizeof(msg) - sizeof(long), 0, IPC_NOWAIT) != -1) {
        std::cout << "Received message from sender: " << msg.senderId << std::endl;

        // Parsowanie wiadomości
        if (msg.messageType.petentType == PetentMessagesEnum::WejdzDoBudynku) {
            sem_wait(semaphore); // Sekcja krytyczna

            if (clientsInBuilding < MAX_CLIENTS_IN_BUILDING) {
                ++clientsInBuilding;
                std::cout << "Petent PID:" << msg.senderId << " wszedł do budynku. Liczba petentów: " << clientsInBuilding << std::endl;

                Message response;
                response.senderId = getpid(); 
                response.receiverId = msg.senderId;
                response.messageType.petentType = PetentMessagesEnum::OtrzymanoBilet;

                if (msgsnd(mqid, &response, sizeof(response) - sizeof(long), 0) == -1) {
                    perror("msgsnd failed");
                }
            } else {
                std::cout << "Building is full. Client denied entry." << std::endl;
            }

            sem_post(semaphore); // Koniec sekcji krytycznej
        }
    }
}

int main() {
    // Inicjalizacja pamięci współdzielonej
    int shmid = initSharedMemory();
    int* sharedClients = static_cast<int*>(shmat(shmid, nullptr, 0));
    if (sharedClients == reinterpret_cast<int*>(-1)) {
        perror("shmat failed");
        exit(EXIT_FAILURE);
    }
    *sharedClients = 0; // Inicjalizacja liczby klientów w pamięci współdzielonej

    // Inicjalizacja kolejki komunikatów
    int mqid = initMessageQueue();

    // Inicjalizacja semafora
    sem_t* semaphore = initSemaphore();

    // Tworzenie pamięci współdzielonej
    int iloscPetentowWBudynku = 0;
    bool urzadOtwarty = true;

    // Uruchamianie procesów
    startProcess("director");
    startOfficers();
    startProcess("cashier");
    startProcess("ticket_machine");

    // Czekanie na uruchomienie procesów
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Uruchamianie wątku zarządzania petentami
    std::thread watekPetentow(zarzadzaniePetentami, std::ref(urzadOtwarty), std::ref(*sharedClients));

    // Główna pętla odbierania komunikatów z MQ
    while (urzadOtwarty) {
        handleMessages(mqid, *sharedClients, semaphore);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    watekPetentow.join();

    // Wait for all child processes to finish
    while (wait(nullptr) > 0);

    // Sprzątanie pamięci współdzielonej
    shmdt(sharedClients);
    shmctl(shmid, IPC_RMID, nullptr);

    // Usuwanie kolejki komunikatów
    msgctl(mqid, IPC_RMID, nullptr);

    // Usuwanie semafora
    sem_close(semaphore);
    sem_unlink("/shm_semaphore");

    std::cout << "Symulacja zakończona." << std::endl;
    return 0;
}
