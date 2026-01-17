#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include "petent.h"
#include "../biletomat/biletomat.h"
#include <format>
#include <sstream>
#include "../../utils/zapisz_logi.h"
#include <cstdlib>
#include <ctime>
#include <thread>

volatile sig_atomic_t zamkniecie_urzedu = 0;

// SIGUSR2: dyrektor zamyka urząd
void sigusr2_handler(int sig) {
	pid_t pid = getpid();
	{
		std::ostringstream oss;
		oss << "Jestem sfrustrowany";
		zapisz_log("Petent", pid, oss.str());
	}
	_exit(0);
}

// SIGUSR1: petent jest obsługiwany przez urzędnika - sprawa załatwiona
void sigusr1_handler(int sig) {
	pid_t pid = getpid();
	{
		std::ostringstream oss;
		oss << "Sprawa Zalatowiona";
		zapisz_log("Petent", pid, oss.str());
	}
	_exit(0);
}

// SIGTERM: petent zostaje odprawiony z kwitkiem przez urzędnika (exhausted)
void sigterm_handler(int sig) {
	pid_t pid = getpid();
	{
		std::ostringstream oss;
		oss << "Odprawiony z kwitkiem";
		zapisz_log("Petent", pid, oss.str());
	}
	_exit(0);
}

void child_thread_function(pid_t pid) {
    {
        std::ostringstream oss;
        oss << "[Child] Proces PID=" << pid << " ma dziecko.";
        zapisz_log("Petent", pid, oss.str());
    }
}

void petent_start(PetentData* petent) {
	pid_t pid = getpid();
	int fd = open(PIPE_NAME, O_WRONLY);
	if (fd < 0) {
		perror("[petent] Nie można otworzyć pipe do biletomatu");
		exit(1);
	}
	// Utwórz pakiet: komenda (16 bajtów) + struktura
	struct {
		char cmd[16];
		pid_t pid;
		int prio;
		wydzial_t typ;
	} packet;
	
	if (petent->isVIP)
		strcpy(packet.cmd, "ASSIGN_TICKET_TO");
	else
		strcpy(packet.cmd, "ASSIGN_TICKET");
	
	packet.pid = pid;
	packet.prio = petent->priorytet;
	packet.typ = petent->typ;
	
	// Log taking the ticket
	{
		std::ostringstream oss;
		oss << "Pobiera bilet dla wydzialu " << petent->typ << ", priorytet=" << petent->priorytet << (petent->isVIP ? " (VIP)" : "");
		if (petent->hasChild) {
			oss << " [z dzieckiem]";
		}
		zapisz_log("Petent", pid, oss.str());
	}
	if (write(fd, &packet, sizeof(packet)) == -1) {
		perror("[petent] Nie udało się zapisać do pipe");
	}
	close(fd);
	{
		std::ostringstream oss;
		oss << "Zgloszy\u0142 si\u0119 do wydzia\u0142u " << petent->typ << ", priorytet=" << petent->priorytet << (petent->isVIP ? " (VIP)" : "");
		if (petent->hasChild) {
			oss << " [z dzieckiem]";
		}
		zapisz_log("Petent", pid, oss.str());
	}
}

int main(int argc, char* argv[]) {
	if (argc < 3) {
		fprintf(stderr, "Użycie: %s <typ_urzedu (int)> <priorytet> [vip=1] [isInside=0|1]\n", argv[0]);
		return 1;
	}
	PetentData petent;
	petent.typ = (wydzial_t)atoi(argv[1]);
	petent.priorytet = atoi(argv[2]);
	petent.isVIP = (argc > 3) ? atoi(argv[3]) : 0;
	petent.isInside = (argc > 4) ? atoi(argv[4]) : 0;
	signal(SIGUSR2, sigusr2_handler);
	signal(SIGTERM, sigterm_handler);
	signal(SIGUSR1, sigusr1_handler);
	srand(time(NULL) ^ getpid()); // Seed the random number generator
	petent.hasChild = (rand() / (double)RAND_MAX) < PROB_CHILD ? 1 : 0; // Assign hasChild based on PROB_CHILD
	std::thread child_thread;
	pid_t pid = getpid(); // Declare and initialize pid in main
	if (petent.hasChild) {
		child_thread = std::thread(child_thread_function, pid); // Use the declared pid
	}
	petent_start(&petent);
	// Oczekiwanie na obsługę lub zamknięcie urzędu - użyj pause() aby czekać na sygnał
	while (!zamkniecie_urzedu) {
		pause();  // Czekaj na sygnał
	}
	if (petent.hasChild && child_thread.joinable()) {
        child_thread.join();
    }
    {
        std::ostringstream oss;
        oss << "PID=" << getpid() << " opuszcza urz\u0105d (zamkni\u0119cie)";
        if (petent.hasChild) {
            oss << " [z dzieckiem]";
        }
        zapisz_log("petent", pid, oss.str());
    }
	return 0;
}
