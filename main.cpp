#include <iostream>
#include <string>
#include <sys/resource.h>
#include "config/config.h"

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
	return system("./loader");
}
