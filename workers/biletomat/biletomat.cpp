#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <string>
#include <cerrno>
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

void wyslijMonitoring(int mqidOther, int senderId, const std::string& text) {
	Message msg{};
	msg.mtype = static_cast<long>(ProcessMqType::Monitoring);
	msg.senderId = senderId;
	msg.receiverId = 0;
	msg.group = MessageGroup::Monitoring;
	msg.messageType.monitoringType = MonitoringMessagesEnum::Log;
	std::snprintf(msg.data3, sizeof(msg.data3), "%s", text.c_str());
	if (msgsnd(mqidOther, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
		perror("msgsnd failed");
	}
}

void logZmianaAktywnosci(int mqidOther, int pid, bool active, SharedState* state, sem_t* semaphore) {
	int queueLen = 0;
	int available = 0;
	sem_wait(semaphore);
	queueLen = state->ticketQueueLen;
	available = MAX_CLIENTS_IN_BUILDING - state->clientsInBuilding;
	sem_post(semaphore);
	if (available < 0) {
		available = 0;
	}

	std::string status = active ? "aktywowany" : "dezaktywowany";
	std::string log = "Biletomat " + std::to_string(pid) + " " + status + ". kolejka=" + std::to_string(queueLen)
		+ " wolne_miejsca=" + std::to_string(available);
	wyslijMonitoring(mqidOther, pid, log);
}

std::string wydzialZKodu(int kod) {
	switch (kod) {
		case 1: return "SA";
		case 2: return "SC";
		case 3: return "KM";
		case 4: return "ML";
		case 5: return "PD";
		default: return "SA";
	}
}

int pobierzKolejke(SharedState* state, sem_t* semaphore) {
	int queueLen = 0;
	sem_wait(semaphore);
	queueLen = state->ticketQueueLen;
	sem_post(semaphore);
	return queueLen;
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

		bool newActive = (index < desired);
		if (newActive != active) {
			active = newActive;
			logZmianaAktywnosci(mqidOther, getpid(), active, state, semaphore);
		}
		if (!active) {
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			continue;
		}

		Message msg{};
		while (true) {
			if (msgrcv(mqidOther, &msg, sizeof(msg) - sizeof(long), static_cast<long>(ProcessMqType::Biletomat), IPC_NOWAIT) != -1) {
				if (msg.group != MessageGroup::Biletomat) {
					continue;
				}

				if (msg.messageType.biletomatType == BiletomatMessagesEnum::Aktywuj ||
					msg.messageType.biletomatType == BiletomatMessagesEnum::Dezaktywuj) {
					if (msg.data1 >= 1 && msg.data1 <= TICKET_MACHINES_MAX) {
						bool newActiveFromMsg = (index < msg.data1);
						if (newActiveFromMsg != active) {
							active = newActiveFromMsg;
							logZmianaAktywnosci(mqidOther, getpid(), active, state, semaphore);
						}
						wyslijMonitoring(mqidOther, getpid(), "Biletomat " + std::to_string(getpid()) + " zmiana aktywnosci -> " + std::to_string(msg.data1));
						std::cout << "{biletomat, " << getpid() << "} active=" << active << " desired=" << msg.data1 << std::endl;
					}
					continue;
				}

				if (msg.messageType.biletomatType == BiletomatMessagesEnum::PetentCzekaNaBilet) {
					std::cout << "{biletomat, " << getpid() << "} petent waiting" << std::endl;
					std::string wydzial = wydzialZKodu(msg.data1);
					int queueLen = pobierzKolejke(state, semaphore);
					wyslijMonitoring(mqidOther, getpid(), "Biletomat " + std::to_string(getpid()) + " petent=" + std::to_string(msg.senderId)
						+ " czeka na bilet wydzial=" + wydzial + " kolejka=" + std::to_string(queueLen));

				Message response{};
				response.mtype = msg.senderId;
				response.senderId = getpid();
				response.receiverId = msg.senderId;
				response.group = MessageGroup::Petent;
				response.messageType.petentType = PetentMessagesEnum::OtrzymanoBilet;

				std::this_thread::sleep_for(std::chrono::milliseconds(500));

				if (msgsnd(mqidPetent, &response, sizeof(response) - sizeof(long), 0) == -1) {
					perror("msgsnd failed");
				}
				std::cout << "{biletomat, " << getpid() << "} ticket issued" << std::endl;
				wyslijMonitoring(mqidOther, getpid(), "Biletomat " + std::to_string(getpid()) + " wydal bilet petent=" + std::to_string(msg.senderId)
					+ " wydzial=" + wydzial);

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
					case 1: deptType = DepartmentMqType::SA; break;
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
				std::cout << "{biletomat, " << getpid() << "} send to urzednik from=" << msg.senderId
				          << " wydzial=" << wydzial << std::endl;
				wyslijMonitoring(mqidOther, getpid(), "Biletomat " + std::to_string(getpid()) + " przekazal petenta=" + std::to_string(msg.senderId)
					+ " do urzednika wydzial=" + wydzial);
				}
				continue;
			}
			if (errno == EINTR) {
				continue;
			}
			break;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	shmdt(state);
	sem_close(semaphore);
	return 0;
}
