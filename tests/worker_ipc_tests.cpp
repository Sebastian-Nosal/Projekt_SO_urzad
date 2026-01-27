#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <csignal>
#include <cerrno>
#include <cstring>

#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config/config.h"
#include "config/messages.h"
#include "config/shm.h"

namespace {

int initQueue(key_t key) {
	int mqid = msgget(key, IPC_CREAT | 0666);
	if (mqid == -1) {
		perror("msgget failed");
	}
	return mqid;
}

int initShm() {
	int shmid = shmget(SHM_KEY, sizeof(SharedState), IPC_CREAT | 0666);
	if (shmid == -1) {
		perror("shmget failed");
	}
	return shmid;
}

sem_t* initSemaphore() {
	sem_t* semaphore = sem_open(SEMAPHORE_NAME, O_CREAT, 0666, 1);
	if (semaphore == SEM_FAILED) {
		perror("sem_open failed");
		return nullptr;
	}
	return semaphore;
}

void cleanupIpc(int mqidPetent, int mqidOther, int shmid, sem_t* sem) {
	if (mqidPetent != -1) {
		msgctl(mqidPetent, IPC_RMID, nullptr);
	}
	if (mqidOther != -1) {
		msgctl(mqidOther, IPC_RMID, nullptr);
	}
	if (shmid != -1) {
		shmctl(shmid, IPC_RMID, nullptr);
	}
	if (sem) {
		sem_close(sem);
		sem_unlink(SEMAPHORE_NAME);
	}
}

bool waitForMsg(int mqid, long type, Message& out, int timeoutMs) {
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	while (std::chrono::steady_clock::now() < deadline) {
		if (msgrcv(mqid, &out, sizeof(out) - sizeof(long), type, IPC_NOWAIT) != -1) {
			return true;
		}
		if (errno != ENOMSG && errno != EINTR) {
			perror("msgrcv failed");
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	return false;
}

bool testMonitoring() {
	std::cout << "[TEST] monitoring\n";
	std::remove(LOG_FILE.c_str());

	int mqidOther = initQueue(MQ_KEY_OTHER);
	if (mqidOther == -1) return false;

	pid_t pid = fork();
	if (pid == 0) {
		execl("./monitoring", "monitoring", nullptr);
		_exit(1);
	}
	if (pid < 0) return false;

	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	Message msg{};
	msg.mtype = static_cast<long>(ProcessMqType::Monitoring);
	msg.senderId = getpid();
	msg.receiverId = 0;
	msg.group = MessageGroup::Monitoring;
	msg.messageType.monitoringType = MonitoringMessagesEnum::Log;
	std::snprintf(msg.data3, sizeof(msg.data3), "%s", "test monitoring");
	if (msgsnd(mqidOther, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
		perror("msgsnd failed");
		kill(pid, SIGTERM);
		waitpid(pid, nullptr, 0);
		msgctl(mqidOther, IPC_RMID, nullptr);
		return false;
	}

	bool ok = false;
	for (int i = 0; i < 50; ++i) {
		std::ifstream f(LOG_FILE);
		if (f.good()) {
			std::string line;
			while (std::getline(f, line)) {
				if (line.find("test monitoring") != std::string::npos) {
					ok = true;
					break;
				}
			}
		}
		if (ok) break;
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	kill(pid, SIGTERM);
	waitpid(pid, nullptr, 0);
	msgctl(mqidOther, IPC_RMID, nullptr);
	return ok;
}

bool testBiletomat() {
	std::cout << "[TEST] biletomat\n";
	int mqidPetent = msgget(IPC_PRIVATE, 0666);
	int mqidOther = initQueue(MQ_KEY_OTHER);
	int shmid = initShm();
	sem_t* sem = initSemaphore();
	if (mqidPetent == -1 || mqidOther == -1 || shmid == -1 || sem == nullptr) {
		cleanupIpc(mqidPetent, mqidOther, shmid, sem);
		return false;
	}

	SharedState* state = static_cast<SharedState*>(shmat(shmid, nullptr, 0));
	if (state == reinterpret_cast<SharedState*>(-1)) {
		cleanupIpc(mqidPetent, mqidOther, shmid, sem);
		return false;
	}
	sem_wait(sem);
	std::memset(state, 0, sizeof(SharedState));
	state->officeOpen = 1;
	state->activeTicketMachines = 1;
	sem_post(sem);

	pid_t pid = fork();
	if (pid == 0) {
		execl("./workers/biletomat/biletomat", "biletomat", "0", nullptr);
		_exit(1);
	}
	if (pid < 0) {
		shmdt(state);
		cleanupIpc(mqidPetent, mqidOther, shmid, sem);
		return false;
	}

	Message req{};
	req.mtype = static_cast<long>(ProcessMqType::Biletomat);
	req.senderId = getpid();
	req.receiverId = 0;
	req.replyQueueId = mqidPetent;
	req.group = MessageGroup::Biletomat;
	req.messageType.biletomatType = BiletomatMessagesEnum::PetentCzekaNaBilet;
	req.data1 = 1;
	if (msgsnd(mqidOther, &req, sizeof(req) - sizeof(long), 0) == -1) {
		perror("msgsnd failed");
		kill(pid, SIGINT);
		waitpid(pid, nullptr, 0);
		shmdt(state);
		cleanupIpc(mqidPetent, mqidOther, shmid, sem);
		return false;
	}

	Message resp{};
	bool ok = waitForMsg(mqidPetent, 0, resp, 2000);
	bool valid = ok && resp.group == MessageGroup::Petent && resp.messageType.petentType == PetentMessagesEnum::OtrzymanoBilet;

	kill(pid, SIGINT);
	waitpid(pid, nullptr, 0);
	shmdt(state);
	cleanupIpc(mqidPetent, mqidOther, shmid, sem);
	return valid;
}

bool testPetent() {
	std::cout << "[TEST] petent\n";
	int mqidPetent = initQueue(MQ_KEY_ENTRY);
	int mqidOther = initQueue(MQ_KEY_OTHER);
	int mqidExit = initQueue(MQ_KEY_EXIT);
	int shmid = initShm();
	sem_t* sem = initSemaphore();
	if (mqidPetent == -1 || mqidOther == -1 || mqidExit == -1 || shmid == -1 || sem == nullptr) {
		cleanupIpc(mqidPetent, mqidOther, shmid, sem);
		if (mqidExit != -1) msgctl(mqidExit, IPC_RMID, nullptr);
		return false;
	}

	SharedState* state = static_cast<SharedState*>(shmat(shmid, nullptr, 0));
	if (state == reinterpret_cast<SharedState*>(-1)) {
		cleanupIpc(mqidPetent, mqidOther, shmid, sem);
		return false;
	}
	sem_wait(sem);
	std::memset(state, 0, sizeof(SharedState));
	state->loaderPid = getpid();
	state->officeOpen = 1;
	sem_post(sem);

	pid_t pid = fork();
	if (pid == 0) {
		execl("./workers/petent/petent", "petent", nullptr);
		_exit(1);
	}
	if (pid < 0) {
		shmdt(state);
		cleanupIpc(mqidPetent, mqidOther, shmid, sem);
		return false;
	}

	Message msg{};
	if (!waitForMsg(mqidPetent, getpid(), msg, 2000)) {
		kill(pid, SIGTERM);
		waitpid(pid, nullptr, 0);
		shmdt(state);
		cleanupIpc(mqidPetent, mqidOther, shmid, sem);
		return false;
	}
	bool gotEntry = msg.group == MessageGroup::Loader && msg.messageType.loaderType == LoaderMessagesEnum::NowyPetent;
	int petentQueueId = msg.replyQueueId;
	if (petentQueueId <= 0) {
		kill(pid, SIGTERM);
		waitpid(pid, nullptr, 0);
		shmdt(state);
		cleanupIpc(mqidPetent, mqidOther, shmid, sem);
		return false;
	}

	Message enter{};
	enter.mtype = 1;
	enter.senderId = getpid();
	enter.receiverId = pid;
	enter.replyQueueId = petentQueueId;
	enter.group = MessageGroup::Petent;
	enter.messageType.petentType = PetentMessagesEnum::WejdzDoBudynku;
	msgsnd(petentQueueId, &enter, sizeof(enter) - sizeof(long), 0);

	Message ticketReq{};
	if (!waitForMsg(mqidOther, static_cast<long>(ProcessMqType::Biletomat), ticketReq, 2000)) {
		kill(pid, SIGTERM);
		waitpid(pid, nullptr, 0);
		shmdt(state);
		cleanupIpc(mqidPetent, mqidOther, shmid, sem);
		return false;
	}
	bool gotTicketReq = ticketReq.group == MessageGroup::Biletomat && ticketReq.messageType.biletomatType == BiletomatMessagesEnum::PetentCzekaNaBilet;

	Message ticketResp{};
	ticketResp.mtype = 1;
	ticketResp.senderId = getpid();
	ticketResp.receiverId = pid;
	ticketResp.replyQueueId = petentQueueId;
	ticketResp.group = MessageGroup::Petent;
	ticketResp.messageType.petentType = PetentMessagesEnum::OtrzymanoBilet;
	msgsnd(petentQueueId, &ticketResp, sizeof(ticketResp) - sizeof(long), 0);

	Message served{};
	served.mtype = 1;
	served.senderId = getpid();
	served.receiverId = pid;
	served.replyQueueId = petentQueueId;
	served.group = MessageGroup::Petent;
	served.messageType.petentType = PetentMessagesEnum::Obsluzony;
	msgsnd(petentQueueId, &served, sizeof(served) - sizeof(long), 0);

	Message exitMsg{};
	bool gotExit = waitForMsg(mqidExit, getpid() + 1, exitMsg, 3000);
	bool exitOk = gotExit && exitMsg.group == MessageGroup::Loader && exitMsg.messageType.loaderType == LoaderMessagesEnum::PetentOpuszczaBudynek;

	waitpid(pid, nullptr, 0);
	shmdt(state);
	cleanupIpc(mqidPetent, mqidOther, shmid, sem);
	msgctl(mqidExit, IPC_RMID, nullptr);
	return gotEntry && gotTicketReq && exitOk;
}

bool testUrzednik() {
	std::cout << "[TEST] urzednik\n";
	int mqidPetent = msgget(IPC_PRIVATE, 0666);
	int mqidOther = initQueue(MQ_KEY_OTHER);
	int shmid = initShm();
	sem_t* sem = initSemaphore();
	if (mqidPetent == -1 || mqidOther == -1 || shmid == -1 || sem == nullptr) {
		cleanupIpc(mqidPetent, mqidOther, shmid, sem);
		return false;
	}

	SharedState* state = static_cast<SharedState*>(shmat(shmid, nullptr, 0));
	if (state == reinterpret_cast<SharedState*>(-1)) {
		cleanupIpc(mqidPetent, mqidOther, shmid, sem);
		return false;
	}
	sem_wait(sem);
	std::memset(state, 0, sizeof(SharedState));
	state->officeOpen = 1;
	sem_post(sem);

	pid_t pid = fork();
	if (pid == 0) {
		execl("./workers/urzednik/urzednik", "urzednik", "SC", nullptr);
		_exit(1);
	}
	if (pid < 0) {
		shmdt(state);
		cleanupIpc(mqidPetent, mqidOther, shmid, sem);
		return false;
	}

	Message call{};
	call.mtype = static_cast<long>(DepartmentMqType::SC);
	call.senderId = getpid();
	call.receiverId = 0;
	call.replyQueueId = mqidPetent;
	call.group = MessageGroup::Biletomat;
	call.messageType.biletomatType = BiletomatMessagesEnum::WydanoBiletCzekaj;
	msgsnd(mqidOther, &call, sizeof(call) - sizeof(long), 0);

	Message first{};
	if (!waitForMsg(mqidPetent, 0, first, 3000)) {
		kill(pid, SIGTERM);
		waitpid(pid, nullptr, 0);
		shmdt(state);
		cleanupIpc(mqidPetent, mqidOther, shmid, sem);
		return false;
	}
	bool firstOk = first.group == MessageGroup::Petent && first.messageType.petentType == PetentMessagesEnum::WezwanoDoUrzednika;

	Message second{};
	if (!waitForMsg(mqidPetent, 0, second, 3000)) {
		kill(pid, SIGTERM);
		waitpid(pid, nullptr, 0);
		shmdt(state);
		cleanupIpc(mqidPetent, mqidOther, shmid, sem);
		return false;
	}
	bool secondOk = second.group == MessageGroup::Petent &&
		(second.messageType.petentType == PetentMessagesEnum::Obsluzony ||
		 second.messageType.petentType == PetentMessagesEnum::IdzDoKasy);

	kill(pid, SIGTERM);
	waitpid(pid, nullptr, 0);
	shmdt(state);
	cleanupIpc(mqidPetent, mqidOther, shmid, sem);
	return firstOk && secondOk;
}
}

int main() {
	std::vector<std::pair<std::string, bool(*)()>> tests = {
		{"monitoring", testMonitoring},
		{"biletomat", testBiletomat},
		{"petent", testPetent},
		{"urzednik", testUrzednik}
	};

	bool ok = true;
	for (auto& t : tests) {
		if (!t.second()) {
			std::cerr << "[FAIL] " << t.first << "\n";
			ok = false;
		} else {
			std::cout << "[OK] " << t.first << "\n";
		}
	}

	return ok ? 0 : 1;
}
