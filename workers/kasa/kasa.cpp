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
    running = 0;
}

void obsluga_kasy() {
    mkfifo(KASA_PIPE, 0666);
    printf("[kasa] Start pracy, oczekiwanie na żądania przez pipe: %s\n", KASA_PIPE);
    while (running) {
        int fd = open(KASA_PIPE, O_RDONLY);
        struct kasa_request req;
        int r = read(fd, &req, sizeof(req));
        if (r == sizeof(req)) {
            printf("[kasa] Otrzymano żądanie opłaty od PID=%d, kwota=%d\n", req.petent_pid, req.kwota);
            // Symulacja obsługi opłaty
            sleep(1);
            printf("[kasa] Opłata przyjęta od PID=%d\n", req.petent_pid);
        }
        close(fd);
    }
    unlink(KASA_PIPE);
    printf("[kasa] Zakończono pracę.\n");
}

int main() {
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    obsluga_kasy();
    return 0;
}
