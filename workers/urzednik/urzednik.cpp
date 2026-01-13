
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "../../config.h"
#include "../biletomat/biletomat.h"
#include "../dyrektor/dyrektor.h"

#define RAPORT_FILE "raport_urzednik.txt"
volatile sig_atomic_t zamkniecie_urzedu = 0;
volatile sig_atomic_t dyrektor_koniec = 0;

void sigusr1_handler(int sig) { dyrektor_koniec = 1; }
void sigusr2_handler(int sig) { zamkniecie_urzedu = 1; }


int get_limit(wydzial_t typ) {
    switch(typ) {
        case WYDZIAL_SC: return PETENT_LIMIT_SC;
        case WYDZIAL_KM: return PETENT_LIMIT_KM;
        case WYDZIAL_ML: return PETENT_LIMIT_ML;
        case WYDZIAL_PD: return PETENT_LIMIT_PD;
        case WYDZIAL_SA: return PETENT_LIMIT_SA;
        default: return 0;
    }
}

wydzial_t random_sa_target() {
    double r = (double)rand() / RAND_MAX;
    if (r < PROB_SA_TO_SC) return WYDZIAL_SC;
    else if (r < PROB_SA_TO_SC + PROB_SA_TO_KM) return WYDZIAL_KM;
    else if (r < PROB_SA_TO_SC + PROB_SA_TO_KM + PROB_SA_TO_ML) return WYDZIAL_ML;
    else return WYDZIAL_PD;
}

