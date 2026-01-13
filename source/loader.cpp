#include "../headers/loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <csignal>
#include <time.h>


// Definicje globalnych zmiennych zadeklarowanych w headers/loader.h
volatile pid_t g_dyrektor_pid = 0;
volatile sig_atomic_t g_pending_sigint = 0;

// Globalne PIDs do zarządzania
pid_t* g_all_pids = NULL;
int g_all_pids_count = 0;

// Handler SIGUSR1: wysyła SIGUSR1 do urzędników i SIGTERM do petentów
void sigusr1_shutdown_handler(int sig) {
	printf("[loader] Otrzymano sygnał zamknięcia. Wysyłam sygnały do wszystkich procesów...\n");
	for (int i = 0; i < g_all_pids_count; ++i) {
		if (g_all_pids[i] > 0) {
			// Pierwsze 3 procesy: dyrektor, biletomat, kasa - SIGUSR1
			// Pozostałe: urzędnicy + petenci - wysyłamy SIGUSR1 do urzędników, SIGTERM do petentów
			if (i < 3) {
				kill(g_all_pids[i], SIGUSR1);
			} else {
				kill(g_all_pids[i], SIGUSR1);
			}
		}
	}
	printf("[loader] Sygnały wysłane.\n");
}

void start_simulation(int liczba_petentow) {
	printf("[loader] Start symulacji z %d petentami\n", liczba_petentow);

	// Alokuj miejsce na wszystkie PIDs
	g_all_pids_count = 0;
	g_all_pids = (pid_t*) malloc(sizeof(pid_t) * (1 + 1 + 1 + 10 + liczba_petentow)); // dyrektor + biletomat + kasa + urzednicy + petenci

	pid_t dyrektor_pid = fork();
	if (dyrektor_pid == 0) {
		execl("./workers/dyrektor/dyrektor", "dyrektor", NULL);
		perror("execl dyrektor");
		exit(1);
	}
	g_all_pids[g_all_pids_count++] = dyrektor_pid;

	// Ustaw globalny PID dyrektora, aby `main` mógł wysyłać sygnały (mostek SIGINT)
	extern volatile pid_t g_dyrektor_pid;
	extern volatile sig_atomic_t g_pending_sigint;
	g_dyrektor_pid = dyrektor_pid;
	if (g_pending_sigint) {
		kill(g_dyrektor_pid, SIGUSR2);
		g_pending_sigint = 0;
	}

	pid_t biletomat_pid = fork();
	if (biletomat_pid == 0) {
		execl("./workers/biletomat/biletomat", "biletomat", NULL);
		perror("execl biletomat");
		exit(1);
	}
	g_all_pids[g_all_pids_count++] = biletomat_pid;

	pid_t kasa_pid = fork();
	if (kasa_pid == 0) {
		execl("./workers/kasa/kasa", "kasa", NULL);
		perror("execl kasa");
		exit(1);
	}
	g_all_pids[g_all_pids_count++] = kasa_pid;

	pid_t urzednik_pids[10];
	int urzednik_count = 0;
	for (int wydzial = 0; wydzial < WYDZIAL_COUNT; ++wydzial) {
		int ile = (wydzial == WYDZIAL_SA) ? 2 : 1;
		for (int j = 0; j < ile; ++j) {
			pid_t upid = fork();
			if (upid == 0) {
				char typ[8];
				snprintf(typ, sizeof(typ), "%d", wydzial);
				execl("./workers/urzednik/urzednik", "urzednik", typ, NULL);
				perror("execl urzednik");
				exit(1);
			}
			urzednik_pids[urzednik_count++] = upid;
			g_all_pids[g_all_pids_count++] = upid;
		}
	}

	int petent_limits[WYDZIAL_COUNT] = {PETENT_LIMIT_SC, PETENT_LIMIT_KM, PETENT_LIMIT_ML, PETENT_LIMIT_PD, PETENT_LIMIT_SA};
	int petent_sum = 0;
	for (int i = 0; i < WYDZIAL_COUNT; ++i) petent_sum += petent_limits[i];
	int petenty_do_uruchomienia = liczba_petentow > 0 ? liczba_petentow : petent_sum;

	pid_t* allowed_petents = (pid_t*) malloc(sizeof(pid_t) * petenty_do_uruchomienia);
	int allowed_count = 0;

	// Rozprowadź petentów równomiernie na wydziały
	for (int idx = 0; idx < petenty_do_uruchomienia; ++idx) {
		int wydzial = idx % WYDZIAL_COUNT;
		pid_t pid = fork();
		if (pid == 0) {
			char typ[8];
			char prio[8];
			char isInside[4] = "1";
			snprintf(typ, sizeof(typ), "%d", wydzial);
			snprintf(prio, sizeof(prio), "%d", rand() % 10);
			execl("./workers/petent/petent", "petent", typ, prio, "0", isInside, NULL);
			perror("execl petent");
			exit(1);
		}
		allowed_petents[allowed_count++] = pid;
		g_all_pids[g_all_pids_count++] = pid;
	}

	// Zainstaluj handler SIGUSR1 do obsługi sygnału zamknięcia
	signal(SIGUSR1, sigusr1_shutdown_handler);

	// Czekaj na wszystkie procesy z monitorowaniem petentów
	printf("[loader] Oczekiwanie na procesy. Będę sprawdzać liczbę żywych petentów co 10 sekund.\n");
	int petents_alive = allowed_count;
	time_t last_check = time(NULL);
	int all_done = 0;
	
	while (!all_done) {
		// Sprawdzaj co 10 sekund ile petentów jeszcze żyje
		time_t now = time(NULL);
		if (now - last_check >= 10) {
			petents_alive = 0;
			for (int i = 0; i < allowed_count; ++i) {
				if (kill(allowed_petents[i], 0) == 0) {
					petents_alive++;
				}
			}
			printf("[loader] Petenci wciąż czekający: %d/%d\n", petents_alive, allowed_count);
			last_check = now;
		}
		
		// Nieblokujące czekanie na procesy
		int urzednicy_done = 0;
		for (int i = 0; i < urzednik_count; ++i) {
			if (waitpid(urzednik_pids[i], NULL, WNOHANG) > 0) {
				urzednik_pids[i] = -1;
				urzednicy_done++;
			}
		}
		
		int petents_done = 0;
		for (int i = 0; i < allowed_count; ++i) {
			if (allowed_petents[i] > 0 && waitpid(allowed_petents[i], NULL, WNOHANG) > 0) {
				allowed_petents[i] = -1;
				petents_done++;
			}
		}
		
		// Sprawdzaj czy wszystkie procesy się skończyły
		all_done = 1;
		for (int i = 0; i < urzednik_count; ++i) {
			if (urzednik_pids[i] > 0) { all_done = 0; break; }
		}
		for (int i = 0; i < allowed_count && all_done; ++i) {
			if (allowed_petents[i] > 0) { all_done = 0; break; }
		}
		if (waitpid(biletomat_pid, NULL, WNOHANG) == 0) all_done = 0;
		if (waitpid(kasa_pid, NULL, WNOHANG) == 0) all_done = 0;
		if (waitpid(dyrektor_pid, NULL, WNOHANG) == 0) all_done = 0;
		
		if (!all_done) sleep(1);
	}
	
	printf("[loader] Symulacja zakończona\n");
	
	free(g_all_pids);
	free(allowed_petents);
}
