#include "../biletomat/biletomat.h"
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
#include <time.h>
#include "petent.h"
#include <errno.h>

volatile sig_atomic_t zamkniecie_urzedu = 0;
volatile sig_atomic_t petent_shutdown = 0;  // Sygnał do przerwania czekania na bilet
volatile sig_atomic_t g_petent_has_ticket = 0;  // 1 jeśli petent faktycznie dostał bilet (jest liczony w people_inside)

typedef struct {
	char cmd[16];
	pid_t pid;
	int prio;
	wydzial_t typ;
} BiletomatPacket;

static void send_petent_left_best_effort(pid_t pid) {
	if (!g_petent_has_ticket) return;
	int fd = open(PIPE_NAME, O_WRONLY | O_NONBLOCK);
	if (fd < 0) return;
	BiletomatPacket packet;
	memset(&packet, 0, sizeof(packet));
	strncpy(packet.cmd, "PETENT_LEFT", sizeof(packet.cmd) - 1);
	packet.pid = pid;
	packet.prio = 0;
	packet.typ = (wydzial_t)0;
	(void)write(fd, &packet, sizeof(packet));
	close(fd);
}

// Global log file descriptor
FILE* g_petent_log = NULL;
char g_petent_logfile[256];

void petent_log(const char* message) {
	if (g_petent_log) {
		time_t now = time(NULL);
		struct tm* timeinfo = localtime(&now);
		char timestamp[32];
		strftime(timestamp, sizeof(timestamp), "%H:%M:%S", timeinfo);
		fprintf(g_petent_log, "[%s] %s\n", timestamp, message);
		fflush(g_petent_log);
	}
}

void cleanup_petent_log() {
	if (g_petent_log) {
		fclose(g_petent_log);
		g_petent_log = NULL;
		// Usuń plik logu
		if (strlen(g_petent_logfile) > 0) {
			unlink(g_petent_logfile);
		}
	}
}

// SIGUSR2: dyrektor zamyka urząd
void sigusr2_handler(int sig) {
	pid_t pid = getpid();
	printf("[Petent -> PID=%d]: Jestem sfrustrowany\n", pid);
	petent_log("SIGUSR2 handler - dyrektor zamyka urząd");
	send_petent_left_best_effort(pid);
	cleanup_petent_log();
	_exit(0);
}

// SIGUSR1: petent jest obsługiwany przez urzędnika - sprawa załatwiona
void sigusr1_handler(int sig) {
	pid_t pid = getpid();
	printf("[Petent -> PID=%d]: Sprawa Zalatowiona\n", pid);
	fflush(stdout);
	petent_log("SIGUSR1 handler - sprawa załatwiona");
	// Jeśli dostaliśmy SIGUSR1, to znaczy że urzędnik nas obsłużył (mieliśmy bilet), nawet jeśli nie zdążyliśmy tego odczytać z SHM.
	g_petent_has_ticket = 1;

	// Powiadom biletomat że opuszczamy budynek
	send_petent_left_best_effort(pid);
	petent_log("Wysłano PETENT_LEFT do biletomatu");
	cleanup_petent_log();
	printf("[Petent -> PID=%d]: Kończę proces po obsłudze (SIGUSR1)\n", pid);
	fflush(stdout);
	_exit(0);
}

// SIGTERM: petent zostaje odprawiony z kwitkiem przez urzędnika (exhausted)
void sigterm_handler(int sig) {
	pid_t pid = getpid();
	printf("PID: %d odprawiony z kwitkiem\n", pid);
	petent_log("SIGTERM handler - odprawiony");
	// SIGTERM w tym projekcie oznacza odprawienie przez urzędnika (petent miał bilet).
	g_petent_has_ticket = 1;
	send_petent_left_best_effort(pid);
	cleanup_petent_log();
	_exit(0);
}

