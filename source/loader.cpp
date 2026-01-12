#include "../headers/loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

void start_simulation(int liczba_petentow) {
	printf("[loader] Start symulacji z %d petentami\n", liczba_petentow);

	pid_t biletomat_pid = fork();
	if (biletomat_pid == 0) {
		execl("./workers/biletomat/biletomat", "biletomat", NULL);
		perror("execl biletomat");
		exit(1);
	}

	pid_t kasa_pid = fork();
	if (kasa_pid == 0) {
		execl("./workers/kasa/kasa", "kasa", NULL);
		perror("execl kasa");
		exit(1);
	}

	pid_t dyrektor_pid = fork();
	if (dyrektor_pid == 0) {
		execl("./workers/dyrektor/dyrektor", "dyrektor", NULL);
		perror("execl dyrektor");
		exit(1);
	}

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
		}
	}

	int petent_limits[WYDZIAL_COUNT] = {PETENT_LIMIT_SC, PETENT_LIMIT_KM, PETENT_LIMIT_ML, PETENT_LIMIT_PD, PETENT_LIMIT_SA};
	int petent_sum = 0;
	for (int i = 0; i < WYDZIAL_COUNT; ++i) petent_sum += petent_limits[i];
	int petenty_do_uruchomienia = liczba_petentow > 0 ? liczba_petentow : petent_sum;

	pid_t* allowed_petents = (pid_t*) malloc(sizeof(pid_t) * petenty_do_uruchomienia);
	int allowed_count = 0;

	for (int i = 0, idx = 0; i < WYDZIAL_COUNT; ++i) {
		int ile = petent_limits[i];
		for (int j = 0; j < ile && idx < petenty_do_uruchomienia; ++j, ++idx) {
			pid_t pid = fork();
			if (pid == 0) {
				char typ[8];
				char prio[8];
				char isInside[4] = "1";
				snprintf(typ, sizeof(typ), "%d", i);
				snprintf(prio, sizeof(prio), "%d", rand() % 10);
				execl("./workers/petent/petent", "petent", typ, prio, "0", isInside, NULL);
				perror("execl petent");
				exit(1);
			}
			allowed_petents[allowed_count++] = pid;
		}
	}

	for (int i = 0; i < urzednik_count; ++i) waitpid(urzednik_pids[i], NULL, 0);
	waitpid(biletomat_pid, NULL, 0);
	waitpid(kasa_pid, NULL, 0);
	waitpid(dyrektor_pid, NULL, 0);
	printf("[loader] Symulacja zakończona\n");
}
