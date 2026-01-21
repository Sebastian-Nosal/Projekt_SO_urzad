#ifndef SHM_H
#define SHM_H

#include <cstdint>

struct SharedState {
    int loaderPid;
    int activeOfficers;
    int clientsInBuilding;
    int livePetents;
    int officeOpen;
    int ticketQueueLen;
    int activeTicketMachines;
    int officerStatus[6];
};

#endif // SHM_H
