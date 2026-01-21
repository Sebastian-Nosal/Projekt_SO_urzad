#include <iostream>
#include <chrono>
#include <thread>
#include <csignal>
#include <cstring>
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

void wyslijMonitoring(int mqidOther, int senderId, const char* text) {
	Message msg{};
	msg.mtype = static_cast<long>(ProcessMqType::Monitoring);
	msg.senderId = senderId;
	msg.receiverId = 0;
	msg.group = MessageGroup::Monitoring;
	msg.messageType.monitoringType = MonitoringMessagesEnum::Log;
	std::snprintf(msg.data3, sizeof(msg.data3), "%s", text);
	if (msgsnd(mqidOther, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
		perror("msgsnd failed");
	}
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

bool odbierzWezwanieUrzednika(int mqidPetent, int selfPid) {
	Message msg{};
	if (msgrcv(mqidPetent, &msg, sizeof(msg) - sizeof(long), selfPid, 0) == -1) {
		perror("msgrcv failed");
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
	if (msgrcv(mqidPetent, &msg, sizeof(msg) - sizeof(long), selfPid, 0) == -1) {
		perror("msgrcv failed");
		return false;
	}
	return msg.group == MessageGroup::Petent && msg.messageType.petentType == PetentMessagesEnum::Obsluzony;
}

bool odbierzOdprawe(int mqidPetent, int selfPid) {
	Message msg{};
	if (msgrcv(mqidPetent, &msg, sizeof(msg) - sizeof(long), selfPid, 0) == -1) {
		perror("msgrcv failed");
		return false;
	}
	return msg.group == MessageGroup::Petent && msg.messageType.petentType == PetentMessagesEnum::Odprawiony;
}

bool odbierzBilet(int mqidPetent, int selfPid) {
	Message msg{};
	if (msgrcv(mqidPetent, &msg, sizeof(msg) - sizeof(long), selfPid, 0) == -1) {
		perror("msgrcv failed");
		return false;
	}
	return msg.group == MessageGroup::Petent && msg.messageType.petentType == PetentMessagesEnum::OtrzymanoBilet;
}

bool odbierzKomunikatPetenta(int mqidPetent, int selfPid, Message& msg) {
	if (msgrcv(mqidPetent, &msg, sizeof(msg) - sizeof(long), selfPid, 0) == -1) {
		perror("msgrcv failed");
		return false;
	}
	return true;
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

	wyslijMonitoring(mqidOther, getpid(), "petent created");
	std::cout << "{petent, " << getpid() << "} created" << std::endl;

	Message request{};
	request.mtype = loaderPid;
	request.senderId = getpid();
	request.receiverId = loaderPid;
	request.group = MessageGroup::Loader;
	request.messageType.loaderType = LoaderMessagesEnum::NowyPetent;
	std::cout << "{petent, " << getpid() << "} send to loader pid=" << loaderPid << std::endl;

	if (msgsnd(mqidPetent, &request, sizeof(request) - sizeof(long), 0) == -1) {
		perror("msgsnd failed");
		shmdt(stan);
		return 1;
	}

	Message response{};
	if (msgrcv(mqidPetent, &response, sizeof(response) - sizeof(long), getpid(), 0) == -1) {
		perror("msgrcv failed");
		shmdt(stan);
		return 1;
	}
	std::cout << "{petent, " << getpid() << "} recv from pid=" << response.senderId << std::endl;

	if (response.group == MessageGroup::Petent && response.messageType.petentType == PetentMessagesEnum::WejdzDoBudynku) {
		wyslijMonitoring(mqidOther, getpid(), "petent entered building");
		std::cout << "{petent, " << getpid() << "} entered building" << std::endl;
	} else {
		wyslijMonitoring(mqidOther, getpid(), "petent denied entry");
		std::cout << "{petent, " << getpid() << "} denied entry" << std::endl;
		shmdt(stan);
		return 0;
	}

	// request ticket for SA (1)
	wyslijDoBiletomatu(mqidOther, getpid(), 0, BiletomatMessagesEnum::PetentCzekaNaBilet, 1);
	odbierzBilet(mqidPetent, getpid());
	std::cout << "{petent, " << getpid() << "} ticket received" << std::endl;

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
				wyslijMonitoring(mqidOther, getpid(), "petent called by officer");
				std::cout << "{petent, " << getpid() << "} called by officer" << std::endl;
				break;
			case PetentMessagesEnum::IdzDoKasy:
				wyslijMonitoring(mqidOther, getpid(), "petent sent to cashier");
				std::cout << "{petent, " << getpid() << "} sent to cashier" << std::endl;
				wyslijDoKasy(mqidOther, getpid(), 0, msg.data1, msg.data2);
				break;
			case PetentMessagesEnum::IdzDoInnegoUrzednika:
				wyslijMonitoring(mqidOther, getpid(), "petent redirected to another office");
				std::cout << "{petent, " << getpid() << "} redirected" << std::endl;
				wyslijDoBiletomatu(mqidOther, getpid(), 0, BiletomatMessagesEnum::PetentCzekaNaBilet, msg.data1);
				odbierzBilet(mqidPetent, getpid());
				break;
			case PetentMessagesEnum::Obsluzony:
				wyslijMonitoring(mqidOther, getpid(), "petent served");
				std::cout << "{petent, " << getpid() << "} served" << std::endl;
				zakoncz = true;
				break;
			case PetentMessagesEnum::Odprawiony:
				wyslijMonitoring(mqidOther, getpid(), "petent rejected");
				std::cout << "{petent, " << getpid() << "} rejected" << std::endl;
				zakoncz = true;
				break;
			default:
				break;
		}
	}

	if (wymuszoneWyjscie) {
		wyslijMonitoring(mqidOther, getpid(), "petent forced exit");
		std::cout << "{petent, " << getpid() << "} forced exit" << std::endl;
		std::this_thread::sleep_for(std::chrono::seconds(120));
	}

	Message exitMsg{};
	exitMsg.mtype = loaderPid;
	exitMsg.senderId = getpid();
	exitMsg.receiverId = loaderPid;
	exitMsg.group = MessageGroup::Loader;
	exitMsg.messageType.loaderType = LoaderMessagesEnum::PetentOpuszczaBudynek;
	exitMsg.data1 = 0;

	if (msgsnd(mqidPetent, &exitMsg, sizeof(exitMsg) - sizeof(long), 0) == -1) {
		perror("msgsnd failed");
	}

	wyslijMonitoring(mqidOther, getpid(), "petent exited building");
	std::cout << "{petent, " << getpid() << "} exited" << std::endl;

	shmdt(stan);
	return 0;
}
