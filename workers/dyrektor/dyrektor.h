#ifndef DYREKTOR_H
#define DYREKTOR_H

#include <signal.h>

#define URZEDNIK_EXHAUST_SHM "/sn_155290_urzednik_exhaust"
#define MAX_URZEDNICY 10

/**
 * @brief Sprawdza, czy urzędnicy wyczerpali swoje limity, i usuwa ich, jeśli tak.
 * 
 * @param urzednik_exhausted Tablica flag wskazujących, czy urzędnicy wyczerpali swoje limity.
 * @param urzednicy Tablica PID-ów urzędników.
 * @param liczba_urzednikow Liczba urzędników.
 */
void sprawdz_i_obsluz_wyczerpanych(int* urzednik_exhausted, pid_t* urzednicy, int liczba_urzednikow);

/**
 * @brief Wysyła sygnał do wszystkich urzędników.
 * 
 * @param sygnal Numer sygnału do wysłania.
 */
void wyslij_sygnal_do_urzednikow(int sygnal);

#endif
