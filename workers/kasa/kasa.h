#ifndef KASA_H
#define KASA_H

#include <sys/types.h>

/**
 * @brief Struktura reprezentująca żądanie do kasy.
 */
struct kasa_request {
    pid_t petent_pid; /**< PID petenta składającego żądanie. */
    int kwota;        /**< Kwota do zapłaty. */
};

#define KASA_PIPE "/tmp/sn_155290_kasa_pipe"

/**
 * @brief Obsługuje żądania do kasy.
 */
void obsluga_kasy();

#endif // KASA_H
