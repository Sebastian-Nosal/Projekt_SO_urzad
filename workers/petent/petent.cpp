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
#include "utils/mq_semaphore.h"
#include "utils/sem_log.h"
#include "petent.h"

namespace {

int mqidOther;
volatile sig_atomic_t wymuszoneWyjscie = 0;
sem_t* g_otherQueueSem = nullptr;
sem_t* g_petentQueueSem = nullptr;
int g_petentReplyQueueId = -1;
int g_petentMainQueueId = -1;
bool g_isVip = false;

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

void monitorujSemaforKolejkiPetenta(const std::string& where) {
	if (g_petentMainQueueId < 0) {
		//wyslijMonitoring(mqidOther, getpid(), "kolejka petenta niedostepna gdzie=" + where);
		return;
	}
	msqid_ds ds{};
	if (msgctl(g_petentMainQueueId, IPC_STAT, &ds) == -1) {
		//wyslijMonitoring(mqidOther, getpid(), "kolejka petenta blad odczytu gdzie=" + where);
		return;
	}
	int wolne = static_cast<int>(ENTRY_QUEUE_LIMIT) - static_cast<int>(ds.msg_qnum);
	if (wolne < 0) wolne = 0;
	const char* stan = (wolne <= 0) ? "ZAMKNIETA" : "OTWARTA";
	//wyslijMonitoring(mqidOther, getpid(), "kolejka petenta gdzie=" + where + " stan=" + stan + " wolne=" + std::to_string(wolne));
}

void monitorujKolejkeWyjscia(int mqid, const std::string& where) {
	if (mqid < 0) {
		//wyslijMonitoring(mqidOther, getpid(), "kolejka wyjscia niedostepna gdzie=" + where);
		return;
	}
	msqid_ds ds{};
	if (msgctl(mqid, IPC_STAT, &ds) == -1) {
		//wyslijMonitoring(mqidOther, getpid(), "kolejka wyjscia blad odczytu gdzie=" + where);
		return;
	}
	int wolne = static_cast<int>(EXIT_QUEUE_LIMIT) - static_cast<int>(ds.msg_qnum);
	if (wolne < 0) wolne = 0;
	const char* stan = (wolne <= 0) ? "ZAMKNIETA" : "OTWARTA";
	//wyslijMonitoring(mqidOther, getpid(), "kolejka wyjscia gdzie=" + where + " stan=" + stan + " wolne=" + std::to_string(wolne));
}

bool czekajNaSlotWyjscia(int mqid) {
	if (mqid < 0) {
		return false;
	}
	while (true) {
		msqid_ds ds{};
		if (msgctl(mqid, IPC_STAT, &ds) == -1) {
			if (errno == EINTR) {
				continue;
			}
			return false;
		}
		if (ds.msg_qnum < EXIT_QUEUE_LIMIT) {
			return true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

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
	msg.replyQueueId = g_petentReplyQueueId;
	msg.flags = g_isVip ? MESSAGE_FLAG_VIP : 0;
	msg.group = MessageGroup::Biletomat;
	msg.messageType.biletomatType = type;
	msg.data1 = data1;
	msg.data2 = data2;
	if (!otherQueueWaitToSend(g_otherQueueSem)) {
		return;
	}
	if (msgsnd(mqidOther, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
		perror("msgsnd failed");
		otherQueueReleaseSlot(g_otherQueueSem);
	}
}

void zglosKolejkeBiletowa(int mqidOther, int senderId) {
	Message notify{};
	notify.mtype = static_cast<long>(ProcessMqType::Loader);
	notify.senderId = senderId;
	notify.receiverId = 0;
	notify.replyQueueId = g_petentReplyQueueId;
	notify.flags = 0;
	notify.group = MessageGroup::Biletomat;
	notify.messageType.biletomatType = BiletomatMessagesEnum::PetentCzekaNaBilet;
	if (!otherQueueWaitToSend(g_otherQueueSem)) {
		return;
	}
	if (msgsnd(mqidOther, &notify, sizeof(notify) - sizeof(long), 0) == -1) {
		perror("msgsnd failed");
		otherQueueReleaseSlot(g_otherQueueSem);
	}
}

bool recvBlocking(int mqid, Message& msg, long type) {
	if (wymuszoneWyjscie) {
		return false;
	}
	while (true) {
		if (msgrcv(mqid, &msg, sizeof(msg) - sizeof(long), type, 0) != -1) {
			return true;
		}
		if (errno == EINTR) {
			if (wymuszoneWyjscie) {
				return false;
			}
			continue;
		}
		perror("msgrcv failed");
		return false;
	}
}

bool odbierzWezwanieUrzednika(int mqidPetent, int selfPid) {
	Message msg{};
	if (!recvBlocking(mqidPetent, msg, 0)) {
		return false;
	}
	return msg.group == MessageGroup::Petent && msg.messageType.petentType == PetentMessagesEnum::WezwanoDoUrzednika;
}

void wyslijDoKasy(int mqidOther, int senderId, int receiverId, int data1 = 0, int data2 = 0) {
	Message msg{};
	msg.mtype = static_cast<long>(ProcessMqType::Kasa);
	msg.senderId = senderId;
	msg.receiverId = receiverId;
	msg.replyQueueId = g_petentReplyQueueId;
	msg.flags = 0;
	msg.group = MessageGroup::Petent;
	msg.messageType.petentType = PetentMessagesEnum::IdzDoKasy;
	msg.data1 = data1;
	msg.data2 = data2;
	if (!otherQueueWaitToSend(g_otherQueueSem)) {
		return;
	}
	if (msgsnd(mqidOther, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
		perror("msgsnd failed");
		otherQueueReleaseSlot(g_otherQueueSem);
	}
}

bool odbierzObsluzenie(int mqidPetent, int selfPid) {
	Message msg{};
	if (!recvBlocking(mqidPetent, msg, 0)) {
		return false;
	}
	return msg.group == MessageGroup::Petent && msg.messageType.petentType == PetentMessagesEnum::Obsluzony;
}

bool odbierzOdprawe(int mqidPetent, int selfPid) {
	Message msg{};
	if (!recvBlocking(mqidPetent, msg, 0)) {
		return false;
	}
	return msg.group == MessageGroup::Petent && msg.messageType.petentType == PetentMessagesEnum::Odprawiony;
}

bool odbierzBilet(int mqidPetent, int selfPid, Message& msg) {
	if (!recvBlocking(mqidPetent, msg, 0)) {
		return false;
	}
	return msg.group == MessageGroup::Petent;
}

bool odbierzKomunikatPetenta(int mqidPetent, int selfPid, Message& msg) {
	return recvBlocking(mqidPetent, msg, 0);
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

void obsluzSigusr2(int) {
	if (wymuszoneWyjscie) {
		return;
	}
	wymuszoneWyjscie = 1;
		// std::cout << "{petent, " << getpid() << "} Otrzymano sygnał wymuszonego wyjścia (SIGUSR2)" << std::endl;
		// std::cout << "{petent, " << getpid() << "} Opuszczam budynek natychmiast." << std::endl;
		 std::cout << "\033[31m[petent, " << getpid() << "]\033[0m sfrustrowany" << std::endl;
		logujZAdnotacja(mqidOther, getpid(), "petent wymuszone wyjście", false, NULL);
		sleep(WAIT_FRUSTRATED);
		logujZAdnotacja(mqidOther, getpid(), "Petent odszedł sfrustrowany!", false, NULL);
}


int main() {
	std::signal(SIGUSR2, obsluzSigusr2);
	std::signal(SIGINT, SIG_IGN);

	int mqidPetent = initMessageQueue(MQ_KEY_ENTRY);
	mqidOther = initMessageQueue(MQ_KEY_OTHER);
	g_petentMainQueueId = mqidPetent;
	int mqidPetentExit = initMessageQueue(MQ_KEY_EXIT);
	int mqidSelf = msgget(IPC_PRIVATE, 0666);
	if (mqidSelf == -1) {
		perror("msgget failed");
		return 1;
	}
	struct MqCleanup {
		int id;
		~MqCleanup() {
			if (id >= 0) {
				msgctl(id, IPC_RMID, nullptr);
			}
		}
	} mqCleanup{mqidSelf};
	g_petentReplyQueueId = mqidSelf;
	setPetentQueueId(mqidPetent);
	setOtherQueueId(mqidOther);
	g_otherQueueSem = openOtherQueueSemaphore(false);
	g_petentQueueSem = openPetentQueueSemaphore(false);

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

	semWaitLogged(semaphore, SEMAPHORE_NAME, __func__);
	int loaderPid = stan->loaderPid;
	semPostLogged(semaphore, SEMAPHORE_NAME, __func__);

	if (loaderPid <= 0) {
		std::cerr << "Loader PID not set." << std::endl;
		shmdt(stan);
		return 1;
	}

	bool maDziecko = (losujIlosc(1, 100) <= CHILD_CHANCE_PERCENT);
	bool isVip = (losujIlosc(1, 100) <= VIP_CHANCE_PERCENT);
	g_isVip = isVip;
	int zajeteMiejsca = maDziecko ? 2 : 1;
	ChildLogQueue childState;
	std::thread childThread;
	if (maDziecko) {
		childThread = std::thread(dzieckoPracuj, &childState, mqidOther, getpid());
	}

	logujZAdnotacja(mqidOther, getpid(), isVip ? "petent utworzony VIP" : "petent utworzony", maDziecko, &childState);
	// std::cout << "{petent, " << getpid() << "} created child=" << (maDziecko ? 1 : 0)
	//           << " places=" << zajeteMiejsca << std::endl;

	Message request{};
	request.mtype = loaderPid;
	request.senderId = getpid();
	request.receiverId = loaderPid;
	request.replyQueueId = g_petentReplyQueueId;
	request.flags = isVip ? MESSAGE_FLAG_VIP : 0;
	request.group = MessageGroup::Loader;
	request.messageType.loaderType = LoaderMessagesEnum::NowyPetent;
	request.data2 = zajeteMiejsca;
	// std::cout << "{petent, " << getpid() << "} request_entry loader=" << loaderPid
	//           << " places=" << zajeteMiejsca << std::endl;

	monitorujSemaforKolejkiPetenta("przed_rezerwacja_slotu_wejscie");
	if (!petentQueueWaitToSend(g_petentQueueSem)) {
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
	monitorujSemaforKolejkiPetenta("po_rezerwacji_slotu_wejscie");
	if (msgsnd(mqidPetent, &request, sizeof(request) - sizeof(long), 0) == -1) {
		perror("msgsnd failed");
		petentQueueReleaseSlot(g_petentQueueSem);
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

	auto zakonczPetenta = [&](bool enteredBuilding, int zajeteMiejsca, bool maDziecko, ChildLogQueue* childState, std::thread& childThread) {
		Message exitMsg{};
		exitMsg.mtype = loaderPid + 1;
		exitMsg.senderId = getpid();
		exitMsg.receiverId = loaderPid;
		exitMsg.replyQueueId = g_petentReplyQueueId;
		exitMsg.group = MessageGroup::Loader;
		exitMsg.messageType.loaderType = LoaderMessagesEnum::PetentOpuszczaBudynek;
		exitMsg.data1 = 0;
		exitMsg.data2 = enteredBuilding ? zajeteMiejsca : 0;

		monitorujKolejkeWyjscia(mqidPetentExit, "przed_rezerwacja_slotu_wyjscie");
		if (!czekajNaSlotWyjscia(mqidPetentExit)) {
			return;
		}
		monitorujKolejkeWyjscia(mqidPetentExit, "po_rezerwacji_slotu_wyjscie");
		if (msgsnd(mqidPetentExit, &exitMsg, sizeof(exitMsg) - sizeof(long), 0) == -1) {
			perror("msgsnd failed");
		}

		logujZAdnotacja(mqidOther, getpid(), "petent opuścił budynek", maDziecko, childState);

		if (maDziecko) {
			{
				std::lock_guard<std::mutex> lock(childState->mutex);
				childState->done = true;
			}
			childState->cv.notify_one();
			childThread.join();
		}
	};

	Message response{};
	bool enteredBuilding = false;
	while (true) {
		if (!recvBlocking(mqidSelf, response, 0)) {
			if (wymuszoneWyjscie) {
				zakonczPetenta(enteredBuilding, zajeteMiejsca, maDziecko, &childState, childThread);
				shmdt(stan);
				return 0;
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
			return 1;
		}
		// std::cout << "{petent, " << getpid() << "} recv from pid=" << response.senderId << std::endl;

		if (wymuszoneWyjscie) {
			zakonczPetenta(enteredBuilding, zajeteMiejsca, maDziecko, &childState, childThread);
			shmdt(stan);
			return 0;
		}

		if (response.group != MessageGroup::Petent) {
			continue;
		}

		if (response.messageType.petentType == PetentMessagesEnum::WejdzDoBudynku) {
			logujZAdnotacja(mqidOther, getpid(), "petent wszedł do budynku", maDziecko, &childState);
			// std::cout << "{petent, " << getpid() << "} entered building by loader=" << response.senderId << std::endl;
			enteredBuilding = true;
			break;
		}

		if (response.messageType.petentType == PetentMessagesEnum::Odprawiony) {
			logujZAdnotacja(mqidOther, getpid(), "petent nie wpuszczony", maDziecko, &childState);
			// std::cout << "{petent, " << getpid() << "} denied entry by loader=" << response.senderId << std::endl;

			Message exitMsg{};
			exitMsg.mtype = loaderPid + 1;
			exitMsg.senderId = getpid();
			exitMsg.receiverId = loaderPid;
			exitMsg.replyQueueId = g_petentReplyQueueId;
			exitMsg.group = MessageGroup::Loader;
			exitMsg.messageType.loaderType = LoaderMessagesEnum::PetentOpuszczaBudynek;
			exitMsg.data1 = 0;
			exitMsg.data2 = 0;

			monitorujKolejkeWyjscia(mqidPetentExit, "przed_rezerwacja_slotu_odmowa");
			if (!czekajNaSlotWyjscia(mqidPetentExit)) {
				break;
			}
			monitorujKolejkeWyjscia(mqidPetentExit, "po_rezerwacji_slotu_odmowa");
			if (msgsnd(mqidPetentExit, &exitMsg, sizeof(exitMsg) - sizeof(long), 0) == -1) {
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
	// std::cout << "{petent, " << getpid() << "} ticket_request dept=" << wydzialZKodu(currentDept) << std::endl;
	zglosKolejkeBiletowa(mqidOther, getpid());
	wyslijDoBiletomatu(mqidOther, getpid(), 0, BiletomatMessagesEnum::PetentCzekaNaBilet, currentDept);
	Message ticketMsg{};
	if (!odbierzBilet(mqidSelf, getpid(), ticketMsg)) {
		if (wymuszoneWyjscie) {
			zakonczPetenta(enteredBuilding, zajeteMiejsca, maDziecko, &childState, childThread);
			shmdt(stan);
			return 0;
		}
	} else if (ticketMsg.messageType.petentType == PetentMessagesEnum::Odprawiony) {
		logujZAdnotacja(mqidOther, getpid(), "petent odprawiony przez biletomat", maDziecko, &childState);
		zakonczPetenta(enteredBuilding, zajeteMiejsca, maDziecko, &childState, childThread);
		shmdt(stan);
		return 0;
	}
	// std::cout << "{petent, " << getpid() << "} ticket_received dept=" << wydzialZKodu(currentDept) << std::endl;

	// wait for officer and handle flow
	bool zakoncz = false;
	while (!zakoncz) {
		Message msg{};
		if (!odbierzKomunikatPetenta(mqidSelf, getpid(), msg)) {
			if (wymuszoneWyjscie) {
				zakonczPetenta(enteredBuilding, zajeteMiejsca, maDziecko, &childState, childThread);
				shmdt(stan);
				return 0;
			}
			continue;
		}

		if (wymuszoneWyjscie) {
			zakonczPetenta(enteredBuilding, zajeteMiejsca, maDziecko, &childState, childThread);
			shmdt(stan);
			return 0;
		}

		if (msg.group != MessageGroup::Petent) {
			continue;
		}

		switch (msg.messageType.petentType) {
			case PetentMessagesEnum::WezwanoDoUrzednika:
				logujZAdnotacja(mqidOther, getpid(), "petent wezwany przez urzędnika", maDziecko, &childState);
				// std::cout << "{petent, " << getpid() << "} called by officer=" << msg.senderId << std::endl;
				break;
			case PetentMessagesEnum::IdzDoKasy:
				logujZAdnotacja(mqidOther, getpid(), "petent skierowany do kasy", maDziecko, &childState);
				// std::cout << "{petent, " << getpid() << "} sent to cashier by officer=" << msg.senderId << std::endl;
				wyslijDoKasy(mqidOther, getpid(), 0, msg.data1, msg.data2);
				break;
			case PetentMessagesEnum::IdzDoInnegoUrzednika:
			{
				logujZAdnotacja(mqidOther, getpid(), "petent przekierowany do innego urzędu", maDziecko, &childState);
				currentDept = msg.data1;
				{
					std::string wydzial = wydzialZKodu(currentDept);
					// std::cout << "{petent, " << getpid() << "} redirected to dept=" << wydzial
					//           << " by officer=" << msg.senderId << std::endl;
				}
				// std::cout << "{petent, " << getpid() << "} ticket_request dept=" << wydzialZKodu(currentDept) << std::endl;
				zglosKolejkeBiletowa(mqidOther, getpid());
				wyslijDoBiletomatu(mqidOther, getpid(), 0, BiletomatMessagesEnum::PetentCzekaNaBilet, currentDept);
				Message redirectTicketMsg{};
				if (odbierzBilet(mqidSelf, getpid(), redirectTicketMsg)) {
					if (redirectTicketMsg.messageType.petentType == PetentMessagesEnum::Odprawiony) {
						logujZAdnotacja(mqidOther, getpid(), "petent odprawiony przez biletomat", maDziecko, &childState);
						zakonczPetenta(enteredBuilding, zajeteMiejsca, maDziecko, &childState, childThread);
						shmdt(stan);
						return 0;
					}
				}
				// std::cout << "{petent, " << getpid() << "} ticket_received dept=" << wydzialZKodu(currentDept) << std::endl;
				break;
			}
			case PetentMessagesEnum::Obsluzony:
				logujZAdnotacja(mqidOther, getpid(), "petent obsłużony", maDziecko, &childState);
				// std::cout << "{petent, " << getpid() << "} served by officer=" << msg.senderId << std::endl;
				zakoncz = true;
				break;
			case PetentMessagesEnum::Odprawiony:
				logujZAdnotacja(mqidOther, getpid(), "petent odprawiony", maDziecko, &childState);
				// std::cout << "{petent, " << getpid() << "} rejected by officer=" << msg.senderId << std::endl;
				zakoncz = true;
				break;
			default:
				break;
		}
	}

	zakonczPetenta(enteredBuilding, zajeteMiejsca, maDziecko, &childState, childThread);

	shmdt(stan);
	return 0;
}
