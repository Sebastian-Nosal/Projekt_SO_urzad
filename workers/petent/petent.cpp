#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include "petent.h"
#include "../biletomat/biletomat.h"

volatile sig_atomic_t zamkniecie_urzedu = 0;

void sigusr2_handler(int sig) { zamkniecie_urzedu = 1; }

void petent_start(PetentData* petent) {
	pid_t pid = getpid();
	int fd = open(PIPE_NAME, O_WRONLY);
	if (fd < 0) {
		perror("[petent] Nie można otworzyć pipe do biletomatu");
		exit(1);
	}
	char cmd[32];
	if (petent->isVIP)
		strcpy(cmd, "ASSIGN_TICKET_TO");
	else
		strcpy(cmd, "ASSIGN_TICKET");
	// Log taking the ticket
	printf("[petent] PID=%d pobiera bilet dla wydziału %d, priorytet=%d%s\n", pid, petent->typ, petent->priorytet, petent->isVIP ? " (VIP)" : "");
	write(fd, cmd, strlen(cmd));
	struct { pid_t pid; int prio; wydzial_t typ; } req = { pid, petent->priorytet, petent->typ };
	write(fd, &req, sizeof(req));
	close(fd);
	printf("[petent] PID=%d zgłosił się do wydziału %d, priorytet=%d%s\n", pid, petent->typ, petent->priorytet, petent->isVIP ? " (VIP)" : "");
}

int main(int argc, char* argv[]) {
	if (argc < 3) {
		fprintf(stderr, "Użycie: %s <typ_urzedu (int)> <priorytet> [vip=1] [isInside=0|1]\n", argv[0]);
		return 1;
	}
	PetentData petent;
	petent.typ = (wydzial_t)atoi(argv[1]);
	petent.priorytet = atoi(argv[2]);
	petent.isVIP = (argc > 3) ? atoi(argv[3]) : 0;
	petent.isInside = (argc > 4) ? atoi(argv[4]) : 0;
	signal(SIGUSR2, sigusr2_handler);
	petent_start(&petent);
	// Oczekiwanie na obsługę lub zamknięcie urzędu
	while (!zamkniecie_urzedu) {
		sleep(1);
	}
	printf("[petent] PID=%d opuszcza urząd (zamknięcie)\n", getpid());
	return 0;
}
