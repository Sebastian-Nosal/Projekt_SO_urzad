#include <iostream>
#include <string>
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
