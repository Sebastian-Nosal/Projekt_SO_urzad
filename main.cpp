#include <iostream>
#include <vector>
#include <cstdlib>
#include "headers/loader.h"
#include "config.h"
#include <signal.h>
#include <unistd.h>

// Handler SIGINT w `main` — mostek: wysyła sygnały do dyrektora i wszystkich procesów
void sigint_bridge(int sig) {
	std::cout << "[main] Otrzymano SIGINT, inicjuję zamknięcie symulacji..." << std::endl;
	if (g_dyrektor_pid > 0) {
		kill(g_dyrektor_pid, SIGUSR2);
		std::cout << "[main] Wysłano SIGUSR2 do dyrektora..." << std::endl;
	}
	// Wysyłaj SIGUSR1 do wszystkich procesów przechowywanych w g_all_pids
	if (g_all_pids && g_all_pids_count > 0) {
		std::cout << "[main] Wysyłam SIGUSR1 do wszystkich procesów..." << std::endl;
		for (int i = 0; i < g_all_pids_count; ++i) {
			if (g_all_pids[i] > 0) {
				kill(g_all_pids[i], SIGUSR1);
			}
		}
	}
}

int main() {
	// Zainstaluj handler przed uruchomieniem symulacji; handler użyje globalnych zmiennych
	signal(SIGINT, sigint_bridge);

	int total_petents = PETENT_AMOUNT;
	start_simulation(total_petents);
	std::cout << "[main] Wszystkie procesy zakończone." << std::endl;
	std::cout << "[main] Koniec działania." << std::endl;
	return 0;
}
