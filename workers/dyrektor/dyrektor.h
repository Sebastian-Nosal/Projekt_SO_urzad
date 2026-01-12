#ifndef DYREKTOR_H
#define DYREKTOR_H

#include <signal.h>

#define URZEDNIK_EXHAUST_SHM "/sn_155290_urzednik_exhaust"
#define MAX_URZEDNICY 10

void check_and_expel_if_exhausted(int* urzednik_exhausted, pid_t* urzednicy, int liczba_urzednikow);

// Funkcje do obsługi sygnałów i wysyłania poleceń do urzędników
void wyslij_sygnal_do_urzednikow(int sygnal);

#endif // DYREKTOR_H
