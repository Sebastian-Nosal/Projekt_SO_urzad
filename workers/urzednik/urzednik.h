/**
 * @brief Obsługuje sygnał SIGUSR1, który oznacza zakończenie pracy przez dyrektora.
 * 
 * @param sig Numer sygnału.
 */
void sigusr1_handler(int sig);

/**
 * @brief Obsługuje sygnał SIGUSR2, który oznacza zamknięcie urzędu.
 * 
 * @param sig Numer sygnału.
 */
void sigusr2_handler(int sig);

/**
 * @brief Pobiera limit petentów dla danego wydziału.
 * 
 * @param typ Typ wydziału.
 * @return Limit petentów dla wydziału.
 */
int get_limit(wydzial_t typ);

/**
 * @brief Losuje cel przekierowania petenta z wydziału SA.
 * 
 * @return Wylosowany typ wydziału.
 */
wydzial_t random_sa_target();