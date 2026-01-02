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

void petent_start(wydzial_t typ, int priorytet, int is_vip) {
	pid_t pid = getpid();
	int fd = open(PIPE_NAME, O_WRONLY);
	if (fd < 0) {
		perror("[petent] Nie można otworzyć pipe do biletomatu");
		exit(1);
	}
	char cmd[32];
	if (is_vip)
		strcpy(cmd, "ASSIGN_TICKET_TO");
	else
		strcpy(cmd, "ASSIGN_TICKET");
	write(fd, cmd, strlen(cmd));
	struct { pid_t pid; int prio; wydzial_t typ; } req = { pid, priorytet, typ };
	write(fd, &req, sizeof(req));
	close(fd);
	printf("[petent] PID=%d zgłosił się do wydziału %d, priorytet=%d%s\n", pid, typ, priorytet, is_vip ? " (VIP)" : "");
}

int main(int argc, char* argv[]) {
	if (argc < 3) {
		fprintf(stderr, "Użycie: %s <typ_urzedu (int)> <priorytet> [vip=1] \n", argv[0]);
		return 1;
	}
	wydzial_t typ = (wydzial_t)atoi(argv[1]);
	int priorytet = atoi(argv[2]);
	int is_vip = (argc > 3) ? atoi(argv[3]) : 0;
	signal(SIGUSR2, sigusr2_handler);
	petent_start(typ, priorytet, is_vip);
	// Oczekiwanie na obsługę lub zamknięcie urzędu
	while (!zamkniecie_urzedu) {
		sleep(1);
	}
	printf("[petent] PID=%d opuszcza urząd (zamknięcie)\n", getpid());
	return 0;
}
