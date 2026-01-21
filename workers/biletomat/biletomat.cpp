#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config/config.h"
#include "config/messages.h"
#include "config/shm.h"

namespace {
volatile sig_atomic_t g_running = 1;

void handleSigint(int) {
	g_running = 0;
}

int initMessageQueue(key_t key) {
	int mqid = msgget(key, IPC_CREAT | 0666);
	if (mqid == -1) {
		perror("msgget failed");
		exit(EXIT_FAILURE);
	}
	return mqid;
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

int main(int argc, char** argv) {
	std::signal(SIGINT, handleSigint);

	int index = 0;
	if (argc > 1) {
		index = std::atoi(argv[1]);
	}

	int mqidPetent = initMessageQueue(MQ_KEY);
	int mqidOther = initMessageQueue(MQ_KEY + 1);

	int shmid = shmget(SHM_KEY, sizeof(SharedState), IPC_CREAT | 0666);
	if (shmid == -1) {
		perror("shmget failed");
		return 1;
	}

	SharedState* state = static_cast<SharedState*>(shmat(shmid, nullptr, 0));
	if (state == reinterpret_cast<SharedState*>(-1)) {
		perror("shmat failed");
		return 1;
	}

	sem_t* semaphore = initSemaphore();

	bool active = false;

	while (g_running) {
		sem_wait(semaphore);
		int open = state->officeOpen;
		int desired = state->activeTicketMachines;
		sem_post(semaphore);

		if (!open) {
			break;
		}

		active = (index < desired);
		if (!active) {
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			continue;
		}

		Message msg{};
		while (msgrcv(mqidOther, &msg, sizeof(msg) - sizeof(long), static_cast<long>(ProcessMqType::Biletomat), IPC_NOWAIT) != -1) {
			if (msg.group != MessageGroup::Biletomat) {
				continue;
			}

			if (msg.messageType.biletomatType == BiletomatMessagesEnum::Aktywuj ||
				msg.messageType.biletomatType == BiletomatMessagesEnum::Dezaktywuj) {
				if (msg.data1 >= 1 && msg.data1 <= TICKET_MACHINES_MAX) {
					active = (index < msg.data1);
					std::cout << "{biletomat, " << getpid() << "} active=" << active << " desired=" << msg.data1 << std::endl;
				}
				continue;
			}

			if (msg.messageType.biletomatType == BiletomatMessagesEnum::PetentCzekaNaBilet) {
				std::cout << "{biletomat, " << getpid() << "} petent waiting" << std::endl;

				Message notifyIn{};
				notifyIn.mtype = static_cast<long>(ProcessMqType::Loader);
				notifyIn.senderId = msg.senderId;
				notifyIn.receiverId = 0;
				notifyIn.group = MessageGroup::Biletomat;
				notifyIn.messageType.biletomatType = BiletomatMessagesEnum::PetentCzekaNaBilet;
				msgsnd(mqidOther, &notifyIn, sizeof(notifyIn) - sizeof(long), 0);
				std::cout << "{biletomat, " << getpid() << "} send to loader from=" << msg.senderId << std::endl;

				Message response{};
				response.mtype = msg.senderId;
				response.senderId = getpid();
				response.receiverId = msg.senderId;
				response.group = MessageGroup::Petent;
				response.messageType.petentType = PetentMessagesEnum::OtrzymanoBilet;

				if (msgsnd(mqidPetent, &response, sizeof(response) - sizeof(long), 0) == -1) {
					perror("msgsnd failed");
				}
				std::cout << "{biletomat, " << getpid() << "} ticket issued" << std::endl;

				Message notifyOut{};
				notifyOut.mtype = static_cast<long>(ProcessMqType::Loader);
				notifyOut.senderId = msg.senderId;
				notifyOut.receiverId = 0;
				notifyOut.group = MessageGroup::Biletomat;
				notifyOut.messageType.biletomatType = BiletomatMessagesEnum::PetentOdebralBilet;
				msgsnd(mqidOther, &notifyOut, sizeof(notifyOut) - sizeof(long), 0);
				std::cout << "{biletomat, " << getpid() << "} send to loader ticket out from=" << msg.senderId << std::endl;

				Message toOfficer{};
				DepartmentMqType deptType = DepartmentMqType::SA;
				switch (msg.data1) {
					case 2: deptType = DepartmentMqType::SC; break;
					case 3: deptType = DepartmentMqType::KM; break;
					case 4: deptType = DepartmentMqType::ML; break;
					case 5: deptType = DepartmentMqType::PD; break;
					default: deptType = DepartmentMqType::SA; break;
				}
				toOfficer.mtype = static_cast<long>(deptType);
				toOfficer.senderId = msg.senderId;
				toOfficer.receiverId = 0;
				toOfficer.group = MessageGroup::Biletomat;
				toOfficer.messageType.biletomatType = BiletomatMessagesEnum::WydanoBiletCzekaj;
				msgsnd(mqidOther, &toOfficer, sizeof(toOfficer) - sizeof(long), 0);
				std::cout << "{biletomat, " << getpid() << "} send to urzednik from=" << msg.senderId << std::endl;
			}
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	shmdt(state);
	sem_close(semaphore);
	return 0;
}
