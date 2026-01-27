/**
 * @file loader.h
 * @brief Deklaracje interfejsu loadera (rezerwowe).
 *
 * Plik pozostawiony pod przyszłe deklaracje publiczne związane z loaderem.
 */

/**
 * @file loader.h
 * @brief Deklaracje funkcji loadera.
 */

#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <sys/types.h>
#include <sys/ipc.h>
#include <semaphore.h>

#include "config/shm.h"

/**
 * @brief Uruchamia proces potomny z opcjonalnym argumentem.
 * @param nazwa Ścieżka/nazwa wykonywalna.
 * @param argument Argument przekazywany do procesu.
 * @return PID procesu potomnego lub -1 przy błędzie.
 */
pid_t uruchomProces(const std::string& nazwa, const std::string& argument = "");

/**
 * @brief Loguje stan symulacji (debug).
 * @param stan Wskaźnik na stan współdzielony.
 * @param wygenerowani Liczba wygenerowanych petentów.
 * @param obsluzeni Liczba obsłużonych petentów.
 */
void logDev(const SharedState* stan, int wygenerowani, int obsluzeni);

/**
 * @brief Uruchamia urzędników w poszczególnych wydziałach.
 * @param pids Wektor na PID-y uruchomionych procesów.
 */
void uruchomUrzednikow(std::vector<pid_t>& pids);

/**
 * @brief Steruje liczbą aktywnych biletomatów.
 * @param mqidOther ID kolejki „other”.
 * @param stan Stan współdzielony.
 * @param semaphore Semafor stanu współdzielonego.
 * @param otherQueueSem Semafor kolejki „other”.
 */
void sterujBiletomatami(int mqidOther, SharedState* stan, sem_t* semaphore, sem_t* otherQueueSem);

/**
 * @brief Wątek zarządzania petentami.
 * @param stan Stan współdzielony.
 * @param semaphore Semafor stanu.
 * @param wygenerowani Licznik wygenerowanych petentów.
 * @param petentPids Lista PID-ów petentów.
 * @param petentMutex Mutex listy PID-ów.
 */
void zarzadzaniePetentami(SharedState* stan, sem_t* semaphore, std::atomic<int>* wygenerowani,
	std::vector<pid_t>* petentPids, std::mutex* petentMutex);

/**
 * @brief Inicjalizuje segment pamięci współdzielonej.
 * @return ID segmentu.
 */
int initSharedMemory();

/**
 * @brief Inicjalizuje kolejkę komunikatów.
 * @param key Klucz kolejki.
 * @return ID kolejki.
 */
int initMessageQueue(key_t key);

/**
 * @brief Inicjalizuje semafor stanu współdzielonego.
 * @return Wskaźnik do semafora.
 */
sem_t* initSemaphore();

/**
 * @brief Obsługuje komunikaty z kolejek wejścia/wyjścia i „other”.
 */
void obsluzKomunikaty(int mqidPetent, int mqidPetentExit, int mqidOther, SharedState* stan, sem_t* semaphore,
	sem_t* otherQueueSem, sem_t* petentQueueSem, std::atomic<int>* obsluzeni);

/**
 * @brief Odrzuca petentów oczekujących w kolejce wejścia.
 * @param mqidPetent ID kolejki wejścia.
 * @param loaderPid PID loadera.
 * @param petentQueueSem Semafor kolejki wejścia.
 */
void odrzucOczekujacychPetentow(int mqidPetent, int loaderPid, sem_t* petentQueueSem);

/**
 * @brief Punkt wejścia procesu loadera.
 */
int main();
