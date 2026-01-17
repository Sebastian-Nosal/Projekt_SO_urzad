#include "zapisz_logi.h"

#include <fstream>
#include <iostream>

void zapisz_log(const std::string& worker, int worker_id, const std::string& wiadomosc) {
	std::cout << '[' << worker << ':' << worker_id << "] "<< wiadomosc<<std::endl;
	std::cout.flush();

	std::ofstream f(ZAPIS_FILE, std::ios::app);
	if (!f) return;
	if (!worker.empty()) {
		f << '[' << worker << ':' << worker_id << "] ";
	}
	f << wiadomosc<<std::endl;
	f.flush();
}

void zapisz_log(const char* worker, int worker_id, const std::string& wiadomosc) {
	zapisz_log(worker ? std::string(worker) : std::string(), worker_id, wiadomosc);
}
