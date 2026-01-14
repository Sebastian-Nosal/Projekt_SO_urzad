
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
    for (int attempt = 0; attempt < 10; ++attempt) {
        shm_fd_exhaust = shm_open(URZEDNIK_EXHAUST_SHM, O_RDWR, 0666);
        if (shm_fd_exhaust >= 0) break;
        sleep(1);
    }
    if (shm_fd_exhaust < 0) {
        shm_fd_exhaust = shm_open(URZEDNIK_EXHAUST_SHM, O_CREAT | O_RDWR, 0666);
        if (shm_fd_exhaust < 0) { perror("shm_open URZEDNIK_EXHAUST_SHM"); return 1; }
        int _ft = ftruncate(shm_fd_exhaust, sizeof(int) * WYDZIAL_COUNT);
        if (_ft == -1) perror("ftruncate URZEDNIK_EXHAUST_SHM");
    }
    int* urzednik_exhausted = (int*)mmap(0, sizeof(int) * WYDZIAL_COUNT, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_exhaust, 0);
    if (urzednik_exhausted == MAP_FAILED) { perror("mmap urzednik_exhausted"); return 1; }
    urzednik_exhausted[typ] = 0;
    int shm_fd = -1;
    for (int attempt = 0; attempt < 10; ++attempt) {
        shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
        if (shm_fd >= 0) break;
        sleep(1);
    }
    if (shm_fd < 0) { perror("shm_open SHM_NAME"); return 1; }
    size_t shm_size = WYDZIAL_COUNT * sizeof(struct ticket) * MAX_TICKETS + WYDZIAL_COUNT * sizeof(int);
    void* shm_ptr = mmap(0, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    char* base = (char*)shm_ptr;
    struct ticket* ticket_list[WYDZIAL_COUNT];
    int* ticket_count = (int*)(base + WYDZIAL_COUNT * sizeof(struct ticket) * MAX_TICKETS);
    for (int i = 0; i < WYDZIAL_COUNT; ++i) {
        ticket_list[i] = (struct ticket*)(base + i * sizeof(struct ticket) * MAX_TICKETS);
    }
    sem_t* sem = SEM_FAILED;
    for (int attempt = 0; attempt < 100000; ++attempt) {
        sem = sem_open(SEM_NAME, 0);
        if (sem != SEM_FAILED) break;
        sleep(1);
    }
    if (sem == SEM_FAILED) { perror("sem_open SEM_NAME"); return 1; }

    signal(SIGUSR1, sigusr1_handler); 
    signal(SIGUSR2, sigusr2_handler);

    FILE* raport = fopen(RAPORT_FILE, "a");
    if (!raport) raport = stdout;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (raport != stdout) setvbuf(raport, NULL, _IONBF, 0);

    int obsluzone = 0;  // Licznik obsłużonych osób
    printf("[Urzędnik PID=%d, typ=%d]: Start wydziału %d, limit: %d\n", getpid(), typ, licznik);
    fprintf(raport, "[Urzędnik PID=%d, typ=%d]: Start wydziału %d, limit: %d\n", getpid(), typ, licznik);
    fflush(raport);
    while (1) {
        if (licznik <= 0) {
            if (!urzednik_exhausted[typ]) {
                urzednik_exhausted[typ] = 1;
                printf("[Urzędnik PID=%d, typ=%d]: Wyczerpany (limit wyczerpany)\n", getpid(), typ);
                sem_wait(sem);
                int pending = ticket_count[typ];
                for (int i = 0; i < pending; ++i) {
                    struct ticket tt = ticket_list[typ][i];
                    fprintf(raport, "[Urzędnik %d] (EXHAUST) ODPRAWIONY: PID=%d, idx=%d\n", typ, tt.PID, tt.index);
                    printf("[Urzędnik PID=%d, typ=%d]: (EXHAUST) ODPRAWIONY: PID=%d, idx=%d\n", getpid(), typ, tt.PID, tt.index);
                    kill(tt.PID, SIGTERM);
                }
                ticket_count[typ] = 0;
                sem_post(sem);
                printf("[Urzędnik PID=%d, typ=%d]: Koniec pracy (limit wyczerpany)\n", getpid(), typ);
                break;
            }
        }
        if (dyrektor_koniec) {
            if (licznik <= 0) {
                printf("[Urzędnik PID=%d, typ=%d]: Dyrektor nakazał koniec\n", getpid(), typ);
                break;
            }
            sem_wait(sem);
            int n = ticket_count[typ];
            if (n > 0) {
                struct ticket t = ticket_list[typ][0];
                for (int i = 1; i < n; ++i) ticket_list[typ][i-1] = ticket_list[typ][i];
                ticket_count[typ]--;
                // Najpierw loguj, flushuj, potem powiadom petenta, że jest obsługiwany
                fprintf(raport, "[Urzędnik %d] (DYREKTOR KONIEC) Obsługuje petenta PID=%d, idx=%d\n", typ, t.PID, t.index);
                printf("[Urzędnik PID=%d, typ=%d] (DYREKTOR KONIEC) Obsługuje petenta PID=%d, idx=%d\n", getpid(), typ, t.PID, t.index);
                fflush(raport);
                fflush(stdout);
                kill(t.PID, SIGUSR1);
                licznik--;
                printf("[Urzędnik] PID=%d obsłużył petenta, pozostało jeszcze %d możliwych petentów do obsłużenia.\n", getpid(), licznik);
            }
            sem_post(sem);
            continue;
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
        // Najpierw loguj, flushuj, potem obsługiwanie
        fprintf(raport, "[Urzędnik %d] Obsługuje petenta PID=%d, idx=%d\n", typ, t.PID, t.index);
        printf("[Urzędnik PID=%d, typ=%d]: Obsługuje petenta PID=%d, idx=%d\n", getpid(), typ, t.PID, t.index);
        fflush(raport);
        fflush(stdout);
        
        if (typ == WYDZIAL_SA) {
            double r = (double)rand() / RAND_MAX;
            if (r < PROB_SA_SOLVE) {
                fprintf(raport, "[Urzędnik SA] Sprawa załatwiona dla PID=%d\n", t.PID);
                printf("[Urzędnik PID=%d, typ=%d]: Sprawa załatwiona dla PID=%d\n", getpid(), typ, t.PID);
                fflush(stdout);
                kill(t.PID, SIGUSR1);  // Wysyłaj SIGUSR1 tylko gdy sprawa załatwiona
                licznik--;
                obsluzone++;
                printf("[Urzędnik] PID=%d obsłużył petenta, pozostało jeszcze %d możliwych petentów do obsłużenia.\n", getpid(), licznik);
                fprintf(raport, "[Raport] Obsłużono: %d, Można obsłużyć jeszcze: %d\n", obsluzone, licznik);
            } else {
                wydzial_t cel = random_sa_target();
                int cel_limit = get_limit(cel);
                
                // Sprawdź czy docelowy wydział nie jest już wyczerpany
                if (urzednik_exhausted[cel]) {
                    fprintf(raport, "[Urzędnik SA] Wydział %d jest już wyczerpany - ODPRAWIENIE PID=%d\n", cel, t.PID);
                    printf("[Urzędnik PID=%d, typ=%d]: Wydział %d jest wyczerpany - odprawianie PID=%d\n", getpid(), typ, cel, t.PID);
                    obsluzone++;
                    kill(t.PID, SIGTERM);
                    fprintf(raport, "[Raport] Obsłużono: %d, Można obsłużyć jeszcze: %d\n", obsluzone, licznik);
                } else {
                    sem_wait(sem);
                    int cel_n = ticket_count[cel];
                    sem_post(sem);
                    if (cel_n < cel_limit) {
                        fprintf(raport, "[Urzędnik SA] Przekierowuje PID=%d do wydziału %d\n", t.PID, cel);
                        printf("[Urzędnik PID=%d, typ=%d]: Przekierowuje PID=%d do wydziału %d\n", getpid(), typ, t.PID, cel);
                        sem_wait(sem);
                        int idx2 = ticket_count[cel]++;
                        ticket_list[cel][idx2] = t;
                        ticket_list[cel][idx2].typ = cel;
                        sem_post(sem);
                        printf("[Urzędnik] PID=%d przekierował petenta PID=%d do wydziału %d (nowa liczba biletów: %d)\n", getpid(), t.PID, cel, ticket_count[cel]);
                    } else {
                        fprintf(raport, "[Urzędnik SA] BRAK MIEJSC w wydziale %d dla PID=%d, raport NIEPRZYJĘTY\n", cel, t.PID);
                        printf("[Urzędnik PID=%d, typ=%d]: BRAK MIEJSC w wydziale %d dla PID=%d\n", getpid(), typ, cel, t.PID);
                        // Gdy brak miejsc w wydziale, odpraw petenta
                        obsluzone++;
                        kill(t.PID, SIGTERM);
                        fprintf(raport, "[Raport] Obsłużono: %d, Można obsłużyć jeszcze: %d\n", obsluzone, licznik);
                    }
                }
            }
        } else {
            double r = (double)rand() / RAND_MAX;
            if (r < 0.1) {
                fprintf(raport, "[Urzędnik %d] Skierowano PID=%d do kasy\n", typ, t.PID);
                printf("[Urzędnik PID=%d, typ=%d]: Skierowano PID=%d do kasy\n", getpid(), typ, t.PID);
                obsluzone++;
                fprintf(raport, "[Raport] Obsłużono: %d, Można obsłużyć jeszcze: %d\n", obsluzone, licznik);
            } else {
                fprintf(raport, "[Urzędnik %d] Sprawa załatwiona dla PID=%d\n", typ, t.PID);
                printf("[Urzędnik PID=%d, typ=%d]: Sprawa załatwiona dla PID=%d\n", getpid(), typ, t.PID);
                fflush(stdout);
                kill(t.PID, SIGUSR1);  // Wysyłaj SIGUSR1 tylko gdy sprawa załatwiona
                licznik--;
                obsluzone++;
                printf("[Urzędnik] PID=%d obsłużył petenta, pozostało jeszcze %d możliwych petentów do obsłużenia.\n", getpid(), licznik);
                fprintf(raport, "[Raport] Obsłużono: %d, Można obsłużyć jeszcze: %d\n", obsluzone, licznik);
            }
        }
        fflush(raport);
        sleep(1);
    }
    fclose(raport);
    munmap(shm_ptr, shm_size);
    close(shm_fd);
    sem_close(sem);
    return 0;
}
