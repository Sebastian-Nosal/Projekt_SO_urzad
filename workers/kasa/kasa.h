#ifndef KASA_H
#define KASA_H

#include <sys/types.h>

// Prosta struktura żądania do kasy
struct kasa_request {
    pid_t petent_pid;
    int kwota;
};

#define KASA_PIPE "/tmp/sn_155290_kasa_pipe"

void obsluga_kasy();

#endif // KASA_H
