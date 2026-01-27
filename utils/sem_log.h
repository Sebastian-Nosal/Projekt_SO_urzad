/**
 * @file sem_log.h
 * @brief Pomocnicze funkcje logujące stan semaforów.
 */

#pragma once

#include <semaphore.h>
#include <unistd.h>
#include <iostream>
#include <chrono>
#include <cerrno>

/**
 * @brief Sprawdza, czy można zalogować kolejną informację (rate limit).
 * @return `true` jeśli logowanie jest dozwolone.
 */
inline bool semShouldLog() {
    using clock = std::chrono::steady_clock;
    static thread_local clock::time_point last = clock::now() - std::chrono::seconds(1);
    auto now = clock::now();
    if (now - last < std::chrono::milliseconds(200)) {
        return false;
    }
    last = now;
    return true;
}

/**
 * @brief Loguje stan semafora.
 * @param tag Etykieta operacji (np. WAIT, ACQUIRED).
 * @param name Nazwa semafora.
 * @param where Miejsce wywołania (np. nazwa funkcji).
 * @param value Wartość semafora.
 */
inline void semLogState(const char* tag, const char* name, const char* where, int value) {
    if (!semShouldLog()) {
        return;
    }
    const char* stan = (value <= 0) ? "ZAMKNIETY" : "OTWARTY";
    /*std::cout << "\033[33m[sem]\033[0m pid=" << getpid()
              << " name=" << name
              << " where=" << where
              << " action=" << tag
              << " stan=" << stan
              << " wartosc=" << value
              << std::endl;
    */
}

/**
 * @brief Czeka na semafor i loguje stan, gdy blokuje się.
 * @param sem Wskaźnik do semafora.
 * @param name Nazwa semafora.
 * @param where Miejsce wywołania.
 */
inline void semWaitLogged(sem_t* sem, const char* name, const char* where) {
    if (!sem || sem == SEM_FAILED) {
        return;
    }
    if (sem_trywait(sem) == 0) {
        return;
    }
    if (errno != EAGAIN && errno != EINTR) {
        return;
    }
    int value = 0;
    sem_getvalue(sem, &value);
    semLogState("WAIT", name, where, value);
    while (sem_wait(sem) == -1) {
        if (errno == EINTR) {
            continue;
        }
        break;
    }
    sem_getvalue(sem, &value);
    semLogState("ACQUIRED", name, where, value);
}

/**
 * @brief Zwolnienie semafora.
 * @param sem Wskaźnik do semafora.
 * @param name Nazwa semafora.
 * @param where Miejsce wywołania.
 */
inline void semPostLogged(sem_t* sem, const char* name, const char* where) {
    sem_post(sem);
}
