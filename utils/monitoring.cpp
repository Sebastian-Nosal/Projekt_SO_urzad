#include <iostream>
#include <fstream>
#include <csignal>
#include <cerrno>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <chrono>

#include "config/config.h"
#include "config/messages.h"
#include "utils/mq_semaphore.h"

namespace {
volatile sig_atomic_t dziala = 1;
sem_t* g_otherQueueSem = nullptr;
sem_t* g_stateSem = nullptr;

void obsluzSigint(int) {
	dziala = 0;
}

int initMessageQueue(key_t key) {
	int mqid = msgget(key, IPC_CREAT | 0666);
	if (mqid == -1) {
		perror("msgget failed");
		exit(EXIT_FAILURE);
	}
	return mqid;
}

const char* typLogu(MonitoringMessagesEnum typ) {
	switch (typ) {
		case MonitoringMessagesEnum::Log: return "[log]";
		case MonitoringMessagesEnum::Error: return "[error]";
		case MonitoringMessagesEnum::Debug: return "[debug]";
		default: return "[log]";
	}
}

void logSemState(const char* name, sem_t* sem) {
	if (!sem || sem == SEM_FAILED) {
		//std::cout << "\033[33m[semaphore]\033[0m pid=" << getpid() << " name=" << name << " stan=UNAVAILABLE" << std::endl;
		return;
	}
	int value = 0;
	if (sem_getvalue(sem, &value) == -1) {
		//std::cout << "\033[33m[semaphore]\033[0m pid=" << getpid() << " name=" << name << " stan=ERROR" << std::endl;
		return;
	}
	const char* stan = (value <= 0) ? "ZAMKNIETY" : "OTWARTY";
	//std::cout << "\033[33m[semaphore]\033[0m pid=" << getpid() << " name=" << name
	//          << " stan=" << stan << " wartosc=" << value << std::endl;
}
}

int main() {
	std::signal(SIGINT, SIG_IGN);
	std::signal(SIGUSR2, SIG_IGN);
	std::signal(SIGTERM, obsluzSigint);

	int mqidOther = initMessageQueue(MQ_KEY_OTHER);
	setOtherQueueId(mqidOther);
	g_otherQueueSem = openOtherQueueSemaphore(false);
	g_stateSem = sem_open(SEMAPHORE_NAME, O_CREAT, 0666, 1);
	std::ofstream logFile(LOG_FILE, std::ios::app);
	auto lastSemLog = std::chrono::steady_clock::now();

	while (dziala) {
		auto now = std::chrono::steady_clock::now();
		if (now - lastSemLog >= std::chrono::seconds(1)) {
			logSemState(OTHER_QUEUE_SEM_NAME, g_otherQueueSem);
			logSemState(PETENT_QUEUE_SEM_NAME, sem_open(PETENT_QUEUE_SEM_NAME, O_CREAT, 0666, ENTRY_QUEUE_LIMIT));
			logSemState(SEMAPHORE_NAME, g_stateSem);
			lastSemLog = now;
		}

		Message msg{};
		if (msgrcv(mqidOther, &msg, sizeof(msg) - sizeof(long), static_cast<long>(ProcessMqType::Monitoring), 0) == -1) {
			if (errno == EINTR) {
				continue;
			}
			perror("msgrcv failed");
			continue;
		}
		otherQueueReleaseSlot(g_otherQueueSem);

		if (msg.group != MessageGroup::Monitoring) {
			continue;
		}

		const char* prefix = typLogu(msg.messageType.monitoringType);
		std::string text = msg.data3;

		std::cout << "\033[34m\033[1m[monitoring]\033[36m " << prefix << " \033[0m" << text << std::endl;
		if (logFile.is_open()) {
			logFile << "[monitoring]" << prefix << " ["<< msg.senderId <<"] " << text << std::endl;
			logFile.flush();
		}
	}

	return 0;
}