// Wątek dla dziecka - informuje że petent wchodzi z dzieckiem
void* child_thread(void* arg) {
	(void)arg;  // Nieużywany parametr
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
	
	// Utwórz plik logu dla tego petenta
	snprintf(g_petent_logfile, sizeof(g_petent_logfile), "./temp/petent_PID_%d.log", pid);
	g_petent_log = fopen(g_petent_logfile, "w");
	
	char logmsg[256];
	snprintf(logmsg, sizeof(logmsg), "START - typ=%d, priorytet=%d, VIP=%d", petent->typ, petent->priorytet, petent->isVIP);
	petent_log(logmsg);

	// Wyślij żądanie biletu do biletomatu przez FIFO.
	BiletomatPacket req;
	memset(&req, 0, sizeof(req));
	strncpy(req.cmd, "ASSIGN_TICKET", sizeof(req.cmd) - 1);
	req.pid = pid;
	req.prio = petent->priorytet;
	req.typ = petent->typ;

	int sent = 0;
	for (int attempt = 0; !zamkniecie_urzedu; ++attempt) {
		int fd = open(PIPE_NAME, O_WRONLY | O_NONBLOCK);
		if (fd < 0) {
			// ENOENT: FIFO jeszcze nie istnieje; ENXIO: brak czytelnika (biletomat nie działa)
			sleep(1);
			continue;
		}
		ssize_t w = write(fd, &req, sizeof(req));
		close(fd);
		if (w == (ssize_t)sizeof(req)) {
			sent = 1;
			break;
		}
		perror("write ASSIGN_TICKET");
		sleep(1);
	}
	if (!sent) {
		petent_log("Nie udało się wysłać żądania biletu do biletomatu (zamknięcie urzędu)");
		return;
	}
	printf("[Petent -> PID=%d]: Wysłano żądanie o ticket przez FIFO\n", pid);
	petent_log("Wysłano żądanie o ticket przez FIFO");

	// Zamiast ciągłego odpytywania: czekaj na semafor sygnalizujący, że JAKIŚ bilet został przydzielony.
	// To semafor globalny (nie per-petent), więc po wybudzeniu zawsze weryfikujemy w SHM czy to my.
	sem_t* sem_ticket = SEM_FAILED;
	while (!zamkniecie_urzedu) {
		sem_ticket = sem_open(SEM_TICKET_ASSIGNED, 0);
		if (sem_ticket != SEM_FAILED) break;
		sleep(1);
	}

	// Czekaj aż bilet pojawi się w ticket_list (urzednik działa na tej kolejce).
	int ticket_wait_iteration = 0;
	while (1) {
		// Jeśli mamy semafor, blokuj się aż biletomat przydzieli jakiś bilet.
		if (sem_ticket != SEM_FAILED) {
			int rc;
			do {
				rc = sem_wait(sem_ticket);
			} while (rc == -1 && errno == EINTR);
		}
		ticket_wait_iteration++;

		int shm_fd = shm_open(SHM_NAME, O_RDONLY, 0666);
		if (shm_fd < 0) {
			if (ticket_wait_iteration % 5 == 0) petent_log("Czekam na SHM biletomatu...");
			continue;
		}
		// Wystarczy zmapować początek SHM: tickety + liczniki.
		size_t shm_size = WYDZIAL_COUNT * sizeof(struct ticket) * MAX_TICKETS +
					  WYDZIAL_COUNT * sizeof(int);
		void* shm_ptr = mmap(0, shm_size, PROT_READ, MAP_SHARED, shm_fd, 0);
		if (shm_ptr == MAP_FAILED) {
			close(shm_fd);
			continue;
		}
		char* base = (char*)shm_ptr;
		struct ticket* my_tickets = (struct ticket*)(base + petent->typ * sizeof(struct ticket) * MAX_TICKETS);
		int* ticket_count = (int*)(base + WYDZIAL_COUNT * sizeof(struct ticket) * MAX_TICKETS);
		int queue_size = ticket_count[petent->typ];

		for (int i = 0; i < queue_size; ++i) {
			if (my_tickets[i].PID == pid) {
				g_petent_has_ticket = 1;
				printf("[Petent -> PID=%d]: Otrzymałem bilet! Pozycja w kolejce: %d\n", pid, i + 1);
				snprintf(logmsg, sizeof(logmsg), "BILET PRZYDZIELONY - pozycja w kolejce: %d (iteracja %d)", i + 1, ticket_wait_iteration);
				petent_log(logmsg);
				munmap(shm_ptr, shm_size);
				close(shm_fd);
				petent_log("Pobrałem bilet, czekam na obsługę");
				return;
			}
		}
		if (sem_ticket == SEM_FAILED) {
			// Fallback jeśli semafor nie jest dostępny.
			sleep(1);
			if (ticket_wait_iteration % 5 == 0) {
				snprintf(logmsg, sizeof(logmsg), "Czekanie na bilet (iteracja %d)", ticket_wait_iteration);
				petent_log(logmsg);
			}
		}
		munmap(shm_ptr, shm_size);
		close(shm_fd);
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
	petent_log("KONIEC - zamknięcie normalnie");
	send_petent_left_best_effort(getpid());
	cleanup_petent_log();
	return 0;
}
