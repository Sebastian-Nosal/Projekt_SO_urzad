/**
 * @file shm.h
 * @brief Definicje pamięci współdzielonej.
 */

#ifndef SHM_H
#define SHM_H

#include <cstdint>

/**
 * @brief Stan współdzielony między procesami symulacji.
 */
struct SharedState {
    int loaderPid;
    int activeOfficers;
    int clientsInBuilding;
    int livePetents;
    int officeOpen;
    int ticketQueueLen;
    int activeTicketMachines;
    int officerStatus[6];
    int exhaustedCount[5];
    int exhaustedDept[5];
};

#endif // SHM_H
