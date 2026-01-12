#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include "dyrektor.h"
#include <sys/types.h>

// Lista przykładowych PID urzędników (w praktyce można pobierać z pliku lub argumentów)

#include <sys/mman.h>
#include <fcntl.h>

pid_t urzednicy[MAX_URZEDNICY];
int liczba_urzednikow = 0;

void check_and_expel_if_exhausted(int* urzednik_exhausted, pid_t* urzednicy, int liczba_urzednikow) {
    int all_exhausted = 1;
    for (int i = 0; i < liczba_urzednikow; ++i) {
        if (!urzednik_exhausted[i]) {
            all_exhausted = 0;
            break;
        }
    }
    if (all_exhausted) {
        printf("[dyrektor] Wszyscy urzędnicy wyczerpani! Wypraszam wszystkich z budynku!\n");
        wyslij_sygnal_do_urzednikow(SIGUSR2);
    }
}

void wyslij_sygnal_do_urzednikow(int sygnal) {
    for (int i = 0; i < liczba_urzednikow; ++i) {
        if (kill(urzednicy[i], sygnal) == 0) {
            printf("[dyrektor] Wysłano sygnał %d do urzędnika PID=%d\n", sygnal, urzednicy[i]);
        } else {
            perror("[dyrektor] Błąd wysyłania sygnału");
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("Użycie: %s <sygnał: 1|2> <PID_URZEDNIKA> [PID_URZEDNIKA ...]\n", argv[0]);
        return 1;
    }
    int sygnal = 0;
    if (strcmp(argv[1], "1") == 0) sygnal = SIGUSR1;
    else if (strcmp(argv[1], "2") == 0) sygnal = SIGUSR2;
    else {
        printf("Nieznany sygnał: %s\n", argv[1]);
        return 1;
    }
    liczba_urzednikow = argc - 2;
    for (int i = 0; i < liczba_urzednikow; ++i) {
        urzednicy[i] = (pid_t)atoi(argv[i + 2]);
    }
    wyslij_sygnal_do_urzednikow(sygnal);

    // Sprawdź wyczerpanie urzędników przez shared memory
    int shm_fd = shm_open(URZEDNIK_EXHAUST_SHM, O_CREAT | O_RDWR, 0666);
    int _ft = ftruncate(shm_fd, sizeof(int) * liczba_urzednikow);
    if (_ft == -1) perror("ftruncate dyrektor");
    int* urzednik_exhausted = (int*)mmap(0, sizeof(int) * liczba_urzednikow, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    // Dyrektor cyklicznie sprawdza status urzędników
    for (int t = 0; t < 60; ++t) {
        check_and_expel_if_exhausted(urzednik_exhausted, urzednicy, liczba_urzednikow);
        sleep(1);
    }
    munmap(urzednik_exhausted, sizeof(int) * liczba_urzednikow);
    close(shm_fd);
    shm_unlink(URZEDNIK_EXHAUST_SHM);
    printf("[dyrektor] Zakończono pracę.\n");
    return 0;
}
