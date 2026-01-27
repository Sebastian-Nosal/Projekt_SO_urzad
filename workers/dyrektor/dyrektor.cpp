#include <iostream>
#include <csignal>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <sys/types.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <thread>
#include <chrono>

#include "config/config.h"
#include "config/shm.h"
#include "utils/sem_log.h"

namespace {
volatile sig_atomic_t dziala = 1;
volatile sig_atomic_t wyslijSigusr1 = 0;
volatile sig_atomic_t wyslijSigusr2 = 0;

void obsluzSigint(int) {
	dziala = 0;
	wyslijSigusr1 = 1;
	wyslijSigusr2 = 1;
}

void obsluzSigusr1(int) {
	wyslijSigusr1 = 1;
}

void obsluzSigusr2(int) {
	wyslijSigusr2 = 1;
}

sem_t* initSemaphore() {
	sem_t* semaphore = sem_open(SEMAPHORE_NAME, O_CREAT, 0666, 1);
	if (semaphore == SEM_FAILED) {
		perror("sem_open failed");
		exit(EXIT_FAILURE);
	}
	return semaphore;
}
}

int main() {
	std::signal(SIGINT, obsluzSigint);
	std::signal(SIGUSR1, obsluzSigusr1);
	std::signal(SIGUSR2, obsluzSigusr2);

	int shmid = shmget(SHM_KEY, sizeof(SharedState), IPC_CREAT | 0666);
	if (shmid == -1) {
		perror("shmget failed");
		return 1;
	}

	SharedState* stan = static_cast<SharedState*>(shmat(shmid, nullptr, 0));
	if (stan == reinterpret_cast<SharedState*>(-1)) {
		perror("shmat failed");
		return 1;
	}

	sem_t* semaphore = initSemaphore();
	bool shutdownSent = false;

	while (dziala || wyslijSigusr1 || wyslijSigusr2) {
		if (wyslijSigusr1) {
			wyslijSigusr1 = 0;
			// SIGUSR1: urzednicy koncza po biezacym petencie
			kill(0, SIGUSR1);
		}

		if (wyslijSigusr2) {
			wyslijSigusr2 = 0;
			// SIGUSR2: petenci natychmiast opuszczaja budynek
			kill(0, SIGUSR2);
		}

		semWaitLogged(semaphore, SEMAPHORE_NAME, __func__);
		int open = stan->officeOpen;
		int livePetents = stan->livePetents;
		int activeOfficers = stan->activeOfficers;
		semPostLogged(semaphore, SEMAPHORE_NAME, __func__);

		if (!open && !shutdownSent) {
			shutdownSent = true;
			// koniec symulacji: petenci SIGUSR2, urzednicy SIGUSR1
			kill(0, SIGUSR2);
			kill(0, SIGUSR1);
		}

		if (shutdownSent && livePetents == 0 && activeOfficers == 0) {
			break;
		}

		if (!open && !dziala) {
			break;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}

	sem_close(semaphore);

	shmdt(stan);
	return 0;
}
