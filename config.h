#ifndef CONFIG_H
#define CONFIG_H

typedef enum {
	WYDZIAL_SC,
	WYDZIAL_KM, 
	WYDZIAL_ML, 
	WYDZIAL_PD,
	WYDZIAL_SA,
    WYDZIAL_COUNT
} wydzial_t;

#define PETENT_LIMIT_SC 20
#define PETENT_LIMIT_KM 20
#define PETENT_LIMIT_ML 20
#define PETENT_LIMIT_PD 20
#define PETENT_LIMIT_SA 40

#define PROB_SA_SOLVE 0.6
#define PROB_SA_TO_SC 0.1
#define PROB_SA_TO_KM 0.1
#define PROB_SA_TO_ML 0.1
#define PROB_SA_TO_PD 0.1

#define PROB_CHILD 0.3

#define BUILDING_CAPACITY 50

#define PETENT_AMOUNT 1000

#define CZAS_KONIEC 120 // czas działania dyrektora w sekundach

#endif 
