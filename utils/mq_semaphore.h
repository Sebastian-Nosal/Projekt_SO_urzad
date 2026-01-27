/**
 * @file mq_semaphore.h
 * @brief Narzędzia do limitowania przepustowości kolejek IPC.
 */

#pragma once

#include <semaphore.h>

/**
 * @brief Otwiera semafor dla kolejki procesów pomocniczych.
 * @param reset Czy usunąć i odtworzyć semafor.
 * @return Wskaźnik do semafora.
 */
sem_t* openOtherQueueSemaphore(bool reset);

/**
 * @brief Zamyka semafor kolejki procesów pomocniczych.
 * @param sem Wskaźnik do semafora.
 */
void closeOtherQueueSemaphore(sem_t* sem);

/**
 * @brief Ustawia ID kolejki „other”.
 * @param mqidOther ID kolejki.
 */
void setOtherQueueId(int mqidOther);

/**
 * @brief Blokuje wysyłkę, dopóki jest miejsce w kolejce „other”.
 * @param sem Wskaźnik do semafora.
 * @return `true` gdy można wysłać.
 */
bool otherQueueWaitToSend(sem_t* sem);

/**
 * @brief Próbuje zarezerwować miejsce w kolejce „other” bez blokowania.
 * @param sem Wskaźnik do semafora.
 * @return `true` gdy można wysłać.
 */
bool otherQueueTryWaitToSend(sem_t* sem);

/**
 * @brief Zwolnienie slotu w kolejce „other” (no-op dla licznika opartego o msgctl).
 * @param sem Wskaźnik do semafora.
 */
void otherQueueReleaseSlot(sem_t* sem);

/**
 * @brief Otwiera semafor dla kolejki wejściowej petentów (legacy).
 * @param reset Czy usunąć i odtworzyć semafor.
 * @return Wskaźnik do semafora.
 */
sem_t* openPetentQueueSemaphore(bool reset);

/**
 * @brief Zamyka semafor kolejki petentów.
 * @param sem Wskaźnik do semafora.
 */
void closePetentQueueSemaphore(sem_t* sem);

/**
 * @brief Ustawia ID kolejki wejściowej petentów.
 * @param mqidPetent ID kolejki.
 */
void setPetentQueueId(int mqidPetent);

/**
 * @brief Blokuje wysyłkę do kolejki wejściowej petentów.
 * @param sem Wskaźnik do semafora.
 * @return `true` gdy można wysłać.
 */
bool petentQueueWaitToSend(sem_t* sem);

/**
 * @brief Próbuje zarezerwować miejsce w kolejce wejściowej petentów bez blokowania.
 * @param sem Wskaźnik do semafora.
 * @return `true` gdy można wysłać.
 */
bool petentQueueTryWaitToSend(sem_t* sem);

/**
 * @brief Zwolnienie slotu kolejki wejściowej petentów.
 * @param sem Wskaźnik do semafora.
 */
void petentQueueReleaseSlot(sem_t* sem);
