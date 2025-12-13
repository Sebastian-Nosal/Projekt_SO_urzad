#ifndef BILETOMAT_H
#define BILETOMAT_H

#include <sys/types.h>
#include <semaphore.h>

#define PIPE_NAME "/tmp/sn_155290_biletomat_wez"
#define SHM_NAME "/sn_155290_biletomat_lista"
#define SEM_NAME "/sn_155290_biletomat_sem1"
#define MAX_TICKETS 128

struct ticket {
	int index;
	pid_t PID;
	int priorytet;
};

extern struct ticket* ticket_list;
extern int* ticket_count;
extern void* shm_ptr;
extern int shm_fd;
extern sem_t* sem;

void sort_queue();
void assign_ticket(pid_t pid, int prio, sem_t* sem);
void cleanup();
void sig_handler(int sig);

#endif // BILETOMAT_H

