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

	// Pobierz maksymalny limit procesów
	int max_processes = get_process_limit();
	std::cout << "[main] Maksymalny limit procesów dla użytkownika: " << max_processes << std::endl;
	
	// Oblicz softcap: 20 mniej niż limit
	// Całkowite procesy: dyrektor (1) + biletomat (1) + kasa (1) + 10 urzędników + petenci
	int base_processes = 1 + 1 + 1 + 10; // 13 procesów bazowych
	int softcap = max_processes - base_processes - 20;
	if (softcap < 1) softcap = 1;
	
	std::cout << "[main] Maksymalnie petentów do uruchomienia (softcap): " << softcap << std::endl;
	
	// Wczytaj ilość petentów od użytkownika
	int total_petents;
	std::cout << "[main] Podaj ilość petentów (domyślnie " << PETENT_AMOUNT << "): ";
	std::cin >> total_petents;
	
	// Sprawdzenie czy podana wartość przekracza limit
	if (total_petents > softcap) {
		std::cout << "[main] OSTRZEŻENIE: Podana ilość " << total_petents << " petentów przekracza softcap " << softcap << "!" << std::endl;
		std::cout << "[main] Ograniczam do " << softcap << " petentów." << std::endl;
		total_petents = softcap;
	}
	
	std::cout << "[main] Uruchamianie symulacji z " << total_petents << " petentami..." << std::endl;
	start_simulation(total_petents);
	std::cout << "[main] Wszystkie procesy zakończone." << std::endl;
	std::cout << "[main] Koniec działania." << std::endl;
	return 0;
}
