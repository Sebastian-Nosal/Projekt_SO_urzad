#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <semaphore.h>
#include <sys/wait.h>
#include <signal.h>
#include "biletomat.h"
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <semaphore.h>
#include <format>
#include <sstream>
#include "../../utils/zapisz_logi.h"


extern volatile sig_atomic_t running;
extern volatile sig_atomic_t zamkniecie_urzedu;

int* shm_ticket_count = NULL;

void sig_handler(int sig) {
    if (sig == SIGUSR1) {
        {
            std::ostringstream oss;
            oss << "Otrzymano SIGUSR1 - koncz\u0119 prac\u0119";
            zapisz_log("Biletomat", getpid(), oss.str());
        }
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
    {
        std::ostringstream oss;
        oss << "Zasoby posprz\u0105tane";
        zapisz_log("Biletomat", getpid(), oss.str());
    }
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
    {
        std::ostringstream oss;
        oss << "Przydzielono ticket (no-sort) id=" << idx << " dla PID=" << pid << ", wydzial=" << typ << ", priorytet=" << prio;
        zapisz_log("biletomat", getpid(), oss.str());
    }
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
    {
        std::ostringstream oss;
        oss << "Przydzielono ticket id=" << idx << " dla PID=" << pid << ", wydzial=" << typ << ", priorytet=" << prio << " (posortowano)";
        zapisz_log("biletomat", getpid(), oss.str());
    }
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

    {
        std::ostringstream oss;
        oss << "Oczekiwanie na \u017c\u0105dania: " << PIPE_NAME;
        zapisz_log("Biletomat", getpid(), oss.str());
    }
    pid_t biletomat_clones[3] = {0};
    int active_clones = 1;

    auto spawn_clone = [](int idx) -> pid_t {
        pid_t pid = fork();
        if (pid == 0) {
            {
                std::ostringstream oss;
                oss << "Uruchomiony, czekam na \u017c\u0105dania";
                zapisz_log("Biletomat-klon", getpid(), oss.str());
            }
            {
                std::ostringstream oss;
                oss << "Otwieranie FIFO: " << PIPE_NAME;
                zapisz_log("Biletomat-klon", getpid(), oss.str());
            }
            fflush(stdout);
            int fd = open(PIPE_NAME, O_RDONLY);
            if (fd < 0) {
                {
                    std::ostringstream oss;
                    oss << "Blad otwarcia pipe: " << PIPE_NAME;
                    zapisz_log("Biletomat-klon", getpid(), oss.str());
                }
                fflush(stdout);
                exit(1);
            }
            {
                std::ostringstream oss;
                oss << "FIFO otwarte, czekam na \u017c\u0105dania";
                zapisz_log("Biletomat-klon", getpid(), oss.str());
            }
            fflush(stdout);
            
            while (running) {
                struct {
                    char cmd[16];
                    pid_t pid;
                    int prio;
                    wydzial_t typ;
                } packet;
                
                int nread = read(fd, &packet, sizeof(packet));
                {
                    std::ostringstream oss;
                    oss << "read() zwr\u00f3ci " << nread << " bajt\u00f3w";
                    zapisz_log("Biletomat-klon", getpid(), oss.str());
                }
                fflush(stdout);
                
                if (nread == 0) {
                    {
                        std::ostringstream oss;
                        oss << "EOF na pipe, ponownie otwieranie";
                        zapisz_log("Biletomat-klon", getpid(), oss.str());
                    }
                    fflush(stdout);
                    close(fd);
                    sleep(1);
                    fd = open(PIPE_NAME, O_RDONLY);
                    if (fd < 0) {
                        {
                            std::ostringstream oss;
                            oss << "B\u0142\u0105d ponownego otwarcia pipe";
                            zapisz_log("Biletomat-klon", getpid(), oss.str());
                        }
                        fflush(stdout);
                        break;
                    }
                    continue;
                }
                
                if (nread != (int)sizeof(packet)) { 
                    {
                        std::ostringstream oss;
                        oss << "Blad: oczekiwano " << sizeof(packet) << " bajt\u00f3w, otrzymano " << nread;
                        zapisz_log("Biletomat-klon", getpid(), oss.str());
                    }
                    fflush(stdout);
                    continue; 
                }
                
                {
                    std::ostringstream oss;
                    oss << "Przeczytano " << nread << " bajt\u00f3w, cmd='" << packet.cmd << "', PID=" << packet.pid << ", prio=" << packet.prio << ", typ=" << packet.typ;
                    zapisz_log("biletomat-klon", idx, oss.str());
                }
                fflush(stdout);
                
                if (zamkniecie_urzedu) { break; }
                
                if (strncmp(packet.cmd, "ASSIGN_TICKET_TO", 16) == 0) {
                    if (packet.prio >= 100) {
                        assign_ticket(packet.pid, packet.prio, packet.typ, sem);
                        {
                            std::ostringstream oss;
                            oss << "VIP ticket dla PID " << packet.pid << ", typ " << packet.typ;
                            zapisz_log("biletomat-klon", idx, oss.str());
                        }
                    } else {
                        assign_ticket_to(packet.pid, packet.prio, packet.typ, sem);
                    }
                } else if (strncmp(packet.cmd, "ASSIGN_TICKET", 13) == 0) {
                    int current = (shm_ticket_count) ? shm_ticket_count[packet.typ] : ticket_count[packet.typ];
                    if (current < MAX_TICKETS) {
                        assign_ticket(packet.pid, packet.prio, packet.typ, sem);
                        {
                            std::ostringstream oss;
                            oss << "Przydzielono ticket dla PID " << packet.pid << ", typ " << packet.typ;
                            zapisz_log("biletomat-klon", idx, oss.str());
                        }
                    } else {
                        {
                            std::ostringstream oss;
                            oss << "Brak bilet\u00f3w dla wydzia\u0142u " << packet.typ;
                            zapisz_log("biletomat-klon", idx, oss.str());
                        }
                    }
                }

                FILE* daily_report = fopen("raport_dzienny.txt", "a");
                if (daily_report) {
                    fprintf(daily_report, "ID=%d - Skierowanie do wydziału %d - Wystawił: %d\n", packet.pid, packet.typ, getpid());
                    fclose(daily_report);
                } else {
                    perror("[biletomat] Nie udało się otworzyć pliku raport_dzienny.txt");
                }
            }
            close(fd);
            exit(0);
        }
        return pid;
    };

    biletomat_clones[0] = spawn_clone(0);
    active_clones = 1;
    
    int zero_tickets_count = 0;

    while (running) {
        int suma = 0;
        if (shm_ticket_count) {
            for (int i = 0; i < WYDZIAL_COUNT; ++i) suma += shm_ticket_count[i];
        } else {
            for (int i = 0; i < WYDZIAL_COUNT; ++i) suma += ticket_count[i];
        }
        
        if (suma == 0) {
            zero_tickets_count++;
            if (zero_tickets_count >= 10) {
                {
                    std::ostringstream oss;
                    oss << "Bilety wyczerpane";
                    zapisz_log("Biletomat", getpid(), oss.str());
                }
                {
                    std::ostringstream oss;
                    oss << "Koniec pracy biletomatu";
                    zapisz_log("Biletomat", getpid(), oss.str());
                }
                running = 0;
                break;
            }
        } else {
            zero_tickets_count = 0;
        }
        
        int N = 60;
        int K = N/3;
        int required_clones = 1;
        if (suma > 2*K) required_clones = 3;
        else if (suma > K) required_clones = 2;

        for (int i = active_clones; i < required_clones; ++i) {
            biletomat_clones[i] = spawn_clone(i);
            {
                std::ostringstream oss;
                oss << "Uruchomiono klon #" << i << " PID=" << biletomat_clones[i];
                zapisz_log("Biletomat", getpid(), oss.str());
            }
        }
        for (int i = required_clones; i < active_clones; ++i) {
            if (biletomat_clones[i] > 0) {
                kill(biletomat_clones[i], SIGTERM);
                {
                    std::ostringstream oss;
                    oss << "Zatrzymano klon #" << i << " PID=" << biletomat_clones[i];
                    zapisz_log("Biletomat", getpid(), oss.str());
                }
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