int main(int argc, char* argv[]) {

    if (argc < 2) {
        fprintf(stderr, "Użycie: %s <typ_urzedu (int) z config.h>\n", argv[0]);
        return 1;
    }
    wydzial_t typ = (wydzial_t)atoi(argv[1]);
    srand(time(NULL) ^ getpid());
    int licznik = get_limit(typ);

    
    int shm_fd_exhaust = -1;
    // Spróbuj otworzyć istniejący SHM, poczekaj trochę jeśli jeszcze nie utworzony przez dyrektora
    for (int attempt = 0; attempt < 10; ++attempt) {
        shm_fd_exhaust = shm_open(URZEDNIK_EXHAUST_SHM, O_RDWR, 0666);
        if (shm_fd_exhaust >= 0) break;
        sleep(1);
    }
    if (shm_fd_exhaust < 0) {
        // fallback: utwórz go samodzielnie (tylko jeśli dyrektor nie utworzył w rozsądnym czasie)
        shm_fd_exhaust = shm_open(URZEDNIK_EXHAUST_SHM, O_CREAT | O_RDWR, 0666);
        if (shm_fd_exhaust < 0) { perror("shm_open URZEDNIK_EXHAUST_SHM"); return 1; }
        int _ft = ftruncate(shm_fd_exhaust, sizeof(int) * WYDZIAL_COUNT);
        if (_ft == -1) perror("ftruncate URZEDNIK_EXHAUST_SHM");
    }
    int* urzednik_exhausted = (int*)mmap(0, sizeof(int) * WYDZIAL_COUNT, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_exhaust, 0);
    if (urzednik_exhausted == MAP_FAILED) { perror("mmap urzednik_exhausted"); return 1; }
    urzednik_exhausted[typ] = 0;
    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    size_t shm_size = WYDZIAL_COUNT * (sizeof(struct ticket) * MAX_TICKETS + sizeof(int));
    void* shm_ptr = mmap(0, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    char* base = (char*)shm_ptr;
    struct ticket* ticket_list[WYDZIAL_COUNT];
    int* ticket_count = (int*)(base + WYDZIAL_COUNT * sizeof(struct ticket) * MAX_TICKETS);
    for (int i = 0; i < WYDZIAL_COUNT; ++i) {
        ticket_list[i] = (struct ticket*)(base + i * sizeof(struct ticket) * MAX_TICKETS);
    }
    sem_t* sem = sem_open(SEM_NAME, 0);

    signal(SIGUSR1, sigusr1_handler); 
    signal(SIGUSR2, sigusr2_handler);

    FILE* raport = fopen(RAPORT_FILE, "a");
    if (!raport) raport = stdout;

    printf("[Urzędnik PID=%d, typ=%d] Start, limit: %d\n", getpid(), typ, licznik);
    while (1) {
        if (licznik <= 0) {
            urzednik_exhausted[typ] = 1;
            printf("[Urzędnik PID=%d, typ=%d] Wyczepany (limit wyczerpany)\n", getpid(), typ);
        }
        if (dyrektor_koniec) {
            sem_wait(sem);
            int n = ticket_count[typ];
            if (n > 0 && licznik > 0) {
                struct ticket t = ticket_list[typ][0];
                for (int i = 1; i < n; ++i) ticket_list[typ][i-1] = ticket_list[typ][i];
                ticket_count[typ]--;
                fprintf(raport, "[Urzędnik %d] (DYREKTOR KONIEC) Obsługuje petenta PID=%d, idx=%d\n", typ, t.PID, t.index);
                printf("[Urzędnik PID=%d, typ=%d] (DYREKTOR KONIEC) Obsługuje petenta PID=%d, idx=%d\n", getpid(), typ, t.PID, t.index);
                licznik--;
                printf("[Urzędnik] PID=%d obsłużył petenta, pozostało jeszcze %d możliwych petentów do obsłużenia.\n", getpid(), licznik);
            }
            sem_post(sem);
            break;
        }
        if (zamkniecie_urzedu) {
            sem_wait(sem);
            int n = ticket_count[typ];
            for (int i = 0; i < n; ++i) {
                struct ticket t = ticket_list[typ][i];
                fprintf(raport, "[Urzędnik %d] (ZAMKNIĘCIE) NIEPRZYJĘTY: PID=%d, idx=%d\n", typ, t.PID, t.index);
                printf("[Urzędnik PID=%d, typ=%d] (ZAMKNIĘCIE) NIEPRZYJĘTY: PID=%d, idx=%d\n", getpid(), typ, t.PID, t.index);
            }
            ticket_count[typ] = 0;
            sem_post(sem);
            break;
        }
        sem_wait(sem);
        int n = ticket_count[typ];
        if (n == 0) {
            sem_post(sem);
            sleep(1);
            continue;
        }
        struct ticket t = ticket_list[typ][0];
        for (int i = 1; i < n; ++i) ticket_list[typ][i-1] = ticket_list[typ][i];
        ticket_count[typ]--;
        sem_post(sem);
        fprintf(raport, "[Urzędnik %d] Obsługuje petenta PID=%d, idx=%d\n", typ, t.PID, t.index);
        printf("[Urzędnik PID=%d, typ=%d] Obsługuje petenta PID=%d, idx=%d\n", getpid(), typ, t.PID, t.index);
            printf("[Urzędnik] PID=%d obsłużył petenta, pozostało jeszcze %d możliwych petentów do obsłużenia.\n", getpid(), licznik);
        if (typ == WYDZIAL_SA) {
            double r = (double)rand() / RAND_MAX;
            if (r < PROB_SA_SOLVE) {
                fprintf(raport, "[Urzędnik SA] Sprawa załatwiona dla PID=%d\n", t.PID);
                printf("[Urzędnik SA PID=%d] Sprawa załatwiona dla PID=%d\n", getpid(), t.PID);
                licznik--;
                printf("[Urzędnik] PID=%d obsłużył petenta, pozostało jeszcze %d możliwych petentów do obsłużenia.\n", getpid(), licznik);
            } else {
                wydzial_t cel = random_sa_target();
                int cel_limit = get_limit(cel);
                sem_wait(sem);
                int cel_n = ticket_count[cel];
                sem_post(sem);
                if (cel_n < cel_limit) {
                    fprintf(raport, "[Urzędnik SA] Przekierowuje PID=%d do wydziału %d\n", t.PID, cel);
                    printf("[Urzędnik SA PID=%d] Przekierowuje PID=%d do wydziału %d\n", getpid(), t.PID, cel);
                    sem_wait(sem);
                    int idx2 = ticket_count[cel]++;
                    ticket_list[cel][idx2] = t;
                    ticket_list[cel][idx2].typ = cel;
                    sem_post(sem);
                    printf("[Urzędnik] PID=%d przekierował petenta PID=%d do wydziału %d (nowa liczba biletów: %d)\n", getpid(), t.PID, cel, ticket_count[cel]);
                } else {
                    fprintf(raport, "[Urzędnik SA] BRAK MIEJSC w wydziale %d dla PID=%d, raport NIEPRZYJĘTY\n", cel, t.PID);
                    printf("[Urzędnik SA PID=%d] BRAK MIEJSC w wydziale %d dla PID=%d\n", getpid(), t.PID);
                }
            }
        } else {
            double r = (double)rand() / RAND_MAX;
            if (r < 0.1) {
                fprintf(raport, "[Urzędnik %d] Skierowano PID=%d do kasy\n", typ, t.PID);
                printf("[Urzędnik PID=%d, typ=%d] Skierowano PID=%d do kasy\n", getpid(), typ, t.PID);
                    printf("[Urzędnik] PID=%d obsłużył petenta, pozostało jeszcze %d możliwych petentów do obsłużenia.\n", getpid(), licznik);
            } else {
                fprintf(raport, "[Urzędnik %d] Sprawa załatwiona dla PID=%d\n", typ, t.PID);
                printf("[Urzędnik PID=%d, typ=%d] Sprawa załatwiona dla PID=%d\n", getpid(), typ, t.PID);
                licznik--;
                    printf("[Urzędnik] PID=%d obsłużył petenta, pozostało jeszcze %d możliwych petentów do obsłużenia.\n", getpid(), licznik);
            }
        }
        sleep(1);
    }
    fclose(raport);
    munmap(shm_ptr, shm_size);
    close(shm_fd);
    sem_close(sem);
    return 0;
}
