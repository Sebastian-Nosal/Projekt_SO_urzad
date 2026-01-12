#include <iostream>
#include <vector>
#include <cstdlib>
#include "headers/loader.h"
#include "config.h"

int main() {
	int total_petents = PETENT_AMOUNT;
	start_simulation(total_petents);
	std::cout << "Symulacja zakończona." << std::endl;
	return 0;
}
