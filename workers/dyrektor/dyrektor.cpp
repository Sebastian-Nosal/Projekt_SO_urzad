#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include "dyrektor.h"

// Lista przykładowych PID urzędników (w praktyce można pobierać z pliku lub argumentów)
#define MAX_URZEDNICY 10
pid_t urzednicy[MAX_URZEDNICY];
int liczba_urzednikow = 0;

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
    printf("[dyrektor] Zakończono pracę.\n");
    return 0;
}
