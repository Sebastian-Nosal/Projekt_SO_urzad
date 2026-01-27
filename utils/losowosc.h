/**
 * @file losowosc.h
 * @brief Narzędzia losujące wykorzystywane w symulacji.
 */

#ifndef LOSOWOSC_H
#define LOSOWOSC_H

#include <random>

/**
 * @brief Losuje liczbę całkowitą z podanego zakresu.
 * @param zakresDolny Dolna granica (włącznie).
 * @param zakresGorny Górna granica (włącznie).
 * @return Wylosowana liczba.
 */
int losujIlosc(int zakresDolny, int zakresGorny);

/**
 * @brief Losuje indeks na podstawie proporcji.
 * @param szansa1 Proporcja 1.
 * @param szansa2 Proporcja 2.
 * @param szansa3 Proporcja 3.
 * @param szansa4 Proporcja 4.
 * @return Wylosowany indeks (1..4).
 */
int losujSzansa(float szansa1, float szansa2, float szansa3, float szansa4);

#endif // LOSOWOSC_H