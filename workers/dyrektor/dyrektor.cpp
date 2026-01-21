#include <iostream>
#include <csignal>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <unistd.h>
#include <sys/types.h>

#include "config/config.h"
#include "config/shm.h"

namespace {
volatile sig_atomic_t dziala = 1;
volatile sig_atomic_t wyslijSigusr1 = 0;
volatile sig_atomic_t wyslijSigusr2 = 0;

void obsluzSigint(int) {
	dziala = 0;
}

void obsluzSigusr1(int) {
	wyslijSigusr1 = 1;
}

void obsluzSigusr2(int) {
	wyslijSigusr2 = 1;
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

	while (dziala) {
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

		pause();
	}

	shmdt(stan);
	return 0;
}
