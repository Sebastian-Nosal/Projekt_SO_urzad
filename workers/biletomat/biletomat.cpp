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
#include "utils/mq_semaphore.h"
#include "utils/sem_log.h"

namespace {
volatile sig_atomic_t g_running = 1;
sem_t* g_otherQueueSem = nullptr;

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
	if (!otherQueueTryWaitToSend(g_otherQueueSem)) {
		return;
	}
	if (msgsnd(mqidOther, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
		perror("msgsnd failed");
		otherQueueReleaseSlot(g_otherQueueSem);
	}
}

void logZmianaAktywnosci(int mqidOther, int pid, bool active, SharedState* state, sem_t* semaphore) {
	int queueLen = 0;
	int available = 0;
	semWaitLogged(semaphore, SEMAPHORE_NAME, __func__);
	queueLen = state->ticketQueueLen;
	available = MAX_CLIENTS_IN_BUILDING - state->clientsInBuilding;
	semPostLogged(semaphore, SEMAPHORE_NAME, __func__);
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
	semWaitLogged(semaphore, SEMAPHORE_NAME, __func__);
	queueLen = state->ticketQueueLen;
	semPostLogged(semaphore, SEMAPHORE_NAME, __func__);
	return queueLen;
}

int indeksWydzialuZKodu(int kod) {
	switch (kod) {
		case 1: return 0; // SA
		case 2: return 1; // SC
		case 3: return 2; // KM
		case 4: return 3; // ML
		case 5: return 4; // PD
		default: return 0;
	}
}

int liczbaUrzednikowDlaWydzialuIdx(int idx) {
	switch (idx) {
		case 0: return NUM_SA_OFFICERS;
		case 1: return NUM_SC_OFFICERS;
		case 2: return NUM_KM_OFFICERS;
		case 3: return NUM_ML_OFFICERS;
		case 4: return NUM_PD_OFFICERS;
		default: return 0;
	}
}
}

