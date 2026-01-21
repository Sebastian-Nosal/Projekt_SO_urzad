#include <iostream>
#include <fstream>
#include <csignal>
#include <sys/ipc.h>
#include <sys/msg.h>

#include "config/config.h"
#include "config/messages.h"

namespace {
volatile sig_atomic_t dziala = 1;

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
}

int main() {
	std::signal(SIGINT, obsluzSigint);

	int mqidOther = initMessageQueue(MQ_KEY + 1);
	std::ofstream logFile(LOG_FILE, std::ios::app);

	while (dziala) {
		Message msg{};
		if (msgrcv(mqidOther, &msg, sizeof(msg) - sizeof(long), static_cast<long>(ProcessMqType::Monitoring), 0) == -1) {
			continue;
		}

		if (msg.group != MessageGroup::Monitoring) {
			continue;
		}

		const char* prefix = typLogu(msg.messageType.monitoringType);
		std::string text = msg.data3;

		std::cout << "{monitoring} " << prefix << " " << text << std::endl;
		if (logFile.is_open()) {
			logFile << "{monitoring} " << prefix << " " << text << std::endl;
			logFile.flush();
		}
	}

	return 0;
}
