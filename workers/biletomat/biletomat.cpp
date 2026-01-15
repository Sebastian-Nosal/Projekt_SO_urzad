#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <semaphore.h>
// Rewritten clean implementation of biletomat
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
#include "biletomat.h"

extern volatile sig_atomic_t running;
extern volatile sig_atomic_t zamkniecie_urzedu;

// Wskaźnik do tablicy liczników biletek w pamięci współdzielonej
int* shm_ticket_count = NULL;

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
    
    // Sprawdź czy jest miejsce w budynku
    if (*people_inside_ptr >= BUILDING_CAPACITY) {
        // Brak miejsca - dodaj do listy oczekujących
        printf("[biletomat] BRAK MIEJSCA w urzedzie dla petenta PID=%d (wydzial=%d). Musi czekać. Oczekujących: %d\n", pid, typ, *waiting_count_ptr);
        if (*waiting_count_ptr < MAX_WAITING) {
            waiting_list[*waiting_count_ptr].pid = pid;
            waiting_list[*waiting_count_ptr].prio = prio;
            waiting_list[*waiting_count_ptr].typ = typ;
            waiting_list[*waiting_count_ptr].used = 1;
            (*waiting_count_ptr)++;
        }
        sem_post(sem);
        return;
    }
    
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
    (*people_inside_ptr)++;
    int queue_size = (shm_ticket_count) ? shm_ticket_count[typ] : ticket_count[typ];
    printf("[biletomat] Przydzielono ticket (no-sort) id=%d dla PID=%d, wydzial=%d, priorytet=%d, pozycja w kolejce: %d, osób w budynku: %d\n", idx, pid, typ, prio, queue_size, *people_inside_ptr);
    sem_post(sem);
    sem_t* sig = (sem_ticket_sig) ? sem_ticket_sig : sem_ticket_assigned;
    if (sig && sig != SEM_FAILED) sem_post(sig);  // Sygnalizuj że bilet przydzielony
}

