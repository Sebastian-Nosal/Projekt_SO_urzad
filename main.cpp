#include <iostream>
#include <vector>
#include <cstdlib>
#include <cstring>
#include "headers/loader.h"
#include "config.h"
#include <signal.h>
#include <unistd.h>

void sigint_bridge(int sig) {
	std::cout << "[main] Otrzymano SIGINT, inicjuję zamknięcie symulacji..." << std::endl;
	if (g_dyrektor_pid > 0) {
		kill(g_dyrektor_pid, SIGUSR2);
		std::cout << "[main] Wysłano SIGUSR2 do dyrektora..." << std::endl;
	}
	if (g_all_pids && g_all_pids_count > 0) {
		std::cout << "[main] Wysyłam SIGUSR1 do wszystkich procesów..." << std::endl;
		for (int i = 0; i < g_all_pids_count; ++i) {
			if (g_all_pids[i] > 0) {
				kill(g_all_pids[i], SIGUSR1);
			}
		}
	}
}

int main(int argc, char** argv) {
	signal(SIGINT, sigint_bridge);

	bool dry_run = false;
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--dry-run") == 0) {
			dry_run = true;
		}
	}
	if (const char* env = std::getenv("SIM_DRY_RUN")) {
		if (std::strcmp(env, "1") == 0) dry_run = true;
	}

	int max_processes = get_process_limit();
	std::cout << "[main] Maksymalny limit procesów dla użytkownika: " << max_processes << std::endl;
	
	int base_processes = 1 + 1 + 1 + 10;
	int softcap = max_processes - base_processes - 20;
	if (softcap < 1) softcap = 1;
	
	std::cout << "[main] Maksymalnie petentów do uruchomienia (softcap): " << softcap << std::endl;
	
	int total_petents;
	std::cout << "[main] Podaj ilość petentów (domyślnie " << PETENT_AMOUNT << "): ";
	std::cin >> total_petents;
	
	if (total_petents > softcap) {
		std::cout << "[main] OSTRZEŻENIE: Podana ilość " << total_petents << " petentów przekracza softcap " << softcap << "!" << std::endl;
		std::cout << "[main] Ograniczam do " << softcap << " petentów." << std::endl;
		total_petents = softcap;
	}
	
	std::cout << "[main] Uruchamianie symulacji z " << total_petents << " petentami..." << std::endl;
	if (dry_run) {
		std::cout << "[main] (dry-run) Pomijam start_simulation()" << std::endl;
		return 0;
	}
	start_simulation(total_petents);
	std::cout << "[main] Wszystkie procesy zakończone." << std::endl;
	std::cout << "[main] Koniec działania." << std::endl;
	return 0;
}
