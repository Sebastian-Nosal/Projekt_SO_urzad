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

struct ticket* ticket_list[WYDZIAL_COUNT] = {NULL};
int* ticket_count = NULL;
void* shm_ptr = NULL;
int shm_fd = -1;
sem_t* sem = NULL;
volatile sig_atomic_t running = 1;
void cleanup() {
    for (int i = 0; i < WYDZIAL_COUNT; ++i) {
        if (ticket_list[i]) munmap(ticket_list[i], sizeof(struct ticket) * MAX_TICKETS);
    }
    if (shm_ptr) munmap(shm_ptr, sizeof(struct ticket) * MAX_TICKETS * WYDZIAL_COUNT + sizeof(int) * WYDZIAL_COUNT);
    if (shm_fd != -1) close(shm_fd);
    if (sem) sem_close(sem);
    sem_unlink(SEM_NAME);
    unlink(PIPE_NAME);
    shm_unlink(SHM_NAME);
    printf("[biletomat] Zasoby posprzątane.\n");
}

void sig_handler(int sig) {
    running = 0;
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

void assign_ticket(pid_t pid, int prio, wydzial_t typ, sem_t* sem) {
    sem_wait(sem);
    int idx = ticket_count[typ]++;
    ticket_list[typ][idx].index = idx;
    ticket_list[typ][idx].PID = pid;
    ticket_list[typ][idx].priorytet = prio;
    ticket_list[typ][idx].typ = typ;
    sort_queue(typ);
    sem_post(sem);
}

int main() {
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    mkfifo(PIPE_NAME, 0666);
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    size_t shm_size = WYDZIAL_COUNT * (sizeof(struct ticket) * MAX_TICKETS + sizeof(int));
    ftruncate(shm_fd, shm_size);
    shm_ptr = mmap(0, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    char* base = (char*)shm_ptr;
    for (int i = 0; i < WYDZIAL_COUNT; ++i) {
        ticket_list[i] = (struct ticket*)(base + i * sizeof(struct ticket) * MAX_TICKETS);
    }
    ticket_count = (int*)(base + WYDZIAL_COUNT * sizeof(struct ticket) * MAX_TICKETS);
    for (int i = 0; i < WYDZIAL_COUNT; ++i) ticket_count[i] = 0;
    sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);

    printf("[biletomat] Oczekiwanie na żądania przez pipe: %s\n", PIPE_NAME);
    while (running) {
        int fd = open(PIPE_NAME, O_RDONLY);
        char cmd[32] = {0};
        int ncmd = read(fd, cmd, sizeof(cmd)-1);
        if (ncmd <= 0) { close(fd); continue; }
        if (strncmp(cmd, "ASSIGN_TICKET_TO", 16) == 0) {
            struct { pid_t pid; int prio; wydzial_t typ; } req;
            int r = read(fd, &req, sizeof(req));
            if (r == sizeof(req)) {
                assign_ticket_to(req.pid, req.prio, req.typ, sem);
            }
        } else if (strncmp(cmd, "ASSIGN_TICKET", 13) == 0) {
            struct { pid_t pid; int prio; wydzial_t typ; } req;
            int r = read(fd, &req, sizeof(req));
            if (r == sizeof(req)) {
                assign_ticket(req.pid, req.prio, req.typ, sem);
                printf("Przydzielono ticket dla PID %d, typ %d\n", req.pid, req.typ);
            }
        }
        close(fd);
    }
    cleanup();
    return 0;
}
