#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include "dyrektor.h"
#include <sys/types.h>
#include <format>
#include "../../utils/zapisz_logi.h"
#include <sstream>

#include <sys/mman.h>
#include <fcntl.h>
#include "../../config.h"

pid_t urzednicy[MAX_URZEDNICY];
int liczba_urzednikow = 0;

void sprawdz_i_obsluz_wyczerpanych(int* urzednik_exhausted, pid_t* urzednicy, int liczba_urzednikow) {
    int all_exhausted = 1;
    for (int i = 0; i < liczba_urzednikow; ++i) {
        if (!urzednik_exhausted[i]) {
            all_exhausted = 0;
            break;
        }
    }
    if (all_exhausted) {
        {
            std::ostringstream oss;
            oss << "Wszyscy urz\u0119dnicy wyczerpani! Wypraszam wszystkich z budynku!";
            zapisz_log("dyrektor", 0, oss.str());
        }
        wyslij_sygnal_do_urzednikow(SIGUSR2);
    }
}

void wyslij_sygnal_do_urzednikow(int sygnal) {
    for (int i = 0; i < liczba_urzednikow; ++i) {
        if (kill(urzednicy[i], sygnal) == 0) {
            {
                std::ostringstream oss;
                oss << "Wys\u0142ano sygna\u0142 " << sygnal << " do urz\u0119dnika PID=" << urzednicy[i];
                zapisz_log("dyrektor", urzednicy[i], oss.str());
            }
        } else {
            perror("[dyrektor] Błąd wysyłania sygnału");
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        {
            std::ostringstream oss;
            oss << "U\u017cycie: " << argv[0] << " <sygna\u0142: 1|2> <PID_URZEDNIKA> [PID_URZEDNIKA ...]";
            zapisz_log("dyrektor", 0, oss.str());
        }
        return 1;
    }
    int sygnal = 0;
    if (strcmp(argv[1], "1") == 0) sygnal = SIGUSR1;
    else if (strcmp(argv[1], "2") == 0) sygnal = SIGUSR2;
    else {
        {
            std::ostringstream oss;
            oss << "Nieznany sygna\u0142: " << argv[1];
            zapisz_log("dyrektor", 0, oss.str());
        }
        return 1;
    }
    liczba_urzednikow = argc - 2;
    for (int i = 0; i < liczba_urzednikow; ++i) {
        urzednicy[i] = (pid_t)atoi(argv[i + 2]);
    }
    {
        std::ostringstream oss;
        oss << "Wysy\u0142am sygna\u0142 " << sygnal << " do " << liczba_urzednikow << " urz\u0119dnik\u00f3w";
        zapisz_log("dyrektor", 0, oss.str());
    }
    for (int i = 0; i < liczba_urzednikow; ++i) {
        {
            std::ostringstream oss;
            oss << "Urz\u0119dnik #" << i << " PID=" << urzednicy[i];
            zapisz_log("dyrektor", urzednicy[i], oss.str());
        }
    }
    wyslij_sygnal_do_urzednikow(sygnal);

    int shm_fd = shm_open(URZEDNIK_EXHAUST_SHM, O_CREAT | O_RDWR, 0666);
    int _ft = ftruncate(shm_fd, sizeof(int) * liczba_urzednikow);
    if (_ft == -1) perror("ftruncate dyrektor");
    int* urzednik_exhausted = (int*)mmap(0, sizeof(int) * liczba_urzednikow, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    for (int t = 0; t < CZAS_KONIEC; ++t) {
        sprawdz_i_obsluz_wyczerpanych(urzednik_exhausted, urzednicy, liczba_urzednikow);
        sleep(1);
    }
    {
        std::ostringstream oss;
        oss << "CZAS_KONIEC=" << CZAS_KONIEC << " sekund min\u0105\u0142, ko\u0144cz\u0119 symulacj\u0119!";
        zapisz_log("dyrektor", 0, oss.str());
    }
    wyslij_sygnal_do_urzednikow(SIGUSR2);
    munmap(urzednik_exhausted, sizeof(int) * liczba_urzednikow);
    close(shm_fd);
    shm_unlink(URZEDNIK_EXHAUST_SHM);
    {
        std::ostringstream oss;
        oss << "Zako\u0144czono prac\u0119.";
        zapisz_log("dyrektor", 0, oss.str());
    }
    return 0;
}
