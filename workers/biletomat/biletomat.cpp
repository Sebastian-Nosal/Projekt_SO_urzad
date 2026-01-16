#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <semaphore.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include "biletomat.h"

extern volatile sig_atomic_t running;
extern volatile sig_atomic_t zamkniecie_urzedu;

// Wskaźnik do tablicy liczników biletek w pamięci współdzielonej
int* shm_ticket_count = NULL;

static int g_building_capacity_announced = 0;

static int count_active_in_tickets() {
    int total = 0;
    for (int wydzial = 0; wydzial < WYDZIAL_COUNT; ++wydzial) {
        int n = (shm_ticket_count) ? shm_ticket_count[wydzial] : ticket_count[wydzial];
        for (int i = 0; i < n; ++i) {
            if (ticket_list[wydzial][i].PID > 0) total++;
        }
    }
    return total;
}

static void remove_pid_from_tickets_locked(pid_t pid) {
    for (int wydzial = 0; wydzial < WYDZIAL_COUNT; ++wydzial) {
        int* counts = (shm_ticket_count) ? shm_ticket_count : ticket_count;
        int n = counts[wydzial];
        struct ticket* list = ticket_list[wydzial];
        for (int i = 0; i < n; ++i) {
            if (list[i].PID != pid) continue;

            for (int j = i + 1; j < n; ++j) {
                list[j - 1] = list[j];
            }
            // Wyzeruj ostatni slot po przesunięciu.
            if (n - 1 >= 0) {
                memset(&list[n - 1], 0, sizeof(struct ticket));
                list[n - 1].typ = (wydzial_t)wydzial;
            }
            counts[wydzial] = n - 1;
            if (shm_ticket_count) ticket_count[wydzial] = shm_ticket_count[wydzial];
            return;
        }
    }
}

void sig_handler(int sig) {
    if (sig == SIGUSR1) {
        printf("[Biletomat -> PID=%d]: Otrzymano SIGUSR1 - konczę pracę\n", getpid());
        running = 0;
    } else {
        running = 0;
    }
    if (sig == SIGUSR2) zamkniecie_urzedu = 1;
}

struct ticket* ticket_list[WYDZIAL_COUNT] = {NULL};
int ticket_count[WYDZIAL_COUNT] = {0};
struct waiting_petent* waiting_list = NULL;
int* waiting_count_ptr = NULL;  // Wskaźnik do SHM
int* waiting_count_per_wydzial = NULL;  // Wskaźnik do SHM - liczba oczekujących per wydział
int* wydzial_closed = NULL;  // Wskaźnik do SHM - tablica flag czy wydział jest zamknięty
int* people_inside_ptr = NULL;  // Wskaźnik do SHM
void* shm_ptr = NULL;
int shm_fd = -1;
sem_t* sem = NULL;
sem_t* sem_ticket_assigned = NULL;
volatile sig_atomic_t running = 1;
volatile sig_atomic_t zamkniecie_urzedu = 0;
int liczba_automatow = 1;

void sigusr2_handler(int sig) { zamkniecie_urzedu = 1; running = 0; }

void cleanup() {
    for (int i = 0; i < WYDZIAL_COUNT; ++i) {
        if (ticket_list[i]) munmap(ticket_list[i], sizeof(struct ticket) * MAX_TICKETS);
    }
    if (shm_ptr) munmap(shm_ptr, WYDZIAL_COUNT * (sizeof(struct ticket) * MAX_TICKETS + sizeof(int)));
    if (sem) sem_close(sem);
    if (sem_ticket_assigned) sem_close(sem_ticket_assigned);
    sem_unlink(SEM_NAME);
    sem_unlink(SEM_TICKET_ASSIGNED);
    unlink(PIPE_NAME);
    shm_unlink(SHM_NAME);
    printf("[Biletomat -> PID=%d]: Zasoby posprzątane\n", getpid());
}

void sort_queue(wydzial_t typ) {
    int n = ticket_count[typ];
    struct ticket* list = ticket_list[typ];
    for (int i = 0; i < n-1; ++i) {
        for (int j = 0; j < n-i-1; ++j) {
            if (list[j].priorytet < list[j+1].priorytet ||
                (list[j].priorytet == list[j+1].priorytet && list[j].index > list[j+1].index)) {
                struct ticket tmp = list[j];
                list[j] = list[j+1];
                list[j+1] = tmp;
            }
        }
    }
}

