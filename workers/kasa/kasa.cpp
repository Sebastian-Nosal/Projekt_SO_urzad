#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <string.h>
#include <format>
#include <sstream>
#include "../../utils/zapisz_logi.h"
#include "kasa.h"

volatile sig_atomic_t running = 1;

void sig_handler(int sig) {
    if (sig == SIGUSR1) {
        {
            std::ostringstream oss;
            oss << "Otrzymano SIGUSR1 - koncz\u0119 prac\u0119";
            zapisz_log("Kasa", getpid(), oss.str());
        }
        running = 0;
    }
}

void obsluga_kasy() {
    mkfifo(KASA_PIPE, 0666);
    {
        std::ostringstream oss;
        oss << "Start pracy, oczekiwanie na \u017c\u0105dania przez pipe: " << KASA_PIPE;
        zapisz_log("Kasa", getpid(), oss.str());
    }
    while (running) {
        int fd = open(KASA_PIPE, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            struct kasa_request req;
            int r = read(fd, &req, sizeof(req));
            if (r == sizeof(req)) {
                {
                    std::ostringstream oss;
                    oss << "Otrzymano \u017c\u0105danie op\u0142aty od PID=" << req.petent_pid << ", kwota=" << req.kwota;
                    zapisz_log("Kasa", getpid(), oss.str());
                }
                sleep(1);
                {
                    std::ostringstream oss;
                    oss << "Op\u0142ata przyj\u0119ta od PID=" << req.petent_pid;
                    zapisz_log("kasa", getpid(), oss.str());
                }
                {
                    std::ostringstream oss;
                    oss << "Petent przyszed\u0142 do kasy (PID=" << req.petent_pid << ")";
                    zapisz_log("kasa", getpid(), oss.str());
                }
                fflush(stdout);
            }
            close(fd);
        }
        sleep(1);
    }
    {
        std::ostringstream oss;
        oss << "Zako\u0144czono prac\u0119.";
        zapisz_log("kasa", getpid(), oss.str());
    }
}

int main() {
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGUSR1, sig_handler);
    obsluga_kasy();
    return 0;
}
