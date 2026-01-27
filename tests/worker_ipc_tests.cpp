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

bool testMessageOrder() {
	std::cout << "[TEST] Testy kolejności komuniaktów\n";
	int mqid = msgget(IPC_PRIVATE, 0666);
	if (mqid == -1) return false;

	for (int i = 1; i <= 3; ++i) {
		Message msg{};
		msg.mtype = 1;
		msg.senderId = i;
		msg.data1 = i;
		if (msgsnd(mqid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
			perror("msgsnd failed");
			msgctl(mqid, IPC_RMID, nullptr);
			return false;
		}
	}

	for (int i = 1; i <= 3; ++i) {
		Message out{};
		if (msgrcv(mqid, &out, sizeof(out) - sizeof(long), 1, 0) == -1) {
			perror("msgrcv failed");
			msgctl(mqid, IPC_RMID, nullptr);
			return false;
		}
		if (out.data1 != i) {
			msgctl(mqid, IPC_RMID, nullptr);
			return false;
		}
	}

	msgctl(mqid, IPC_RMID, nullptr);
	return true;
}

bool testQueueLength() {
	std::cout << "[TEST] Test ilości petentów w kolejce\n";
	int mqid = msgget(IPC_PRIVATE, 0666);
	if (mqid == -1) return false;

	constexpr int kCount = 10;
	for (int i = 0; i < kCount; ++i) {
		Message msg{};
		msg.mtype = 1;
		msg.senderId = i;
		if (msgsnd(mqid, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
			perror("msgsnd failed");
			msgctl(mqid, IPC_RMID, nullptr);
			return false;
		}
	}

	msqid_ds ds{};
	if (msgctl(mqid, IPC_STAT, &ds) == -1) {
		perror("msgctl IPC_STAT failed");
		msgctl(mqid, IPC_RMID, nullptr);
		return false;
	}
	msgctl(mqid, IPC_RMID, nullptr);
	return ds.msg_qnum == kCount;
}

bool testBiletomatStart() {
	std::cout << "[TEST] Test uruchamiania biletomatów\n";
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

bool testDeadlockDetection() {
	std::cout << "[TEST] Test wykrywania zakleszczeń/deadlockó\n";
	std::remove(LOG_FILE.c_str());

	pid_t pid = fork();
	if (pid == 0) {
		int fd = open("/dev/null", O_WRONLY);
		if (fd != -1) {
			dup2(fd, STDOUT_FILENO);
			dup2(fd, STDERR_FILENO);
			close(fd);
		}
		execl("./main_all", "main_all", nullptr);
		_exit(1);
	}
	if (pid < 0) return false;

	std::this_thread::sleep_for(std::chrono::seconds(30));
	kill(pid, SIGINT);

	bool exited = false;
	for (int i = 0; i < 50; ++i) {
		int status = 0;
		pid_t w = waitpid(pid, &status, WNOHANG);
		if (w == pid) {
			exited = true;
			break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}
	if (!exited) {
		kill(pid, SIGKILL);
		waitpid(pid, nullptr, 0);
	}

	std::ifstream f(LOG_FILE);
	if (!f.good()) {
		return false;
	}
	bool hasPetent = false;
	bool hasBiletomat = false;
	bool hasUrzednik = false;
	std::string line;
	while (std::getline(f, line)) {
		if (line.find("petent") != std::string::npos) hasPetent = true;
		if (line.find("Biletomat") != std::string::npos) hasBiletomat = true;
		if (line.find("Urzędnik") != std::string::npos || line.find("Urzednik") != std::string::npos) hasUrzednik = true;
	}
	return hasPetent && hasBiletomat && hasUrzednik && exited;
}
}

int main() {
	std::vector<std::pair<std::string, bool(*)()>> tests = {
		{"Testy kolejności komuniaktów", testMessageOrder},
		{"Test ilości petentów w kolejce", testQueueLength},
		{"Test uruchamiania biletomatów", testBiletomatStart},
		{"Test wykrywania zakleszczeń/deadlockó", testDeadlockDetection}
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
