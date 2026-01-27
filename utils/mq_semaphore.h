#pragma once

#include <semaphore.h>

// Queue capacity limiter for "other processes queue" (send-side only)
sem_t* openOtherQueueSemaphore(bool reset);
void closeOtherQueueSemaphore(sem_t* sem);
void setOtherQueueId(int mqidOther);
bool otherQueueWaitToSend(sem_t* sem);
bool otherQueueTryWaitToSend(sem_t* sem);
void otherQueueReleaseSlot(sem_t* sem);

// Queue capacity limiter for petent queue (send-side + release on receive)
sem_t* openPetentQueueSemaphore(bool reset);
void closePetentQueueSemaphore(sem_t* sem);
void setPetentQueueId(int mqidPetent);
bool petentQueueWaitToSend(sem_t* sem);
bool petentQueueTryWaitToSend(sem_t* sem);
void petentQueueReleaseSlot(sem_t* sem);
