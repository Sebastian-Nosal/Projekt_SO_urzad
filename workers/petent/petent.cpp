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

// SIGUSR2: dyrektor zamyka urząd
void sigusr2_handler(int sig) {
	pid_t pid = getpid();
	printf("[Petent -> PID=%d]: Jestem sfrustrowany\n", pid);
	_exit(0);
}

// SIGUSR1: petent jest obsługiwany przez urzędnika - sprawa załatwiona
void sigusr1_handler(int sig) {
	pid_t pid = getpid();
	printf("[Petent -> PID=%d]: Sprawa Zalatowiona\n", pid);
	_exit(0);
}

// SIGTERM: petent zostaje odprawiony z kwitkiem przez urzędnika (exhausted)
void sigterm_handler(int sig) {
	pid_t pid = getpid();
	printf("PID: %d odprawiony z kwitkiem\n", pid);
	_exit(0);
}

void petent_start(PetentData* petent) {
	pid_t pid = getpid();
	int fd = open(PIPE_NAME, O_WRONLY);
	if (fd < 0) {
		perror("[petent] Nie można otworzyć pipe do biletomatu");
		exit(1);
	}
	// Utwórz pakiet: komenda (16 bajtów) + struktura
	struct {
		char cmd[16];
		pid_t pid;
		int prio;
		wydzial_t typ;
	} packet;
	
	if (petent->isVIP)
		strcpy(packet.cmd, "ASSIGN_TICKET_TO");
	else
		strcpy(packet.cmd, "ASSIGN_TICKET");
	
	packet.pid = pid;
	packet.prio = petent->priorytet;
	packet.typ = petent->typ;
	
	// Log taking the ticket
	printf("[Petent -> PID=%d]: Pobiera bilet dla wydzialu %d, priorytet=%d%s\n", pid, petent->typ, petent->priorytet, petent->isVIP ? " (VIP)" : "");
	(void)write(fd, &packet, sizeof(packet));
	close(fd);
	printf("[Petent -> PID=%d]: Zgloszył się do wydziału %d, priorytet=%d%s\n", pid, petent->typ, petent->priorytet, petent->isVIP ? " (VIP)" : "");
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
	signal(SIGTERM, sigterm_handler);
	signal(SIGUSR1, sigusr1_handler);
	petent_start(&petent);
	// Oczekiwanie na obsługę lub zamknięcie urzędu - użyj pause() aby czekać na sygnał
	while (!zamkniecie_urzedu) {
		pause();  // Czekaj na sygnał
	}
	printf("[petent] PID=%d opuszcza urząd (zamknięcie)\n", getpid());
	return 0;
}
