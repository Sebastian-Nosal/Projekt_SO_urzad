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
#define MAX_WAITING 256 

struct ticket {
	int index;
	pid_t PID;
	int priorytet;
	wydzial_t typ; 
};

struct waiting_petent {
	pid_t pid;
	int prio;
	wydzial_t typ;
	int used;
};

extern struct ticket* ticket_list[WYDZIAL_COUNT];
extern int ticket_count[WYDZIAL_COUNT];
extern struct waiting_petent* waiting_list;
extern int* waiting_count_ptr;  
extern int* waiting_count_per_wydzial;  
extern int* wydzial_closed;
extern int* people_inside_ptr;
extern void* shm_ptr;
extern int shm_fd;
extern sem_t* sem;

/**
 * @brief Sortuje kolejkę biletów dla danego wydziału.
 * 
 * @param typ Typ wydziału, dla którego kolejka ma zostać posortowana.
 */
void sort_queue(wydzial_t typ);

/**
 * @brief Przypisuje bilet do petenta.
 * 
 * @param pid ID procesu petenta.
 * @param prio Priorytet biletu.
 * @param typ Typ wydziału dla biletu.
 * @param sem Wskaźnik na semafor używany do synchronizacji.
 */
void assign_ticket(pid_t pid, int prio, wydzial_t typ, sem_t* sem);

/**
 * @brief Czyści zasoby używane przez system biletowy.
 */
void cleanup();

/**
 * @brief Obsługuje określone sygnały.
 * 
 * @param sig Numer sygnału do obsłużenia.
 */
void sig_handler(int sig);

/**
 * @brief Przypisuje bilet do konkretnego petenta.
 * 
 * @param pid ID procesu petenta.
 * @param prio Priorytet biletu.
 * @param typ Typ wydziału dla biletu.
 * @param sem Wskaźnik na semafor używany do synchronizacji.
 */
void assign_ticket_to(pid_t pid, int prio, wydzial_t typ, sem_t* sem);


#endif // BILETOMAT_H