void assign_ticket_to(pid_t pid, int prio, wydzial_t typ, sem_t* sem, sem_t* sem_ticket_sig = nullptr) {
    sem_wait(sem);

    // Jest miejsce - wyślij bilet
    int idx = 0;
    if (shm_ticket_count) {
        idx = shm_ticket_count[typ]++;
        ticket_count[typ] = shm_ticket_count[typ];
    } else {
        idx = ticket_count[typ]++;
    }
    ticket_list[typ][idx].index = idx;
    ticket_list[typ][idx].PID = pid;
    ticket_list[typ][idx].priorytet = prio;
    ticket_list[typ][idx].typ = typ;
    if (shm_ticket_count) shm_ticket_count[typ] = ticket_count[typ];
    int queue_size = (shm_ticket_count) ? shm_ticket_count[typ] : ticket_count[typ];

    int active = count_active_in_tickets();
    if (!g_building_capacity_announced && active > BUILDING_CAPACITY) {
        printf("[Biletomat -> PID=%d]: Przekroczono pojemność budynku (%d). Pozostali czekają na zewnątrz.\n", getpid(), BUILDING_CAPACITY);
        g_building_capacity_announced = 1;
    }

    printf("[biletomat] Przydzielono ticket (no-sort) id=%d dla PID=%d, wydzial=%d, priorytet=%d, pozycja w kolejce: %d\n", idx, pid, typ, prio, queue_size);
    sem_post(sem);
    sem_t* sig = (sem_ticket_sig) ? sem_ticket_sig : sem_ticket_assigned;
    if (sig && sig != SEM_FAILED) sem_post(sig);  // Sygnalizuj że bilet przydzielony
}

void assign_ticket(pid_t pid, int prio, wydzial_t typ, sem_t* sem, sem_t* sem_ticket_sig = nullptr) {
    sem_wait(sem);

    // Jest miejsce - wyślij bilet
    int idx = 0;
    if (shm_ticket_count) {
        idx = shm_ticket_count[typ]++;
        ticket_count[typ] = shm_ticket_count[typ];
    } else {
        idx = ticket_count[typ]++;
    }
    ticket_list[typ][idx].index = idx;
    ticket_list[typ][idx].PID = pid;
    ticket_list[typ][idx].priorytet = prio;
    ticket_list[typ][idx].typ = typ;
    if (shm_ticket_count) shm_ticket_count[typ] = ticket_count[typ];
    sort_queue(typ);
    int queue_size = (shm_ticket_count) ? shm_ticket_count[typ] : ticket_count[typ];

    int active = count_active_in_tickets();
    if (!g_building_capacity_announced && active > BUILDING_CAPACITY) {
        printf("[Biletomat -> PID=%d]: Przekroczono pojemność budynku (%d). Pozostali czekają na zewnątrz.\n", getpid(), BUILDING_CAPACITY);
        g_building_capacity_announced = 1;
    }

    printf("[biletomat] Przydzielono ticket id=%d dla PID=%d, wydzial=%d, priorytet=%d (posortowano), pozycja w kolejce: %d\n", idx, pid, typ, prio, queue_size);
    sem_post(sem);
    sem_t* sig = (sem_ticket_sig) ? sem_ticket_sig : sem_ticket_assigned;
    if (sig && sig != SEM_FAILED) sem_post(sig);  // Sygnalizuj że bilet przydzielony
}

