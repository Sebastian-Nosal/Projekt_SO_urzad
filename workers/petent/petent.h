#ifndef PETENT_H
#define PETENT_H

#include <sys/types.h>
#include "../../config.h"


/**
 * @brief Struktura przechowująca dane petenta.
 */
typedef struct {
	wydzial_t typ;      /**< Typ wydziału, do którego petent się udaje. */
	int priorytet;      /**< Priorytet petenta. */
	int isVIP;          /**< Flaga wskazująca, czy petent jest VIP-em. */
	int isInside;       /**< Flaga wskazująca, czy petent znajduje się w budynku. */
	int hasChild;       /**< Flaga wskazująca, czy petent ma dziecko. */
} PetentData;

/**
 * @brief Rozpoczyna obsługę petenta.
 * 
 * @param petent Wskaźnik na strukturę danych petenta.
 */
void petent_start(PetentData* petent);

/**
 * @brief Obsługuje sygnał SIGUSR2, który oznacza zamknięcie urzędu przez dyrektora.
 * 
 * @param sig Numer sygnału.
 */
void sigusr2_handler(int sig);

/**
 * @brief Obsługuje sygnał SIGUSR1, który oznacza, że sprawa petenta została załatwiona.
 * 
 * @param sig Numer sygnału.
 */
void sigusr1_handler(int sig);

/**
 * @brief Obsługuje sygnał SIGTERM, który oznacza, że petent został odprawiony z kwitkiem.
 * 
 * @param sig Numer sygnału.
 */
void sigterm_handler(int sig);

/**
 * @brief Funkcja wykonywana przez wątek dziecka petenta.
 * 
 * @param pid PID procesu petenta.
 */
void child_thread_function(pid_t pid);

#endif // PETENT_H
