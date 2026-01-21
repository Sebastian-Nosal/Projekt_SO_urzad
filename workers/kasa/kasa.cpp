#include <iostream>
#include <csignal>
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
}

int main() {
	std::signal(SIGINT, obsluzSigint);

	int mqidPetent = initMessageQueue(MQ_KEY);
	int mqidOther = initMessageQueue(MQ_KEY + 1);

	while (dziala) {
		Message msg{};
		if (msgrcv(mqidOther, &msg, sizeof(msg) - sizeof(long), static_cast<long>(ProcessMqType::Kasa), 0) == -1) {
			continue;
		}

		if (msg.group != MessageGroup::Petent || msg.messageType.petentType != PetentMessagesEnum::IdzDoKasy) {
			continue;
		}

		std::cout << "{kasa, " << getpid() << "} petent=" << msg.senderId << std::endl;

		Message response{};
		response.mtype = msg.senderId;
		response.senderId = getpid();
		response.receiverId = msg.senderId;
		response.group = MessageGroup::Petent;
		response.messageType.petentType = PetentMessagesEnum::WezwanoDoUrzednika;
		response.data1 = msg.data1;

		if (msgsnd(mqidPetent, &response, sizeof(response) - sizeof(long), 0) == -1) {
			perror("msgsnd failed");
		}
		std::cout << "{kasa, " << getpid() << "} odeslany do pid=" << msg.senderId << std::endl;
	}

	return 0;
}
