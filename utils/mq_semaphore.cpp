#include "utils/mq_semaphore.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/msg.h>
#include <thread>

#include "config/config.h"

namespace {
int g_otherQueueId = -1;
int g_petentQueueId = -1;
}

sem_t* openOtherQueueSemaphore(bool reset) {
    if (reset) {
        sem_unlink(OTHER_QUEUE_SEM_NAME);
    }
    sem_t* sem = sem_open(OTHER_QUEUE_SEM_NAME, O_CREAT, 0666, OTHER_QUEUE_LIMIT);
    if (sem == SEM_FAILED) {
        perror("sem_open other queue failed");
        exit(EXIT_FAILURE);
    }
    return sem;
}

void closeOtherQueueSemaphore(sem_t* sem) {
    if (sem && sem != SEM_FAILED) {
        sem_close(sem);
    }
}

void setOtherQueueId(int mqidOther) {
    g_otherQueueId = mqidOther;
}

bool otherQueueWaitToSend(sem_t* sem) {
    (void)sem;
    if (g_otherQueueId < 0) {
        return true;
    }
    while (true) {
        msqid_ds ds{};
        if (msgctl(g_otherQueueId, IPC_STAT, &ds) == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("msgctl IPC_STAT failed");
            return false;
        }
        if (ds.msg_qnum < OTHER_QUEUE_LIMIT) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool otherQueueTryWaitToSend(sem_t* sem) {
    (void)sem;
    if (g_otherQueueId < 0) {
        return true;
    }
    msqid_ds ds{};
    if (msgctl(g_otherQueueId, IPC_STAT, &ds) == -1) {
        return false;
    }
    return ds.msg_qnum < OTHER_QUEUE_LIMIT;
}

void otherQueueReleaseSlot(sem_t* sem) {
    (void)sem;
}

sem_t* openPetentQueueSemaphore(bool reset) {
    if (reset) {
        sem_unlink(PETENT_QUEUE_SEM_NAME);
    }
    sem_t* sem = sem_open(PETENT_QUEUE_SEM_NAME, O_CREAT, 0666, ENTRY_QUEUE_LIMIT);
    if (sem == SEM_FAILED) {
        perror("sem_open petent queue failed");
        exit(EXIT_FAILURE);
    }
    return sem;
}

void closePetentQueueSemaphore(sem_t* sem) {
    if (sem && sem != SEM_FAILED) {
        sem_close(sem);
    }
}

void setPetentQueueId(int mqidPetent) {
    g_petentQueueId = mqidPetent;
}

bool petentQueueWaitToSend(sem_t* sem) {
    (void)sem;
    if (g_petentQueueId < 0) {
        return true;
    }
    while (true) {
        msqid_ds ds{};
        if (msgctl(g_petentQueueId, IPC_STAT, &ds) == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("msgctl IPC_STAT failed");
            return false;
        }
        if (ds.msg_qnum < ENTRY_QUEUE_LIMIT) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool petentQueueTryWaitToSend(sem_t* sem) {
    (void)sem;
    if (g_petentQueueId < 0) {
        return true;
    }
    msqid_ds ds{};
    if (msgctl(g_petentQueueId, IPC_STAT, &ds) == -1) {
        return false;
    }
    return ds.msg_qnum < ENTRY_QUEUE_LIMIT;
}

void petentQueueReleaseSlot(sem_t* sem) {
    (void)sem;
}
