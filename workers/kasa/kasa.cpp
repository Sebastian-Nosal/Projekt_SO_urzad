#include <iostream>
#include <csignal>
#include <string>
#include <cerrno>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>

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
}

int main() {
	std::signal(SIGINT, obsluzSigint);

	int mqidPetent = initMessageQueue(MQ_KEY);
	int mqidOther = initMessageQueue(MQ_KEY + 1);

	while (dziala) {
		Message msg{};
		if (msgrcv(mqidOther, &msg, sizeof(msg) - sizeof(long), static_cast<long>(ProcessMqType::Kasa), 0) == -1) {
			if (errno == EINTR) {
				continue;
			}
			perror("msgrcv failed");
			continue;
		}

		if (msg.group != MessageGroup::Petent || msg.messageType.petentType != PetentMessagesEnum::IdzDoKasy) {
			continue;
		}

		std::cout << "{kasa, " << getpid() << "} petent=" << msg.senderId << std::endl;
		wyslijMonitoring(mqidOther, getpid(), "Kasa przyjęła petenta=" + std::to_string(msg.senderId));

		int officerPid = msg.data1;
		Message response{};
		response.mtype = officerPid > 0 ? officerPid : static_cast<long>(msg.data2);
		response.senderId = msg.senderId; // petent id
		response.receiverId = officerPid;
		response.group = MessageGroup::Petent;
		response.messageType.petentType = PetentMessagesEnum::WezwanoDoUrzednika;
		response.data1 = msg.senderId;
		response.data2 = msg.data2;

		if (msgsnd(mqidOther, &response, sizeof(response) - sizeof(long), 0) == -1) {
			perror("msgsnd failed");
		}
		std::cout << "{kasa, " << getpid() << "} odeslany do urzednika=" << officerPid << " petent=" << msg.senderId << std::endl;
		wyslijMonitoring(mqidOther, getpid(), "Kasa odesłała petenta=" + std::to_string(msg.senderId) + " do urzędnika=" + std::to_string(officerPid));
	}

	return 0;
}
