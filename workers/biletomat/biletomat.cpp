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
        printf("[biletomat] Otrzymano SIGUSR1 - kończę pracę.\n");
        running = 0;
    } else {
        running = 0;
    }
    if (sig == SIGUSR2) zamkniecie_urzedu = 1;
}

struct ticket* ticket_list[WYDZIAL_COUNT] = {NULL};
int ticket_count[WYDZIAL_COUNT] = {0};
void* shm_ptr = NULL;
int shm_fd = -1;
sem_t* sem = NULL;
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
    sem_unlink(SEM_NAME);
    unlink(PIPE_NAME);
    shm_unlink(SHM_NAME);
    printf("[biletomat] Zasoby posprzątane.\n");
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

void assign_ticket_to(pid_t pid, int prio, wydzial_t typ, sem_t* sem) {
    sem_wait(sem);
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
    printf("[biletomat] Przydzielono ticket (no-sort) id=%d dla PID=%d, wydzial=%d, priorytet=%d\n", idx, pid, typ, prio);
    sem_post(sem);
}

void assign_ticket(pid_t pid, int prio, wydzial_t typ, sem_t* sem) {
    sem_wait(sem);
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
    printf("[biletomat] Przydzielono ticket id=%d dla PID=%d, wydzial=%d, priorytet=%d (posortowano)\n", idx, pid, typ, prio);
    sem_post(sem);
}

int main() {
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGUSR1, sig_handler);
    signal(SIGUSR2, sigusr2_handler);
    mkfifo(PIPE_NAME, 0666);
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    size_t shm_size = WYDZIAL_COUNT * sizeof(struct ticket) * MAX_TICKETS + WYDZIAL_COUNT * sizeof(int);
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
    sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);

    printf("[biletomat] Oczekiwanie na żądania: %s\n", PIPE_NAME);
    pid_t biletomat_clones[3] = {0};
    int active_clones = 1;

    auto spawn_clone = [](int idx) -> pid_t {
        pid_t pid = fork();
        if (pid == 0) {
            printf("[biletomat-klon %d] Uruchomiony, czekam na żądania...\n", idx);
            fflush(stdout);
            while (running) {
                printf("[biletomat-klon %d] Otwieranie pipe: %s\n", idx, PIPE_NAME);
                fflush(stdout);
                int fd = open(PIPE_NAME, O_RDONLY);
                if (fd < 0) {
                    printf("[biletomat-klon %d] Błąd otwarcia pipe\n", idx);
                    fflush(stdout);
                    sleep(1);
                    continue;
                }
                // Czytaj pakiet: komenda (16 bajtów) + dane
                struct {
                    char cmd[16];
                    pid_t pid;
                    int prio;
                    wydzial_t typ;
                } packet;
                printf("[biletomat-klon %d] Czytam pakiet...\n", idx);
                fflush(stdout);
                int nread = read(fd, &packet, sizeof(packet));
                printf("[biletomat-klon %d] Przeczytano %d bajtów, cmd='%.16s', PID=%d, prio=%d, typ=%d\n", 
                       idx, nread, packet.cmd, packet.pid, packet.prio, packet.typ);
                fflush(stdout);
                if (nread != sizeof(packet)) { 
                    printf("[biletomat-klon %d] Błąd: oczekiwano %lu bajtów, otrzymano %d\n", idx, sizeof(packet), nread);
                    fflush(stdout);
                    close(fd); 
                    continue; 
                }
                if (zamkniecie_urzedu) { close(fd); break; }
                if (strncmp(packet.cmd, "ASSIGN_TICKET_TO", 16) == 0) {
                    if (packet.prio >= 100) {
                        assign_ticket(packet.pid, packet.prio, packet.typ, sem);
                        printf("[biletomat-klon %d] VIP ticket dla PID %d, typ %d\n", idx, packet.pid, packet.typ);
                    } else {
                        assign_ticket_to(packet.pid, packet.prio, packet.typ, sem);
                    }
                } else if (strncmp(packet.cmd, "ASSIGN_TICKET", 13) == 0) {
                    int current = (shm_ticket_count) ? shm_ticket_count[packet.typ] : ticket_count[packet.typ];
                    if (current < MAX_TICKETS) {
                        assign_ticket(packet.pid, packet.prio, packet.typ, sem);
                        printf("[biletomat-klon %d] Przydzielono ticket dla PID %d, typ %d\n", idx, packet.pid, packet.typ);
                    } else {
                        printf("[biletomat-klon %d] Brak biletów dla wydziału %d\n", idx, packet.typ);
                    }
                }
                close(fd);
            }
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
        int N = 60;
        int K = N/3;
        int required_clones = 1;
        if (suma > 2*K) required_clones = 3;
        else if (suma > K) required_clones = 2;

        for (int i = active_clones; i < required_clones; ++i) {
            biletomat_clones[i] = spawn_clone(i);
            printf("[biletomat] Uruchomiono  #%d PID=%d\n", i, biletomat_clones[i]);
        }
        for (int i = required_clones; i < active_clones; ++i) {
            if (biletomat_clones[i] > 0) {
                kill(biletomat_clones[i], SIGTERM);
                printf("[biletomat] Zatrzymano #%d PID=%d\n", i, biletomat_clones[i]);
                biletomat_clones[i] = 0;
            }
        }
        active_clones = required_clones;

        sleep(1);
    }

    for (int i = 0; i < active_clones; ++i) {
        if (biletomat_clones[i] > 0) kill(biletomat_clones[i], SIGTERM);
    }
    cleanup();
    return 0;
}
