#include <iostream>
#include <chrono>
#include <thread>
#include <csignal>
#include <cstring>
#include <condition_variable>
#include <mutex>
#include <deque>
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
#include "utils/losowosc.h"

namespace {
volatile sig_atomic_t wymuszoneWyjscie = 0;

void obsluzSigusr2(int) {
	wymuszoneWyjscie = 1;
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

struct ChildLogQueue {
	std::mutex mutex;
	std::condition_variable cv;
	std::deque<std::string> queue;
	bool done = false;
};

void dzieckoPracuj(ChildLogQueue* state, int mqidOther, int parentPid) {
	if (!state) {
		return;
	}
	while (true) {
		std::unique_lock<std::mutex> lock(state->mutex);
		state->cv.wait(lock, [&]() { return state->done || !state->queue.empty(); });
		if (state->done && state->queue.empty()) {
			break;
		}
		std::string text = std::move(state->queue.front());
		state->queue.pop_front();
		lock.unlock();

		std::string childLog = "Petent " + std::to_string(parentPid) + " robi " + text + " z dzieckiem";
		wyslijMonitoring(mqidOther, parentPid, childLog);
	}
}

void logujZAdnotacja(int mqidOther, int senderId, const std::string& text, bool maDziecko, ChildLogQueue* childState) {
	wyslijMonitoring(mqidOther, senderId, text);
	if (maDziecko && childState) {
		{
			std::lock_guard<std::mutex> lock(childState->mutex);
			childState->queue.push_back(text);
		}
		childState->cv.notify_one();
	}
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

int losujWydzialStartowy() {
	int los = losujIlosc(1, 100);
	int progSA = static_cast<int>(PERCENT_SA * 100.0);
	int progSC = progSA + static_cast<int>(PERCENT_SC * 100.0);
	int progKM = progSC + static_cast<int>(PERCENT_KM * 100.0);
	int progML = progKM + static_cast<int>(PERCENT_ML * 100.0);
	int progPD = progML + static_cast<int>(PERCENT_PD * 100.0);

	if (los <= progSA) return 1; // SA
	if (los <= progSC) return 2; // SC
	if (los <= progKM) return 3; // KM
	if (los <= progML) return 4; // ML
	if (los <= progPD) return 5; // PD
	return 1; // fallback SA
}

void wyslijDoBiletomatu(int mqidOther, int senderId, int receiverId, BiletomatMessagesEnum type, int data1 = 0, int data2 = 0) {
	Message msg{};
	msg.mtype = static_cast<long>(ProcessMqType::Biletomat);
	msg.senderId = senderId;
	msg.receiverId = receiverId;
	msg.group = MessageGroup::Biletomat;
	msg.messageType.biletomatType = type;
	msg.data1 = data1;
	msg.data2 = data2;
	if (msgsnd(mqidOther, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
		perror("msgsnd failed");
	}
}

void zglosKolejkeBiletowa(int mqidOther, int senderId) {
	Message notify{};
	notify.mtype = static_cast<long>(ProcessMqType::Loader);
	notify.senderId = senderId;
	notify.receiverId = 0;
	notify.group = MessageGroup::Biletomat;
	notify.messageType.biletomatType = BiletomatMessagesEnum::PetentCzekaNaBilet;
	if (msgsnd(mqidOther, &notify, sizeof(notify) - sizeof(long), 0) == -1) {
		perror("msgsnd failed");
	}
}

bool recvBlocking(int mqid, Message& msg, long type) {
	while (true) {
		if (msgrcv(mqid, &msg, sizeof(msg) - sizeof(long), type, 0) != -1) {
			return true;
		}
		if (errno == EINTR) {
			continue;
		}
		perror("msgrcv failed");
		return false;
	}
}

bool odbierzWezwanieUrzednika(int mqidPetent, int selfPid) {
	Message msg{};
	if (!recvBlocking(mqidPetent, msg, selfPid)) {
		return false;
	}
	return msg.group == MessageGroup::Petent && msg.messageType.petentType == PetentMessagesEnum::WezwanoDoUrzednika;
}

void wyslijDoKasy(int mqidOther, int senderId, int receiverId, int data1 = 0, int data2 = 0) {
	Message msg{};
	msg.mtype = static_cast<long>(ProcessMqType::Kasa);
	msg.senderId = senderId;
	msg.receiverId = receiverId;
	msg.group = MessageGroup::Petent;
	msg.messageType.petentType = PetentMessagesEnum::IdzDoKasy;
	msg.data1 = data1;
	msg.data2 = data2;
	if (msgsnd(mqidOther, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
		perror("msgsnd failed");
	}
}

bool odbierzObsluzenie(int mqidPetent, int selfPid) {
	Message msg{};
	if (!recvBlocking(mqidPetent, msg, selfPid)) {
		return false;
	}
	return msg.group == MessageGroup::Petent && msg.messageType.petentType == PetentMessagesEnum::Obsluzony;
}

bool odbierzOdprawe(int mqidPetent, int selfPid) {
	Message msg{};
	if (!recvBlocking(mqidPetent, msg, selfPid)) {
		return false;
	}
	return msg.group == MessageGroup::Petent && msg.messageType.petentType == PetentMessagesEnum::Odprawiony;
}

bool odbierzBilet(int mqidPetent, int selfPid) {
	Message msg{};
	if (!recvBlocking(mqidPetent, msg, selfPid)) {
		return false;
	}
	return msg.group == MessageGroup::Petent && msg.messageType.petentType == PetentMessagesEnum::OtrzymanoBilet;
}

bool odbierzKomunikatPetenta(int mqidPetent, int selfPid, Message& msg) {
	return recvBlocking(mqidPetent, msg, selfPid);
}
}

int main() {
	std::signal(SIGUSR2, obsluzSigusr2);

	int mqidPetent = initMessageQueue(MQ_KEY);
	int mqidOther = initMessageQueue(MQ_KEY + 1);

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

	sem_wait(semaphore);
	int loaderPid = stan->loaderPid;
	sem_post(semaphore);

	if (loaderPid <= 0) {
		std::cerr << "Loader PID not set." << std::endl;
		shmdt(stan);
		return 1;
	}

	bool maDziecko = (losujIlosc(1, 100) <= CHILD_CHANCE_PERCENT);
	int zajeteMiejsca = maDziecko ? 2 : 1;
	ChildLogQueue childState;
	std::thread childThread;
	if (maDziecko) {
		childThread = std::thread(dzieckoPracuj, &childState, mqidOther, getpid());
	}

	logujZAdnotacja(mqidOther, getpid(), "petent created", maDziecko, &childState);
	std::cout << "{petent, " << getpid() << "} created child=" << (maDziecko ? 1 : 0)
	          << " places=" << zajeteMiejsca << std::endl;

	Message request{};
	request.mtype = loaderPid;
	request.senderId = getpid();
	request.receiverId = loaderPid;
	request.group = MessageGroup::Loader;
	request.messageType.loaderType = LoaderMessagesEnum::NowyPetent;
	request.data2 = zajeteMiejsca;
	std::cout << "{petent, " << getpid() << "} request_entry loader=" << loaderPid
	          << " places=" << zajeteMiejsca << std::endl;

	if (msgsnd(mqidPetent, &request, sizeof(request) - sizeof(long), 0) == -1) {
		perror("msgsnd failed");
		shmdt(stan);
		if (maDziecko) {
			{
				std::lock_guard<std::mutex> lock(childState.mutex);
				childState.done = true;
			}
			childState.cv.notify_one();
			childThread.join();
		}
		return 1;
	}

	Message response{};
	bool enteredBuilding = false;
	while (true) {
		if (!recvBlocking(mqidPetent, response, getpid())) {
			shmdt(stan);
			if (maDziecko) {
				{
					std::lock_guard<std::mutex> lock(childState.mutex);
					childState.done = true;
				}
				childState.cv.notify_one();
				childThread.join();
			}
			return 1;
		}
		std::cout << "{petent, " << getpid() << "} recv from pid=" << response.senderId << std::endl;

		if (response.group != MessageGroup::Petent) {
			continue;
		}

		if (response.messageType.petentType == PetentMessagesEnum::WejdzDoBudynku) {
			logujZAdnotacja(mqidOther, getpid(), "petent entered building", maDziecko, &childState);
			std::cout << "{petent, " << getpid() << "} entered building by loader=" << response.senderId << std::endl;
			enteredBuilding = true;
			break;
		}

		if (response.messageType.petentType == PetentMessagesEnum::Odprawiony) {
			logujZAdnotacja(mqidOther, getpid(), "petent denied entry", maDziecko, &childState);
			std::cout << "{petent, " << getpid() << "} denied entry by loader=" << response.senderId << std::endl;

			Message exitMsg{};
			exitMsg.mtype = loaderPid + 1;
			exitMsg.senderId = getpid();
			exitMsg.receiverId = loaderPid;
			exitMsg.group = MessageGroup::Loader;
			exitMsg.messageType.loaderType = LoaderMessagesEnum::PetentOpuszczaBudynek;
			exitMsg.data1 = 0;
			exitMsg.data2 = 0;

			if (msgsnd(mqidPetent, &exitMsg, sizeof(exitMsg) - sizeof(long), 0) == -1) {
				perror("msgsnd failed");
			}

			shmdt(stan);
			if (maDziecko) {
				{
					std::lock_guard<std::mutex> lock(childState.mutex);
					childState.done = true;
				}
				childState.cv.notify_one();
				childThread.join();
			}
			return 0;
		}
	}

	// request ticket for department based on probabilities
	int currentDept = losujWydzialStartowy();
	std::cout << "{petent, " << getpid() << "} ticket_request dept=" << wydzialZKodu(currentDept) << std::endl;
	zglosKolejkeBiletowa(mqidOther, getpid());
	wyslijDoBiletomatu(mqidOther, getpid(), 0, BiletomatMessagesEnum::PetentCzekaNaBilet, currentDept);
	odbierzBilet(mqidPetent, getpid());
	std::cout << "{petent, " << getpid() << "} ticket_received dept=" << wydzialZKodu(currentDept) << std::endl;

	// wait for officer and handle flow
	bool zakoncz = false;
	while (!zakoncz) {
		Message msg{};
		if (!odbierzKomunikatPetenta(mqidPetent, getpid(), msg)) {
			continue;
		}

		if (msg.group != MessageGroup::Petent) {
			continue;
		}

		switch (msg.messageType.petentType) {
			case PetentMessagesEnum::WezwanoDoUrzednika:
				logujZAdnotacja(mqidOther, getpid(), "petent called by officer", maDziecko, &childState);
				std::cout << "{petent, " << getpid() << "} called by officer=" << msg.senderId << std::endl;
				break;
			case PetentMessagesEnum::IdzDoKasy:
				logujZAdnotacja(mqidOther, getpid(), "petent sent to cashier", maDziecko, &childState);
				std::cout << "{petent, " << getpid() << "} sent to cashier by officer=" << msg.senderId << std::endl;
				wyslijDoKasy(mqidOther, getpid(), 0, msg.data1, msg.data2);
				break;
			case PetentMessagesEnum::IdzDoInnegoUrzednika:
				logujZAdnotacja(mqidOther, getpid(), "petent redirected to another office", maDziecko, &childState);
				currentDept = msg.data1;
				{
					std::string wydzial = wydzialZKodu(currentDept);
					std::cout << "{petent, " << getpid() << "} redirected to dept=" << wydzial
					          << " by officer=" << msg.senderId << std::endl;
				}
				std::cout << "{petent, " << getpid() << "} ticket_request dept=" << wydzialZKodu(currentDept) << std::endl;
				zglosKolejkeBiletowa(mqidOther, getpid());
				wyslijDoBiletomatu(mqidOther, getpid(), 0, BiletomatMessagesEnum::PetentCzekaNaBilet, currentDept);
				odbierzBilet(mqidPetent, getpid());
				std::cout << "{petent, " << getpid() << "} ticket_received dept=" << wydzialZKodu(currentDept) << std::endl;
				break;
			case PetentMessagesEnum::Obsluzony:
				logujZAdnotacja(mqidOther, getpid(), "petent served", maDziecko, &childState);
				std::cout << "{petent, " << getpid() << "} served by officer=" << msg.senderId << std::endl;
				zakoncz = true;
				break;
			case PetentMessagesEnum::Odprawiony:
				logujZAdnotacja(mqidOther, getpid(), "petent rejected", maDziecko, &childState);
				std::cout << "{petent, " << getpid() << "} rejected by officer=" << msg.senderId << std::endl;
				zakoncz = true;
				break;
			default:
				break;
		}
	}

	if (wymuszoneWyjscie) {
		
		logujZAdnotacja(mqidOther, getpid(), "petent forced exit", maDziecko, &childState);
		std::cout << "{petent, " << getpid() << "} forced exit" << std::endl;
		std::cout << "{petent, " << getpid() << "} sfrustrowany" << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(120));
	}

	Message exitMsg{};
	exitMsg.mtype = loaderPid + 1;
	exitMsg.senderId = getpid();
	exitMsg.receiverId = loaderPid;
	exitMsg.group = MessageGroup::Loader;
	exitMsg.messageType.loaderType = LoaderMessagesEnum::PetentOpuszczaBudynek;
	exitMsg.data1 = 0;
	exitMsg.data2 = enteredBuilding ? zajeteMiejsca : 0;

	if (msgsnd(mqidPetent, &exitMsg, sizeof(exitMsg) - sizeof(long), 0) == -1) {
		perror("msgsnd failed");
	}

	logujZAdnotacja(mqidOther, getpid(), "petent exited building", maDziecko, &childState);
	std::cout << "{petent, " << getpid() << "} exited" << std::endl;

	if (maDziecko) {
		{
			std::lock_guard<std::mutex> lock(childState.mutex);
			childState.done = true;
		}
		childState.cv.notify_one();
		childThread.join();
	}

	shmdt(stan);
	return 0;
}
