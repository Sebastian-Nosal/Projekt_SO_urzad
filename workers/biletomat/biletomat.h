#ifndef BILETOMAT_H
#define BILETOMAT_H


#include <sys/types.h>
#include <semaphore.h>
#include "../../config.h"

#define PIPE_NAME "/tmp/sn_155290_biletomat_wez"
#define SHM_NAME "/sn_155290_biletomat_lista"
#define SEM_NAME "/sn_155290_biletomat_sem1"
#define SEM_TICKET_ASSIGNED "/sn_155290_biletomat_ticket_assigned"
#define MAX_TICKETS 128
#define MAX_WAITING 256  // Maksymalnie oczekujących petentów

struct ticket {
	int index;
	pid_t PID;
	int priorytet;
	wydzial_t typ; // typ/cel biletu (wydział)
};

struct waiting_petent {
	pid_t pid;
	int prio;
	wydzial_t typ;
	int used;  // 1 jeśli slot jest zajęty, 0 jeśli wolny
};

extern struct ticket* ticket_list[WYDZIAL_COUNT];
extern int ticket_count[WYDZIAL_COUNT];
extern struct waiting_petent* waiting_list;
extern int* waiting_count_ptr;  // Wskaźnik do SHM - całkowita liczba oczekujących
extern int* waiting_count_per_wydzial;  // Wskaźnik do SHM - liczba oczekujących PER WYDZIAŁ (tablica WYDZIAL_COUNT)
extern int* wydzial_closed;  // Wskaźnik do SHM - tablica flag czy wydział jest zamknięty (tablica WYDZIAL_COUNT)
extern int* people_inside_ptr;  // Wskaźnik do SHM
extern void* shm_ptr;
extern int shm_fd;
extern sem_t* sem;

void sort_queue(wydzial_t typ);
void assign_ticket(pid_t pid, int prio, wydzial_t typ, sem_t* sem);
void cleanup();
void sig_handler(int sig);
void assign_ticket_to(pid_t pid, int prio, wydzial_t typ, sem_t* sem);

#endif // BILETOMAT_H