int main(int argc, char** argv) {
	std::signal(SIGINT, handleSigint);

	int index = 0;
	if (argc > 1) {
		index = std::atoi(argv[1]);
	}

	int mqidOther = initMessageQueue(MQ_KEY_OTHER);
	setOtherQueueId(mqidOther);

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
	g_otherQueueSem = openOtherQueueSemaphore(false);

	bool active = false;

	while (g_running) {
		semWaitLogged(semaphore, SEMAPHORE_NAME, __func__);
		int open = state->officeOpen;
		int desired = state->activeTicketMachines;
		semPostLogged(semaphore, SEMAPHORE_NAME, __func__);

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
				otherQueueReleaseSlot(g_otherQueueSem);
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
						// std::cout << "{biletomat, " << getpid() << "} active=" << active << " desired=" << msg.data1 << std::endl;
					}
					continue;
				}

				if (msg.messageType.biletomatType == BiletomatMessagesEnum::PetentCzekaNaBilet) {
					// std::cout << "{biletomat, " << getpid() << "} petent waiting" << std::endl;
					bool isVip = (msg.flags & MESSAGE_FLAG_VIP) != 0;
					std::string wydzial = wydzialZKodu(msg.data1);
					int queueLen = pobierzKolejke(state, semaphore);
					wyslijMonitoring(mqidOther, getpid(), "Biletomat " + std::to_string(getpid()) + " petent=" + std::to_string(msg.senderId)
						+ " czeka na bilet wydzial=" + wydzial + " kolejka=" + std::to_string(queueLen) + (isVip ? " VIP" : ""));

					int idx = indeksWydzialuZKodu(msg.data1);
					bool exhausted = false;
					semWaitLogged(semaphore, SEMAPHORE_NAME, __func__);
					if (idx >= 0 && idx < 5) {
						int total = liczbaUrzednikowDlaWydzialuIdx(idx);
						int exhaustedCount = state->exhaustedCount[idx];
						exhausted = (total <= 0) || (exhaustedCount >= total) || (state->exhaustedDept[idx] != 0);
					}
					semPostLogged(semaphore, SEMAPHORE_NAME, __func__);

					if (exhausted) {
						Message response{};
						response.mtype = 1;
						response.senderId = getpid();
						response.receiverId = msg.senderId;
						response.replyQueueId = msg.replyQueueId;
						response.flags = msg.flags;
						response.group = MessageGroup::Petent;
						response.messageType.petentType = PetentMessagesEnum::Odprawiony;
						if (msg.replyQueueId > 0) {
							if (msgsnd(msg.replyQueueId, &response, sizeof(response) - sizeof(long), 0) == -1) {
								perror("msgsnd failed");
							}
						}

						Message notifyOut{};
						notifyOut.mtype = static_cast<long>(ProcessMqType::Loader);
						notifyOut.senderId = msg.senderId;
						notifyOut.receiverId = 0;
						notifyOut.group = MessageGroup::Biletomat;
						notifyOut.messageType.biletomatType = BiletomatMessagesEnum::PetentOdebralBilet;
						if (otherQueueWaitToSend(g_otherQueueSem)) {
							if (msgsnd(mqidOther, &notifyOut, sizeof(notifyOut) - sizeof(long), 0) == -1) {
								perror("msgsnd failed");
								otherQueueReleaseSlot(g_otherQueueSem);
							}
						}

						wyslijMonitoring(mqidOther, getpid(), "Biletomat " + std::to_string(getpid()) + " odmowa biletu petent=" + std::to_string(msg.senderId)
							+ " wydzial=" + wydzial + " (wyczerpany)");
						continue;
					}

				Message response{};
				response.mtype = 1;
				response.senderId = getpid();
				response.receiverId = msg.senderId;
				response.replyQueueId = msg.replyQueueId;
				response.flags = msg.flags;
				response.group = MessageGroup::Petent;
				response.messageType.petentType = PetentMessagesEnum::OtrzymanoBilet;

				std::this_thread::sleep_for(std::chrono::milliseconds(500));

				if (msg.replyQueueId > 0) {
					if (msgsnd(msg.replyQueueId, &response, sizeof(response) - sizeof(long), 0) == -1) {
						perror("msgsnd failed");
					}
				}
				// std::cout << "{biletomat, " << getpid() << "} ticket issued" << std::endl;
				wyslijMonitoring(mqidOther, getpid(), "Biletomat " + std::to_string(getpid()) + " wydal bilet petent=" + std::to_string(msg.senderId)
					+ " wydzial=" + wydzial);

				Message notifyOut{};
				notifyOut.mtype = static_cast<long>(ProcessMqType::Loader);
				notifyOut.senderId = msg.senderId;
				notifyOut.receiverId = 0;
				notifyOut.group = MessageGroup::Biletomat;
				notifyOut.messageType.biletomatType = BiletomatMessagesEnum::PetentOdebralBilet;
				if (otherQueueWaitToSend(g_otherQueueSem)) {
					if (msgsnd(mqidOther, &notifyOut, sizeof(notifyOut) - sizeof(long), 0) == -1) {
						perror("msgsnd failed");
						otherQueueReleaseSlot(g_otherQueueSem);
					}
				}
				// std::cout << "{biletomat, " << getpid() << "} send to loader ticket out from=" << msg.senderId << std::endl;

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
				toOfficer.mtype = static_cast<long>(deptType) + (isVip ? VIP_DEPT_MTYPE_OFFSET : 0);
				toOfficer.senderId = msg.senderId;
				toOfficer.receiverId = 0;
				toOfficer.replyQueueId = msg.replyQueueId;
				toOfficer.flags = msg.flags;
				toOfficer.group = MessageGroup::Biletomat;
				toOfficer.messageType.biletomatType = BiletomatMessagesEnum::WydanoBiletCzekaj;
				if (otherQueueWaitToSend(g_otherQueueSem)) {
					if (msgsnd(mqidOther, &toOfficer, sizeof(toOfficer) - sizeof(long), 0) == -1) {
						perror("msgsnd failed");
						otherQueueReleaseSlot(g_otherQueueSem);
					}
				}
				// std::cout << "{biletomat, " << getpid() << "} send to urzednik from=" << msg.senderId
				//           << " wydzial=" << wydzial << std::endl;
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