int main(int argc, char* argv[]) {
    int N = PETENT_AMOUNT;  // Domyślna liczba petentów
    if (argc > 1) {
        N = atoi(argv[1]);
    }
    printf("[Biletomat -> PID=%d]: Start biletomatu dla %d petentów\n", getpid(), N);
    
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGUSR1, sig_handler);
    signal(SIGUSR2, sigusr2_handler);
    mkfifo(PIPE_NAME, 0666);
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    // SHM zawiera: tickety + liczniki biletów + waiting queue + liczniki waiting per wydział + flagi zamkniętych wydziałów + people_inside + waiting_count
    size_t shm_size = WYDZIAL_COUNT * sizeof(struct ticket) * MAX_TICKETS +  // Tickety
                      WYDZIAL_COUNT * sizeof(int) +                          // Liczniki biletów
                      sizeof(struct waiting_petent) * MAX_WAITING +           // Oczekujący
                      WYDZIAL_COUNT * sizeof(int) +                          // Liczniki waiting per wydział
                      WYDZIAL_COUNT * sizeof(int) +                          // Flagi zamkniętych wydziałów
                      sizeof(int) * 2;                                        // people_inside + waiting_count
    int _ft = ftruncate(shm_fd, shm_size);
    if (_ft == -1) perror("ftruncate biletomat");
    shm_ptr = mmap(0, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    char* base = (char*)shm_ptr;
    for (int i = 0; i < WYDZIAL_COUNT; ++i) {
        ticket_list[i] = (struct ticket*)(base + i * sizeof(struct ticket) * MAX_TICKETS);
    }
    shm_ticket_count = (int*)(base + WYDZIAL_COUNT * sizeof(struct ticket) * MAX_TICKETS);
    for (int i = 0; i < WYDZIAL_COUNT; ++i) {
        ticket_count[i] = shm_ticket_count[i] = 0;
    }
    // Wskaźniki do waiting queue
    char* waiting_base = base + WYDZIAL_COUNT * sizeof(struct ticket) * MAX_TICKETS + WYDZIAL_COUNT * sizeof(int);
    waiting_list = (struct waiting_petent*)waiting_base;
    
    // Liczniki waiting per wydział
    waiting_count_per_wydzial = (int*)(waiting_base + sizeof(struct waiting_petent) * MAX_WAITING);
    for (int i = 0; i < WYDZIAL_COUNT; ++i) {
        waiting_count_per_wydzial[i] = 0;
    }
    
    // Flagi zamkniętych wydziałów
    wydzial_closed = (int*)(waiting_base + sizeof(struct waiting_petent) * MAX_WAITING + WYDZIAL_COUNT * sizeof(int));
    for (int i = 0; i < WYDZIAL_COUNT; ++i) {
        wydzial_closed[i] = 0;  // 0 = otwarty, 1 = zamknięty
    }
    
    // Pozostałe liczniki
    int* counters = (int*)(waiting_base + sizeof(struct waiting_petent) * MAX_WAITING + WYDZIAL_COUNT * sizeof(int) + WYDZIAL_COUNT * sizeof(int));
    people_inside_ptr = counters;
    waiting_count_ptr = counters + 1;
    *people_inside_ptr = 0;  // Liczba osób wewnątrz
    *waiting_count_ptr = 0;  // Liczba oczekujących
    
    sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    sem_ticket_assigned = sem_open(SEM_TICKET_ASSIGNED, O_CREAT, 0666, 0);

    printf("[Biletomat -> PID=%d]: Oczekiwanie na żądania: %s\n", getpid(), PIPE_NAME);
    pid_t biletomat_clones[3] = {0};
    int active_clones = 1;

    auto spawn_clone = [](int idx) -> pid_t {
        pid_t pid = fork();
        if (pid == 0) {
            printf("[Biletomat-klon -> PID=%d]: Uruchomiony, czekam na żądania\n", getpid());
            printf("[Biletomat-klon -> PID=%d]: Otwieranie FIFO: %s\n", getpid(), PIPE_NAME);
            fflush(stdout);
            
            // Otwórz semafor dla dostępu do SHM
            sem_t* sem_clone = sem_open(SEM_NAME, 0);
            // Otwórz FIFO raz na początku i trzymaj otwarte (O_RDWR usuwa problem EOF gdy brak pisarzy)
            // Używamy trybu blokującego, żeby nie robić busy-loop gdy nie ma danych.
            int fd = open(PIPE_NAME, O_RDWR);
            if (fd < 0) {
                printf("[Biletomat-klon -> PID=%d]: Blad otwarcia pipe: %s\n", getpid(), PIPE_NAME);
                fflush(stdout);
                exit(1);
            }
            printf("[Biletomat-klon -> PID=%d]: FIFO otwarte, czekam na żądania\n", getpid());
            fflush(stdout);
            
            while (running) {
                // Czytaj pakiet: komenda (16 bajtów) + dane
                struct {
                    char cmd[16];
                    pid_t pid;
                    int prio;
                    wydzial_t typ;
                } packet;
                
                int nread = (int)read(fd, &packet, sizeof(packet));
                if (nread < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    perror("[Biletomat-klon] read");
                    sleep(1);
                    continue;
                }
                
                // Przy O_RDWR nie powinno być klasycznego EOF gdy brak pisarzy, ale zostawiamy safeguard.
                if (nread == 0) {
                    sleep(1);
                    continue;
                }
                
                if (nread != (int)sizeof(packet)) { 
                    //printf("[Biletomat-klon -> PID=%d]: Blad: oczekiwano %lu bajtów, otrzymano %d\n", getpid(), sizeof(packet), nread);
                    fflush(stdout);
                    continue; 
                }
                
                //printf("[biletomat-klon %d] Przeczytano %d bajtów, cmd='%.16s', PID=%d, prio=%d, typ=%d\n", 
                //       idx, nread, packet.cmd, packet.pid, packet.prio, packet.typ);
                fflush(stdout);
                
                if (zamkniecie_urzedu) { break; }
                
                // KLON: odbiera żądania i od razu przydziela tickety do ticket_list
                
                if (strncmp(packet.cmd, "ASSIGN_TICKET_TO", 16) == 0 || 
                    strncmp(packet.cmd, "ASSIGN_TICKET", 13) == 0 ||
                    strncmp(packet.cmd, "PETENT_LEFT", 11) == 0) {

                    if (strncmp(packet.cmd, "PETENT_LEFT", 11) == 0) {
                        // Usuń petenta z kolejki w sposób spójny: bez "dziur" i z korektą liczników.
                        sem_wait(sem_clone);
                        remove_pid_from_tickets_locked(packet.pid);
                        sem_post(sem_clone);
                        continue;
                    }

                    if (strncmp(packet.cmd, "ASSIGN_TICKET_TO", 16) == 0) {
                        assign_ticket_to(packet.pid, packet.prio, packet.typ, sem_clone, sem_ticket_assigned);
                    } else {
                        assign_ticket(packet.pid, packet.prio, packet.typ, sem_clone, sem_ticket_assigned);
                    }
                }
            }
            close(fd);
            exit(0);
        }
        return pid;
    };

    biletomat_clones[0] = spawn_clone(0);
    active_clones = 1;
    
    while (running) {
        int suma = 0;
        if (shm_ticket_count) {
            for (int i = 0; i < WYDZIAL_COUNT; ++i) suma += shm_ticket_count[i];
        } else {
            for (int i = 0; i < WYDZIAL_COUNT; ++i) suma += ticket_count[i];
        }
        
        // Informacyjne: ile petentów jest w ticket_list (w obsłudze / w kolejce do urzędników)
        int petents_in_tickets = 0;
        for (int wydzial = 0; wydzial < WYDZIAL_COUNT; ++wydzial) {
            int n = (shm_ticket_count) ? shm_ticket_count[wydzial] : ticket_count[wydzial];
            for (int i = 0; i < n; ++i) {
                if (ticket_list[wydzial][i].PID > 0) {
                    petents_in_tickets++;
                }
            }
        }
        
        if (petents_in_tickets > 0) {
            printf("[Biletomat -> PID=%d]: Petenci w ticket_list: %d\n", getpid(), petents_in_tickets);
        }
        
        int K = N/3;
        int required_clones = 1;
        if (suma > 2*K) required_clones = 3;
        else if (suma > K) required_clones = 2;

        for (int i = active_clones; i < required_clones; ++i) {
            biletomat_clones[i] = spawn_clone(i);
            printf("[Biletomat -> PID=%d]: Uruchomiono klon #%d PID=%d\n", getpid(), i, biletomat_clones[i]);
        }
        for (int i = required_clones; i < active_clones; ++i) {
            if (biletomat_clones[i] > 0) {
                kill(biletomat_clones[i], SIGTERM);
                printf("[Biletomat -> PID=%d]: Zatrzymano klon #%d PID=%d\n", getpid(), i, biletomat_clones[i]);
                biletomat_clones[i] = 0;
            }
        }
        active_clones = required_clones;

        sleep(1);
    }

    for (int i = 0; i < active_clones; ++i) {
        if (biletomat_clones[i] > 0) kill(biletomat_clones[i], SIGTERM);
    }
    
    // Odpraw wszystkich petentów czekających na bilet
    printf("[Biletomat -> PID=%d]: Odprawianie wszystkich czekających petentów...\n", getpid());
    
    // Zamarkuj wszystkie wydziały jako zamknięte - petenci będą wiedzieć że mogą wyjść
    sem_wait(sem);
    for (int wydzial = 0; wydzial < WYDZIAL_COUNT; ++wydzial) {
        wydzial_closed[wydzial] = 1;
    }
    sem_post(sem);
    
    sem_wait(sem);
    int total_dismissed = 0;
    for (int wydzial = 0; wydzial < WYDZIAL_COUNT; ++wydzial) {
        int n = (shm_ticket_count) ? shm_ticket_count[wydzial] : ticket_count[wydzial];
        for (int i = 0; i < n; ++i) {
            pid_t petent_pid = ticket_list[wydzial][i].PID;
            if (petent_pid > 0) {
                // Petent ma bilet (ticket_list) -> SIGTERM (jak odprawienie przez urzędnika)
                kill(petent_pid, SIGTERM);
                printf("[Biletomat -> PID=%d]: Odprawienie petenta PID=%d (wydział %d)\n", getpid(), petent_pid, wydzial);
                total_dismissed++;
            }
        }
    }
    
    sem_post(sem);
    if (total_dismissed > 0) {
        printf("[Biletomat -> PID=%d]: Odprawiono %d petentów\n", getpid(), total_dismissed);
    }
    
    cleanup();
    return 0;
}
