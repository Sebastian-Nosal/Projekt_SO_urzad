#ifndef PETENT_H
#define PETENT_H

#include <sys/types.h>
#include "../../config.h"


typedef struct {
	wydzial_t typ;
	int priorytet;
	int isVIP;
	int isInside;
} PetentData;

// Funkcje obsługi petenta
void petent_start(PetentData* petent);

#endif // PETENT_H
