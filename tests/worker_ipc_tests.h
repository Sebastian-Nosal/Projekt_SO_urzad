/**
 * @file worker_ipc_tests.h
 * @brief Pomocnicze funkcje i testy IPC używane przez `worker_ipc_tests.cpp`.
 *
 * Zawiera deklaracje inicjalizujące kolejki komunikatów, pamięć współdzieloną
 * i semafor POSIX oraz funkcje pomocnicze do oczekiwania na wiadomości i
 * czyszczenia zasobów IPC. Również deklaruje poszczególne testy integracyjne
 * uruchamiane przez `worker_ipc_tests.cpp`.
 */

#ifndef WORKER_IPC_TESTS_H
#define WORKER_IPC_TESTS_H

#include <sys/ipc.h>
#include <semaphore.h>

#include "config/config.h"
#include "config/messages.h"
#include "config/shm.h"

/**
 * @brief Inicjalizuje kolejkę wiadomości o danym kluczu.
 * @param key Klucz System V IPC.
 * @return ID kolejki lub -1 w przypadku błędu.
 */
int initQueue(key_t key);

/**
 * @brief Tworzy/uzyskuje dostęp do segmentu pamięci współdzielonej.
 * @return ID segmentu pamięci lub -1 w przypadku błędu.
 */
int initShm();

/**
 * @brief Otwiera semafor POSIX używany przez testy.
 * @return Wskaźnik do semafora lub `nullptr` przy błędzie.
 */
sem_t* initSemaphore();

/**
 * @brief Usuwa i czyści zasoby IPC utworzone przez testy.
 * @param mqidPetent ID kolejki petentów (lub -1 jeśli nie istnieje).
 * @param mqidOther ID drugiej kolejki (lub -1).
 * @param shmid ID segmentu pamięci współdzielonej (lub -1).
 * @param sem Wskaźnik do otwartego semafora (może być `nullptr`).
 */
void cleanupIpc(int mqidPetent, int mqidOther, int shmid, sem_t* sem);

/**
 * @brief Oczekuje na wiadomość o określonym typie w kolejce z limitem czasu.
 * @param mqid ID kolejki.
 * @param type Typ wiadomości (pole `mtype`).
 * @param out Referencja, do której zostanie zapisany odebrany komunikat.
 * @param timeoutMs Limit oczekiwania w milisekundach.
 * @return `true` jeśli wiadomość została odebrana, `false` w przeciwnym razie.
 */
bool waitForMsg(int mqid, long type, Message& out, int timeoutMs);

/**
 * @brief Testuje zachowanie FIFO kolejki komunikatów.
 * @return `true` jeśli test zakończył się sukcesem.
 */
bool testMessageOrder();

/**
 * @brief Testuje liczbę wiadomości w kolejce IPC.
 * @return `true` jeśli test zakończył się sukcesem.
 */
bool testQueueLength();

/**
 * @brief Testuje uruchomienie biletomatu i odpowiedź na żądanie biletu.
 * @return `true` jeśli test zakończył się sukcesem.
 */
bool testBiletomatStart();

/**
 * @brief Test wykrywania zakleszczeń na podstawie logów z `main_all`.
 * @return `true` jeśli test zakończył się sukcesem.
 */
bool testDeadlockDetection();

/**
 * @brief Główny punkt wejścia testów IPC.
 * @return Kod zakończenia procesu.
 */
int main();

#endif // WORKER_IPC_TESTS_H
