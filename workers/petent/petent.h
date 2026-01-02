#ifndef PETENT_H
#define PETENT_H

#include <sys/types.h>
#include "../../config.h"

// Funkcje obsługi petenta
void petent_start(wydzial_t typ, int priorytet, int is_vip);

#endif // PETENT_H
