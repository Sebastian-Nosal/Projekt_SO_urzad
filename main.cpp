#include <iostream>
#include <string>
#include <csignal>
#include <cerrno>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "config/config.h"

namespace {
volatile sig_atomic_t g_loaderPid = -1;

void forwardSignal(int sig) {
	if (g_loaderPid > 0) {
		kill(-g_loaderPid, sig);
	}
}
}

int main() {
	std::cout << "Podaj maksymalna liczbe petentow (ENTER = domyslna z configu): ";
	std::string input;
	std::getline(std::cin, input);

	if (!input.empty()) {
		try {
			int value = std::stoi(input);
			if (value > 0) {
				PETENT_MAX_COUNT_IN_MOMENT = value;
			}
		} catch (...) {
			// pozostaw domyslna wartosc z configu
		}
	}

	struct rlimit lim;
	if (getrlimit(RLIMIT_NPROC, &lim) == 0 && lim.rlim_cur != RLIM_INFINITY) {
		int limitProc = static_cast<int>(lim.rlim_cur);
		int bezpieczny = limitProc > 10 ? (limitProc - 10) : limitProc;
		if (bezpieczny > 0 && PETENT_MAX_COUNT_IN_MOMENT > bezpieczny) {
			std::cout << "Uwaga: limit procesow uzytkownika to " << limitProc
			          << ", zmniejszam maksymalna liczbe petentow do " << bezpieczny << "\n";
			PETENT_MAX_COUNT_IN_MOMENT = bezpieczny;
		}
	}

	std::cout << "Symulacja uruchomiona.\n";
	std::cout << "Sygnały sterujace:\n";
	std::cout << " - SIGUSR1: urzednik konczy po biezacym petencie\n";
	std::cout << " - SIGUSR2: petenci natychmiast opuszczaja budynek\n";
	std::cout << " - SIGINT/SIGQUIT: zamkniecie urzedu\n";

	std::cout << "Parametry:\n";
	std::cout << " - maksymalna liczba petentow: " << PETENT_MAX_COUNT_IN_MOMENT << "\n";

	// Uruchomienie symulacji
	pid_t pid = fork();
	if (pid == 0) {
		setpgid(0, 0);
		execl("./loader", "loader", nullptr);
		return 1;
	}
	if (pid < 0) {
		std::cerr << "Nie udalo sie uruchomic loadera\n";
		return 1;
	}
	setpgid(pid, pid);

	g_loaderPid = pid;
	std::signal(SIGINT, forwardSignal);
	std::signal(SIGTERM, forwardSignal);

	int status = 0;
	while (true) {
		pid_t w = waitpid(pid, &status, 0);
		if (w == -1) {
			if (errno == EINTR) {
				continue;
			}
			break;
		}
		break;
	}
	return status;
}