void assign_ticket(pid_t pid, int prio, wydzial_t typ, sem_t* sem, sem_t* sem_ticket_sig = nullptr) {
    sem_wait(sem);
    
    // Sprawdź czy jest miejsce w budynku
    if (*people_inside_ptr >= BUILDING_CAPACITY) {
        // Brak miejsca - dodaj do listy oczekujących
        printf("[biletomat] BRAK MIEJSCA w urzedzie dla petenta PID=%d (wydzial=%d). Musi czekać. Oczekujących: %d\n", pid, typ, *waiting_count_ptr);
        if (*waiting_count_ptr < MAX_WAITING) {
            waiting_list[*waiting_count_ptr].pid = pid;
            waiting_list[*waiting_count_ptr].prio = prio;
            waiting_list[*waiting_count_ptr].typ = typ;
            waiting_list[*waiting_count_ptr].used = 1;
            (*waiting_count_ptr)++;
        }
        sem_post(sem);
        return;
    }
    
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
    (*people_inside_ptr)++;
    int queue_size = (shm_ticket_count) ? shm_ticket_count[typ] : ticket_count[typ];
    printf("[biletomat] Przydzielono ticket id=%d dla PID=%d, wydzial=%d, priorytet=%d (posortowano), pozycja w kolejce: %d, osób w budynku: %d\n", idx, pid, typ, prio, queue_size, *people_inside_ptr);
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
            
            // Otwórz semafor dla sygnalizacji przydzielonego biletu
            sem_t* sem_clone_assigned = sem_open(SEM_TICKET_ASSIGNED, 0);
            
            // Otwórz semafor dla dostępu do SHM
            sem_t* sem_clone = sem_open(SEM_NAME, 0);
            // Otwórz FIFO raz na początku w trybie do czytania
            int fd = open(PIPE_NAME, O_RDONLY);
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
                
                int nread = read(fd, &packet, sizeof(packet));
                printf("[Biletomat-klon -> PID=%d]: read() zwróci %d bajtów\n", getpid(), nread);
                fflush(stdout);
                
                // EOF oznacza, że wszyscy pisarze zamknęli FIFO
                if (nread == 0) {
                    printf("[Biletomat-klon -> PID=%d]: EOF na pipe, ponownie otwieranie\n", getpid());
                    fflush(stdout);
                    close(fd);
                    sleep(1);
                    fd = open(PIPE_NAME, O_RDONLY);
                    if (fd < 0) {
                        printf("[Biletomat-klon -> PID=%d]: Błąd ponownego otwarcia pipe\n", getpid());
                        fflush(stdout);
                        break;
                    }
                    continue;
                }
                
                if (nread != (int)sizeof(packet)) { 
                    printf("[Biletomat-klon -> PID=%d]: Blad: oczekiwano %lu bajtów, otrzymano %d\n", getpid(), sizeof(packet), nread);
                    fflush(stdout);
                    continue; 
                }
                
                printf("[biletomat-klon %d] Przeczytano %d bajtów, cmd='%.16s', PID=%d, prio=%d, typ=%d\n", 
                       idx, nread, packet.cmd, packet.pid, packet.prio, packet.typ);
                fflush(stdout);
                
                if (zamkniecie_urzedu) { break; }
                
                // KLON: Tylko odbiera żądania i dodaje do waiting_list
                // Główny biletomat będzie zarządzać wydaniem biletów
                
                if (strncmp(packet.cmd, "ASSIGN_TICKET_TO", 16) == 0 || 
                    strncmp(packet.cmd, "ASSIGN_TICKET", 13) == 0 ||
                    strncmp(packet.cmd, "PETENT_LEFT", 11) == 0) {
                    
                    sem_wait(sem_clone);
                    
                    if (strncmp(packet.cmd, "PETENT_LEFT", 11) == 0) {
                        // Petent opuszcza budynek - zmniejsz people_inside
                        if (*people_inside_ptr > 0) (*people_inside_ptr)--;
                        printf("[biletomat-klon %d] Petent PID=%d opuszcza urząd (zgloszenie), osób w budynku: %d\n", 
                               idx, packet.pid, *people_inside_ptr);
                    } else {
                        // ASSIGN_TICKET lub ASSIGN_TICKET_TO - dodaj do waiting_list
                        if (*waiting_count_ptr < MAX_WAITING) {
                            // OK - dodaj do waiting_list
                            waiting_list[*waiting_count_ptr].pid = packet.pid;
                            waiting_list[*waiting_count_ptr].prio = packet.prio;
                            waiting_list[*waiting_count_ptr].typ = packet.typ;
                            waiting_list[*waiting_count_ptr].used = 1;
                            
                            // Zwiększ licznik dla tego wydziału
                            waiting_count_per_wydzial[packet.typ]++;
                            
                            printf("[biletomat-klon %d] Petent PID=%d dodany do waiting_list (pozycja %d, wydzial=%d, czeka: %d)\n", 
                                   idx, packet.pid, *waiting_count_ptr, packet.typ, 
                                   waiting_count_per_wydzial[packet.typ]);
                            (*waiting_count_ptr)++;
                        } else {
                            printf("[biletomat-klon %d] BŁĄD: waiting_list pełna!\n", idx);
                        }
                    }
                    
                    sem_post(sem_clone);
                }
            }
            close(fd);
            exit(0);
        }
        return pid;
    };

    biletomat_clones[0] = spawn_clone(0);
    active_clones = 1;
    
    int zero_tickets_count = 0;  // Licznik iteracji z zerowymi biletami

    while (running) {
        int suma = 0;
        if (shm_ticket_count) {
            for (int i = 0; i < WYDZIAL_COUNT; ++i) suma += shm_ticket_count[i];
        } else {
            for (int i = 0; i < WYDZIAL_COUNT; ++i) suma += ticket_count[i];
        }
        
        // ===== GŁÓWNY BILETOMAT: Zarządzanie waiting_list =====
        
        // 1. ODPRAWIANIE: Sprawdź czy są petenci czekający do zamkniętych wydziałów
        if (*waiting_count_ptr > 0) {
            sem_wait(sem);
            
            int i = 0;
            while (i < *waiting_count_ptr) {
                if (waiting_list[i].used == 1 && wydzial_closed[waiting_list[i].typ] == 1) {
                    // Wydział zamknięty - odpraw petenta
                    pid_t drop_pid = waiting_list[i].pid;
                    wydzial_t drop_typ = waiting_list[i].typ;
                    
                    printf("[Biletomat -> PID=%d]: Odprawianie petenta PID=%d (wydzial=%d jest zamknięty)\n", 
                           getpid(), drop_pid, drop_typ);
                    
                    // Usuń ze waiting queue
                    if (i < *waiting_count_ptr - 1) {
                        waiting_list[i] = waiting_list[*waiting_count_ptr - 1];
                    }
                    (*waiting_count_ptr)--;
                    waiting_count_per_wydzial[drop_typ]--;
                    
                    // Wyślij SIGTERM
                    kill(drop_pid, SIGTERM);
                } else {
                    i++;
                }
            }
            
            sem_post(sem);
        }
        
        // 2. Wydaj bilet oczekującemu jeśli jest miejsce
        if (*waiting_count_ptr > 0 && *people_inside_ptr < BUILDING_CAPACITY) {
            sem_wait(sem);
            
            // Wydaj bilet pierwszemu oczekującemu (jeśli miejsce)
            if (*waiting_count_ptr > 0 && *people_inside_ptr < BUILDING_CAPACITY) {
                pid_t waiting_pid = waiting_list[0].pid;
                int waiting_prio = waiting_list[0].prio;
                wydzial_t waiting_typ = waiting_list[0].typ;
                
                // Wydaj bilet
                int idx = 0;
                if (shm_ticket_count) {
                    idx = shm_ticket_count[waiting_typ]++;
                    ticket_count[waiting_typ] = shm_ticket_count[waiting_typ];
                } else {
                    idx = ticket_count[waiting_typ]++;
                }
                ticket_list[waiting_typ][idx].index = idx;
                ticket_list[waiting_typ][idx].PID = waiting_pid;
                ticket_list[waiting_typ][idx].priorytet = waiting_prio;
                ticket_list[waiting_typ][idx].typ = waiting_typ;
                if (shm_ticket_count) shm_ticket_count[waiting_typ] = ticket_count[waiting_typ];
                (*people_inside_ptr)++;
                
                printf("[Biletomat -> PID=%d]: WYDANO bilet dla czekającego PID=%d (wydzial=%d, pozycja w kolejce %d), osób w budynku: %d/%d\n", 
                       getpid(), waiting_pid, waiting_typ, idx, *people_inside_ptr, BUILDING_CAPACITY);
                
                // Usuń ze waiting queue
                wydzial_t removed_typ = waiting_list[0].typ;  // Zapamięta typ przed usunięciem
                if (*waiting_count_ptr > 1) {
                    waiting_list[0] = waiting_list[*waiting_count_ptr - 1];
                }
                (*waiting_count_ptr)--;
                waiting_count_per_wydzial[removed_typ]--;  // Zmniejsz licznik dla tego wydziału
            }
            
            sem_post(sem);
        }
        
        // Sprawdzenie czy bilety się wyczerpały I nie ma już oczekujących
        if (suma == 0 && *waiting_count_ptr == 0) {
            zero_tickets_count++;
            if (zero_tickets_count >= 10) {  // Przez 10 iteracji brak biletów i brak czekających = koniec
                printf("[Biletomat -> PID=%d]: Bilety wyczerpane i brak czekających\n", getpid());
                printf("[Biletomat -> PID=%d]: Koniec pracy biletomatu\n", getpid());
                running = 0;
                break;
            }
        } else {
            if (*waiting_count_ptr > 0) {
                printf("[Biletomat -> PID=%d]: Oczekujący petenci: %d\n", getpid(), *waiting_count_ptr);
            }
            zero_tickets_count = 0;  // Reset licznika jeśli pojawiły się bilety lub są czekający
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
    sem_wait(sem);
    int total_dismissed = 0;
    for (int wydzial = 0; wydzial < WYDZIAL_COUNT; ++wydzial) {
        for (int i = 0; i < ticket_count[wydzial]; ++i) {
            pid_t petent_pid = ticket_list[wydzial][i].PID;
            if (petent_pid > 0) {
                kill(petent_pid, SIGTERM);
                printf("[Biletomat -> PID=%d]: Odprawienie petenta PID=%d (wydział %d)\n", getpid(), petent_pid, wydzial);
                total_dismissed++;
            }
        }
    }
    
    // Odpraw również petentów czekających w waiting_list
    for (int i = 0; i < *waiting_count_ptr; ++i) {
        if (waiting_list[i].used == 1) {
            pid_t petent_pid = waiting_list[i].pid;
            kill(petent_pid, SIGTERM);
            printf("[Biletomat -> PID=%d]: Odprawienie petenta PID=%d (czekający)\n", getpid(), petent_pid);
            total_dismissed++;
        }
    }
    
    sem_post(sem);
    if (total_dismissed > 0) {
        printf("[Biletomat -> PID=%d]: Odprawiono %d petentów\n", getpid(), total_dismissed);
    }
    
    cleanup();
    return 0;
}
