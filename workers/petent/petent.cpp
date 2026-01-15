#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <signal.h>
#include <pthread.h>
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
	
	// Wyślij pakiet do biletomatu że opuszczamy budynek
	int fd = open(PIPE_NAME, O_WRONLY);
	if (fd >= 0) {
		struct {
			char cmd[16];
			pid_t pid;
			int prio;
			int typ;
		} packet;
		strcpy(packet.cmd, "PETENT_LEFT");
		packet.pid = pid;
		packet.prio = 0;
		packet.typ = 0;
		write(fd, &packet, sizeof(packet));
		close(fd);
	}
	
	_exit(0);
}

// SIGTERM: petent zostaje odprawiony z kwitkiem przez urzędnika (exhausted)
void sigterm_handler(int sig) {
	pid_t pid = getpid();
	printf("PID: %d odprawiony z kwitkiem\n", pid);
	_exit(0);
}

// Wątek dla dziecka - informuje że petent wchodzi z dzieckiem
void* child_thread(void* arg) {
	PetentData* petent = (PetentData*)arg;
	pid_t parent_pid = getpid();
	printf("[Petent -> PID=%d]: Wchodzę do urzędu z dzieckiem (Identyfikator dziecka: %d)\n", parent_pid, parent_pid + 1000);
	pthread_exit(NULL);
}

void spawn_child_thread(PetentData* petent) {
	pthread_t tid;
	pthread_create(&tid, NULL, child_thread, petent);
	pthread_detach(tid);
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
	
	// Wyślij żądanie do biletomatu (będę czekać w waiting_list)
	printf("[Petent -> PID=%d]: Wysyłam żądanie do biletomatu dla wydziału %d, priorytet=%d%s\n", pid, petent->typ, petent->priorytet, petent->isVIP ? " (VIP)" : "");
	(void)write(fd, &packet, sizeof(packet));
	close(fd);
	
	// Czekaj aż dostanę bilet ALBO wydział będzie zamknięty
	// Bez timeoutu - czekaj w nieskończoność
	while (1) {
		sleep(1);
		
		// Otwórz SHM i sprawdzenie
		int shm_fd = shm_open(SHM_NAME, O_RDONLY, 0666);
		if (shm_fd >= 0) {
			size_t shm_size = WYDZIAL_COUNT * sizeof(struct ticket) * MAX_TICKETS + 
							  WYDZIAL_COUNT * sizeof(int) + 
							  sizeof(struct waiting_petent) * MAX_WAITING + 
							  WYDZIAL_COUNT * sizeof(int) +
							  WYDZIAL_COUNT * sizeof(int) +
							  sizeof(int) * 2;
			void* shm_ptr = mmap(0, shm_size, PROT_READ, MAP_SHARED, shm_fd, 0);
			if (shm_ptr != MAP_FAILED) {
				char* base = (char*)shm_ptr;
				
				// 1. Szukaj w ticket_list (już mam bilet?)
				struct ticket* my_tickets = (struct ticket*)(base + petent->typ * sizeof(struct ticket) * MAX_TICKETS);
				int* ticket_count = (int*)(base + WYDZIAL_COUNT * sizeof(struct ticket) * MAX_TICKETS);
				int queue_size = ticket_count[petent->typ];
				
				int found_in_tickets = 0;
				for (int i = 0; i < queue_size; ++i) {
					if (my_tickets[i].PID == pid) {
						found_in_tickets = 1;
						printf("[Petent -> PID=%d]: Otrzymałem bilet! Pozycja w kolejce: %d\n", pid, i+1);
						break;
					}
				}
				
				if (found_in_tickets) {
					munmap(shm_ptr, shm_size);
					close(shm_fd);
					break;  // Mam bilet, wychodzimy z czekania
				}
				
				// 2. Sprawdź czy mój wydział jest zamknięty
				int* wydzial_closed = (int*)(base + WYDZIAL_COUNT * sizeof(struct ticket) * MAX_TICKETS + 
											 WYDZIAL_COUNT * sizeof(int) + 
											 sizeof(struct waiting_petent) * MAX_WAITING + 
											 WYDZIAL_COUNT * sizeof(int));
				
				if (wydzial_closed[petent->typ] == 1) {
					printf("[Petent -> PID=%d]: Mój wydział (%d) jest zamknięty - kończę\n", pid, petent->typ);
					munmap(shm_ptr, shm_size);
					close(shm_fd);
					break;  // Wydział zamknięty, wychodzimy z czekania
				}
				
				munmap(shm_ptr, shm_size);
			}
			close(shm_fd);
		}
	}
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
	
	// Losuj czy petent ma dziecko
	double r = (double)rand() / RAND_MAX;
	petent.hasChild = (r < PROB_CHILD) ? 1 : 0;
	
	// Jeśli ma dziecko, uruchom wątek
	if (petent.hasChild) {
		spawn_child_thread(&petent);
	}
	
	signal(SIGUSR2, sigusr2_handler);
	signal(SIGTERM, sigterm_handler);
	signal(SIGUSR1, sigusr1_handler);
	petent_start(&petent);
	
	// Oczekiwanie na obsługę lub zamknięcie urzędu - czekaj na sygnały
	while (!zamkniecie_urzedu) {
		sleep(1);  // Czekaj na sygnał
	}
	printf("[petent] PID=%d opuszcza urząd (zamknięcie)\n", getpid());
	return 0;
}
