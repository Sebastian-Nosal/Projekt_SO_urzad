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

struct ticket* ticket_list;
int* ticket_count;
void* shm_ptr = NULL;
int shm_fd = -1;
sem_t* sem = NULL;
volatile sig_atomic_t running = 1;
void cleanup() {
    if (shm_ptr) munmap(shm_ptr, sizeof(struct ticket) * MAX_TICKETS + sizeof(int));
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

void sort_queue() {
    int n = *ticket_count;
    for (int i = 0; i < n-1; ++i) {
        for (int j = 0; j < n-i-1; ++j) {
            if (ticket_list[j].priorytet < ticket_list[j+1].priorytet ||
                (ticket_list[j].priorytet == ticket_list[j+1].priorytet && ticket_list[j].index > ticket_list[j+1].index)) {
                struct ticket tmp = ticket_list[j];
                ticket_list[j] = ticket_list[j+1];
                ticket_list[j+1] = tmp;
            }
        }
    }
}

void assign_ticket(pid_t pid, int prio, sem_t* sem) {
    sem_wait(sem);
    int idx = (*ticket_count)++;
    ticket_list[idx].index = idx;
    ticket_list[idx].PID = pid;
    ticket_list[idx].priorytet = prio;
    sort_queue();
    sem_post(sem);
}

int main() {
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    mkfifo(PIPE_NAME, 0666);
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_fd, sizeof(struct ticket) * MAX_TICKETS + sizeof(int));
    shm_ptr = mmap(0, sizeof(struct ticket) * MAX_TICKETS + sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    ticket_list = (struct ticket*)shm_ptr;
    ticket_count = (int*)((char*)shm_ptr + sizeof(struct ticket) * MAX_TICKETS);
    *ticket_count = 0;
    sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);

    printf("[biletomat] Oczekiwanie na żądania przez pipe: %s\n", PIPE_NAME);
    while (running) {
        int fd = open(PIPE_NAME, O_RDONLY);
        pid_t pid;
        int prio = 0;
        int read_bytes = read(fd, &pid, sizeof(pid_t));
        if (read_bytes == sizeof(pid_t)) {
            assign_ticket(pid, prio, sem);
            printf("Przydzielono ticket dla PID %d\n", pid);
        }
        close(fd);
    }
    cleanup();
    return 0;
}
