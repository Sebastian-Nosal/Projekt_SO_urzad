#include "../headers/loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <csignal>
#include <time.h>
#include <format>
#include <sstream>
#include "../utils/zapisz_logi.h"


volatile pid_t g_dyrektor_pid = 0;
volatile sig_atomic_t g_pending_sigint = 0;

int get_process_limit() {
	struct rlimit limit;
	if (getrlimit(RLIMIT_NPROC, &limit) == 0) {
		std::ostringstream oss;
		oss << "Limit proces\u00f3w dla u\u017cytkownika: " << limit.rlim_cur;
		zapisz_log("loader", 0, oss.str());
		return (int)limit.rlim_cur;
	}
	int global_limit = 1024;
	FILE* fp = fopen("/proc/sys/kernel/pid_max", "r");
	if (fp) {
		if (fscanf(fp, "%d", &global_limit) != 1) {
		}
		fclose(fp);
		std::ostringstream oss;
		oss << "Nie mo\u017cna odczyta\u0107 limitu u\u017cytkownika, u\u017cywam globalnego: " << global_limit;
		zapisz_log("loader", 0, oss.str());
	}
	return global_limit;
}

pid_t* g_all_pids = NULL;
int g_all_pids_count = 0;

void sigusr1_shutdown_handler(int sig) {
	std::ostringstream oss;
	oss << "Otrzymano sygna\u0142 zamkni\u0119cia. Wysy\u0142am sygna\u0142y do wszystkich proces\u00f3w...";
	zapisz_log("loader", 0, oss.str());
	for (int i = 0; i < g_all_pids_count; ++i) {
		if (g_all_pids[i] > 0) {

			if (i < 3) {
				kill(g_all_pids[i], SIGUSR1);
			} else {
				kill(g_all_pids[i], SIGUSR1);
			}
		}
	}
	std::ostringstream oss2;
	oss2 << "Sygna\u0142y wys\u0142ane.";
	zapisz_log("loader", 0, oss2.str());
}

void start_simulation(int liczba_petentow) {
	std::ostringstream oss;
	oss << "Start symulacji z " << liczba_petentow << " petentami";
	zapisz_log("Loader", getpid(), oss.str());

	g_all_pids_count = 0;
	g_all_pids = (pid_t*) malloc(sizeof(pid_t) * (1 + 1 + 1 + 10 + liczba_petentow)); // dyrektor + biletomat + kasa + urzednicy + petenci

	pid_t dyrektor_pid = fork();
	if (dyrektor_pid == 0) {
		execl("./workers/dyrektor/dyrektor", "dyrektor", NULL);
		perror("execl dyrektor");
		exit(1);
	}
	g_all_pids[g_all_pids_count++] = dyrektor_pid;

	extern volatile pid_t g_dyrektor_pid;
	extern volatile sig_atomic_t g_pending_sigint;
	g_dyrektor_pid = dyrektor_pid;
	if (g_pending_sigint) {
		kill(g_dyrektor_pid, SIGUSR2);
		g_pending_sigint = 0;
	}

	pid_t biletomat_pid = fork();
	if (biletomat_pid == 0) {
		char n_str[16];
		snprintf(n_str, sizeof(n_str), "%d", liczba_petentow);
		execl("./workers/biletomat/biletomat", "biletomat", n_str, NULL);
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

	signal(SIGUSR1, sigusr1_shutdown_handler);

	int petents_alive = allowed_count;
	time_t last_check = time(NULL);
	int all_done = 0;
	
	while (!all_done) {
		time_t now = time(NULL);
		if (now - last_check >= 2) {
			petents_alive = 0;
			for (int i = 0; i < allowed_count; ++i) {
				if (allowed_petents[i] > 0 && kill(allowed_petents[i], 0) == 0) {
					petents_alive++;
				}
			}
			printf("[Loader -> PID=%d]: Petenci wciąż czekający: %d/%d\n", getpid(), petents_alive, allowed_count);
			if (petents_alive == 0) {
				printf("[Loader -> PID=%d]: Wszyscy petenci zostali obsłużeni lub odprawieni.\n", getpid());
				all_done = 1;
				break;
			}
			last_check = now;
		}
		
		int urzednicy_done = 0;
		for (int i = 0; i < urzednik_count; ++i) {
			if (waitpid(urzednik_pids[i], NULL, WNOHANG) > 0) {
				urzednik_pids[i] = -1;
				urzednicy_done++;
			}
		}
		
		int all_urzednicy_done = 1;
		for (int i = 0; i < urzednik_count; ++i) {
			if (urzednik_pids[i] > 0) { all_urzednicy_done = 0; break; }
		}
		
		static int urzednicy_finished_signaled = 0;
		if (all_urzednicy_done && !urzednicy_finished_signaled) {
			printf("[Loader -> PID=%d]: Wszyscy urzednicy skonczyli prace! Wysyłam SIGTERM do czekających petentów...\n", getpid());
			int remaining = 0;
			for (int i = 0; i < allowed_count; ++i) {
				if (allowed_petents[i] > 0) {
					kill(allowed_petents[i], SIGTERM);
					remaining++;
				}
			}
			printf("[Loader -> PID=%d]: Odprawiono %d petentów (urzędnicy się skończyli)\n", getpid(), remaining);
			urzednicy_finished_signaled = 1;
		}
		
		static int sigkill_sent = 0;
		static time_t sigterm_time = 0;
		if (urzednicy_finished_signaled && !sigkill_sent) {
			if (sigterm_time == 0) sigterm_time = time(NULL);
			if (time(NULL) - sigterm_time >= 1) {  
				int still_alive = 0;
				for (int i = 0; i < allowed_count; ++i) {
					if (allowed_petents[i] > 0 && kill(allowed_petents[i], 0) == 0) {
						printf("[Loader -> PID=%d]: SIGKILL dla petenta PID=%d (nie odpowiedział na SIGTERM)\n", getpid(), allowed_petents[i]);
						kill(allowed_petents[i], SIGKILL);
						still_alive++;
					}
				}
				if (still_alive > 0) {
					printf("[Loader -> PID=%d]: Wysłano SIGKILL do %d petentów\n", getpid(), still_alive);
				}
				sigkill_sent = 1;
			}
		}
		
		int petents_done = 0;
		for (int i = 0; i < allowed_count; ++i) {
			if (allowed_petents[i] > 0 && waitpid(allowed_petents[i], NULL, WNOHANG) > 0) {
				allowed_petents[i] = -1;
				petents_done++;
			}
		}
		
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

	printf("[Loader -> PID=%d]: Czekam na biletomat (posprzątanie zasobów)...\n", getpid());
	waitpid(biletomat_pid, NULL, 0);
	printf("[Loader -> PID=%d]: Biletomat się skończył i posprzątał zasoby\n", getpid());

	printf("[Loader -> PID=%d]: Czekam na kasę...\n", getpid());
	waitpid(kasa_pid, NULL, 0);
	printf("[Loader -> PID=%d]: Kasa się skończyła\n", getpid());
	
	printf("[Loader -> PID=%d]: Czekam na dyrektora...\n", getpid());
	waitpid(dyrektor_pid, NULL, 0); 
	printf("[Loader -> PID=%d]: Dyrektor się skończył\n", getpid());
	
	printf("[loader] Symulacja zakończona\n");
	
	free(g_all_pids);
	free(allowed_petents);
}
