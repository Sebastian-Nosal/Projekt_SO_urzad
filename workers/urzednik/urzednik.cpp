#include <iostream>
#include <string>
#include <csignal>
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
#include "utils/losowosc.h"

namespace {
volatile sig_atomic_t konczPoBiezacym = 0;

void obsluzSigusr1(int) {
	konczPoBiezacym = 1;
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

int maxPrzyjecDlaWydzialu(const std::string& wydzial) {
	if (wydzial == "SA") return MAX_SA_APPOINTMENTS;
	if (wydzial == "SC") return MAX_SC_APPOINTMENTS;
	if (wydzial == "KM") return MAX_KM_APPOINTMENTS;
	if (wydzial == "ML") return MAX_ML_APPOINTMENTS;
	if (wydzial == "PD") return MAX_PD_APPOINTMENTS;
	return 0;
}

int losujPrzekierowanie() {
	int los = losujIlosc(1, 4);
	return los;
}

void wyslijDoPetenta(int mqidPetent, int pid, PetentMessagesEnum typ, int data1 = 0, int data2 = 0) {
	Message msg{};
	msg.mtype = pid;
	msg.senderId = getpid();
	msg.receiverId = pid;
	msg.group = MessageGroup::Petent;
	msg.messageType.petentType = typ;
	msg.data1 = data1;
	msg.data2 = data2;
	if (msgsnd(mqidPetent, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
		perror("msgsnd failed");
	}
}

void wyslijDoBiletomatu(int mqidOther) {
	Message msg{};
	msg.mtype = static_cast<long>(ProcessMqType::Biletomat);
	msg.senderId = getpid();
	msg.receiverId = 0;
	msg.group = MessageGroup::Biletomat;
	msg.messageType.biletomatType = BiletomatMessagesEnum::UrzednikWyczerpanyOdejdz;
	if (msgsnd(mqidOther, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
		perror("msgsnd failed");
	}
}

DepartmentMqType mtypeDlaWydzialu(const std::string& wydzial) {
	if (wydzial == "SA") return DepartmentMqType::SA;
	if (wydzial == "SC") return DepartmentMqType::SC;
	if (wydzial == "KM") return DepartmentMqType::KM;
	if (wydzial == "ML") return DepartmentMqType::ML;
	return DepartmentMqType::PD;
}
}

int main(int argc, char** argv) {
	if (argc < 2) {
		std::cerr << "Brak argumentu wydzialu" << std::endl;
		return 1;
	}

	std::string wydzial = argv[1];
	int limit = maxPrzyjecDlaWydzialu(wydzial);
	if (limit <= 0) {
		std::cerr << "Nieznany wydzial" << std::endl;
		return 1;
	}

	std::signal(SIGUSR1, obsluzSigusr1);

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

	int obsluzeni = 0;
	bool wyczerpany = false;

	while (true) {
		Message msg{};
		if (msgrcv(mqidOther, &msg, sizeof(msg) - sizeof(long), static_cast<long>(mtypeDlaWydzialu(wydzial)), 0) == -1) {
			continue;
		}

		if (msg.group != MessageGroup::Petent && msg.group != MessageGroup::Biletomat) {
			continue;
		}

		if (msg.group == MessageGroup::Biletomat &&
			msg.messageType.biletomatType != BiletomatMessagesEnum::WydanoBiletCzekaj) {
			continue;
		}

		int pidPetenta = msg.senderId;
		std::cout << "{urzednik, " << getpid() << "} petent=" << pidPetenta << std::endl;
		wyslijDoPetenta(mqidPetent, pidPetenta, PetentMessagesEnum::WezwanoDoUrzednika);
		std::cout << "{urzednik, " << getpid() << "} send to pid=" << pidPetenta << std::endl;

		if (wyczerpany) {
			wyslijDoPetenta(mqidPetent, pidPetenta, PetentMessagesEnum::Odprawiony);
			std::cout << "{urzednik, " << getpid() << "} rejected" << std::endl;
			continue;
		}

		if (wydzial == "SA") {
			int los = losujIlosc(1, 100);
			if (los <= 40) {
				int przekierowanie = losujPrzekierowanie();
				wyslijDoPetenta(mqidPetent, pidPetenta, PetentMessagesEnum::IdzDoInnegoUrzednika, przekierowanie);
				std::cout << "{urzednik, " << getpid() << "} redirected" << std::endl;
			} else {
				wyslijDoPetenta(mqidPetent, pidPetenta, PetentMessagesEnum::Obsluzony);
				std::cout << "{urzednik, " << getpid() << "} served" << std::endl;
			}
		} else {
			int los = losujIlosc(1, 100);
			if (los <= 10) {
				wyslijDoPetenta(mqidPetent, pidPetenta, PetentMessagesEnum::IdzDoKasy);
				std::cout << "{urzednik, " << getpid() << "} to cashier" << std::endl;
			} else {
				wyslijDoPetenta(mqidPetent, pidPetenta, PetentMessagesEnum::Obsluzony);
				std::cout << "{urzednik, " << getpid() << "} served" << std::endl;
			}
		}

		obsluzeni++;
		std::cout << "{urzednik, " << getpid() << "} obsluzeni=" << obsluzeni
		          << " pozostalo=" << (limit - obsluzeni) << std::endl;

		if (obsluzeni >= limit) {
			wyczerpany = true;
			std::cout << "{urzednik, " << getpid() << "} exhausted" << std::endl;
			sem_wait(semaphore);
			if (stan->activeOfficers > 0) {
				stan->activeOfficers -= 1;
			}
			sem_post(semaphore);
			wyslijDoBiletomatu(mqidOther);
		}

		if (konczPoBiezacym) {
			break;
		}
	}

	shmdt(stan);
	sem_close(semaphore);
	return 0;
}
