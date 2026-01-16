#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <string.h>
#include "kasa.h"

volatile sig_atomic_t running = 1;

void sig_handler(int sig) {
    if (sig == SIGUSR1) {
        printf("[Kasa -> PID=%d]: Otrzymano SIGUSR1 - konczę pracę\n", getpid());
    }
    running = 0;
}

void obsluga_kasy() {
    mkfifo(KASA_PIPE, 0666);
    printf("[Kasa -> PID=%d]: Start pracy, oczekiwanie na żądania przez pipe: %s\n", getpid(), KASA_PIPE);
    while (running) {
        // Otwórz pipe w trybie non-blocking, aby mógł się wybudzić na sygnały
        int fd = open(KASA_PIPE, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            struct kasa_request req;
            int r = read(fd, &req, sizeof(req));
            if (r == sizeof(req)) {
                printf("[Kasa -> PID=%d]: Otrzymano żądanie opłaty od PID=%d, kwota=%d\n", getpid(), req.petent_pid, req.kwota);
                // Symulacja obsługi opłaty
                sleep(1);
                printf("[kasa] Opłata przyjęta od PID=%d\n", req.petent_pid);
                printf("[kasa] Petent przyszedł do kasy (PID=%d)\n", req.petent_pid);
                fflush(stdout);
            }
            close(fd);
        }
        // Krótka pauza, aby znowu sprawdzić sygnały
        sleep(1);
    }
    unlink(KASA_PIPE);
    printf("[kasa] Zakończono pracę.\n");
}

int main() {
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGUSR1, sig_handler);
    obsluga_kasy();
    return 0;
}
