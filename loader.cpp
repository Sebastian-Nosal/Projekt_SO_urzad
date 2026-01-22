#include <iostream>
#include <thread>
#include <chrono>
#include <ctime>
#include <csignal>
#include <unistd.h> // fork() and exec()
#include <cerrno>
#include <cstring>
#include <atomic>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <semaphore.h> // POSIX semaphores
#include <fcntl.h> // O_CREAT
#include <sys/stat.h> // permissions
#include "utils/losowosc.h"
#include "config/config.h"
#include "config/messages.h"
#include "config/shm.h"

namespace {
volatile sig_atomic_t g_shutdown = 0;

void obsluzSigint(int) {
    g_shutdown = 1;
}

void reapZombies() {
    int status = 0;
    while (waitpid(-1, &status, WNOHANG) > 0) {
        // reaped
    }
}
}

pid_t uruchomProces(const std::string& nazwa, const std::string& argument = "") {
    pid_t pid = fork();

    if (pid == 0) { // proces potomny
        char cwd[4096] = {0};
        std::string executable = "./" + nazwa;
        if (getcwd(cwd, sizeof(cwd)) != nullptr) {
            executable = std::string(cwd) + "/" + nazwa;
        }
        if (!argument.empty()) {
            execl(executable.c_str(), nazwa.c_str(), argument.c_str(), nullptr);
        } else {
            execl(executable.c_str(), nazwa.c_str(), nullptr);
        }

        std::cerr << "{loader, " << getpid() << "} Blad uruchomienia procesu: " << nazwa << " (" << std::strerror(errno) << ")" << std::endl;
        _exit(EXIT_FAILURE);
    } else if (pid < 0) { // blad forka
        std::cerr << "Fork nieudany dla procesu: " << nazwa << std::endl;
        return -1;
    } else { // proces macierzysty
        std::cout << "{loader, " << getpid() << "} Uruchomiono proces: " << nazwa << " PID: " << pid << std::endl;
        return pid;
    }
    return -1;
}

void logDev(const SharedState* stan, int wygenerowani, int obsluzeni) {
    int poza = stan->livePetents - stan->clientsInBuilding;
    if (poza < 0) poza = 0;
    std::cout << "{loader, " << getpid() << "} petenci_w_budynku=" << stan->clientsInBuilding
              << " petenci_poza=" << poza
              << " petenci_w_kolejce_po_bilet=" << stan->ticketQueueLen
              << " petenci_zywi=" << stan->livePetents
              << " urzednicy_aktywni=" << stan->activeOfficers
              << " biletomaty_aktywne=" << stan->activeTicketMachines
              << std::endl;
}

void uruchomUrzednikow() {
    for (int i = 0; i < NUM_SA_OFFICERS; ++i) {
        uruchomProces("workers/urzednik/urzednik", "SA");
    }

    for (int i = 0; i < NUM_SC_OFFICERS; ++i) {
        uruchomProces("workers/urzednik/urzednik", "SC");
    }

    for (int i = 0; i < NUM_KM_OFFICERS; ++i) {
        uruchomProces("workers/urzednik/urzednik", "KM");
    }

    for (int i = 0; i < NUM_ML_OFFICERS; ++i) {
        uruchomProces("workers/urzednik/urzednik", "ML");
    } 

    for (int i = 0; i < NUM_PD_OFFICERS; ++i) {
        uruchomProces("workers/urzednik/urzednik", "PD");
    } 
}

void sterujBiletomatami(int mqidOther, SharedState* stan, sem_t* semaphore) {
    int dlugoscKolejki = 0;
    int aktywne = 0;

    sem_wait(semaphore);
    dlugoscKolejki = stan->ticketQueueLen;
    aktywne = stan->activeTicketMachines;
    sem_post(semaphore);

    int docelowe = TICKET_MACHINES_MIN;
    if (dlugoscKolejki > TICKET_MACHINE_K) {
        docelowe = 2;
    }
    if (dlugoscKolejki > 2 * TICKET_MACHINE_K) {
        docelowe = 3;
    }
    if (docelowe == 3 && dlugoscKolejki < TICKET_MACHINE_THIRD_CLOSE) {
        docelowe = 2;
    }
    if (docelowe == 2 && dlugoscKolejki < TICKET_MACHINE_K) {
        docelowe = 1;
    }

    if (docelowe != aktywne) {
        Message ctrl{};
        ctrl.mtype = static_cast<long>(ProcessMqType::Biletomat);
        ctrl.senderId = getpid();
        ctrl.receiverId = 0;
        ctrl.group = MessageGroup::Biletomat;
        ctrl.messageType.biletomatType = (docelowe > aktywne)
            ? BiletomatMessagesEnum::Aktywuj
            : BiletomatMessagesEnum::Dezaktywuj;
        ctrl.data1 = docelowe;

        if (msgsnd(mqidOther, &ctrl, sizeof(ctrl) - sizeof(long), 0) == -1) {
            perror("msgsnd failed");
        } else {
            sem_wait(semaphore);
            stan->activeTicketMachines = docelowe;
            sem_post(semaphore);
        }
    }
}

void zarzadzaniePetentami(SharedState* stan, sem_t* semaphore, std::atomic<int>* wygenerowani) {
    int czasStart = std::time(nullptr);

    while (std::time(nullptr) - czasStart < SIMULATION_DURATION) {
        int pozostaloDoLimitu = DAILY_CLIENTS - wygenerowani->load();
        if (pozostaloDoLimitu <= 0) {
            break;
        }
        
        int liczbaPetentow = losujIlosc(ADD_PETENTS_MIN, ADD_PETENTS_MAX);

        if (liczbaPetentow > pozostaloDoLimitu) {
            liczbaPetentow = pozostaloDoLimitu;
        }

        sem_wait(semaphore);
        int zywi = stan->livePetents;
        int miejsca = PETENT_MAX_COUNT_IN_MOMENT - zywi;
        sem_post(semaphore);

        if (miejsca <= 0) {
            int czasOczekiwania = losujIlosc(WAIT_MIN, WAIT_MAX);
            std::this_thread::sleep_for(std::chrono::seconds(czasOczekiwania));
            continue;
        }

        if (liczbaPetentow > miejsca) {
            liczbaPetentow = miejsca;
        }

        for (int i = 0; i < liczbaPetentow; ++i) {
            uruchomProces("workers/petent/petent");
            ++(*wygenerowani);
            sem_wait(semaphore);
            stan->livePetents += 1;
            sem_post(semaphore);
        }

        int czasOczekiwania = losujIlosc(INTERVAL_MIN, INTERVAL_MAX);
        std::this_thread::sleep_for(std::chrono::seconds(czasOczekiwania));
    }

    std::cout << "{loader, " << getpid() << "} Zarzadzanie petentami zakonczone (koniec czasu pracy urzędu)" << std::endl;

    sem_wait(semaphore);
    stan->officeOpen = 0;
    sem_post(semaphore);
}

int initSharedMemory() {
    int shmid = shmget(SHM_KEY, sizeof(SharedState), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget failed");
        exit(EXIT_FAILURE);
    }
    return shmid;
}

int initMessageQueue(key_t key) {
    int mqid = msgget(key, IPC_CREAT | 0666);
    if (mqid == -1) {
        perror("msgget failed");
        exit(EXIT_FAILURE);
    }
    return mqid;
}

sem_t* initSemaphore() {
    sem_t* semaphore = sem_open(SEMAPHORE_NAME, O_CREAT, 0666, 1);
    if (semaphore == SEM_FAILED) {
        perror("sem_open failed");
        exit(EXIT_FAILURE);
    }
    return semaphore;
}

// Funkcja odbierająca komunikaty z MQ
void obsluzKomunikaty(int mqidPetent, int mqidOther, SharedState* stan, sem_t* semaphore, std::atomic<int>* obsluzeni) {
    Message msg;
    const long entryType = getpid();
    const long exitType = getpid() + 1;

    while (true) {
        if (msgrcv(mqidPetent, &msg, sizeof(msg) - sizeof(long), exitType, IPC_NOWAIT) != -1) {
            if (msg.group != MessageGroup::Loader) {
                continue;
            }

            std::cout << "{loader, " << getpid() << "} recv from=" << msg.senderId << " to=" << msg.receiverId << std::endl;

            if (msg.messageType.loaderType == LoaderMessagesEnum::PetentOpuszczaBudynek) {
                int zajeteMiejsca = msg.data2;
                if (zajeteMiejsca <= 0) {
                    zajeteMiejsca = 0;
                }
                sem_wait(semaphore);
                if (zajeteMiejsca > 0) {
                    if (stan->clientsInBuilding >= zajeteMiejsca) {
                        stan->clientsInBuilding -= zajeteMiejsca;
                    } else {
                        stan->clientsInBuilding = 0;
                    }
                }
                if (stan->livePetents > 0) {
                    --stan->livePetents;
                }
                sem_post(semaphore);
                if (msg.data1 == 1) {
                    ++(*obsluzeni);
                }
            }
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        break;
    }

    sem_wait(semaphore);
    int miejsca = MAX_CLIENTS_IN_BUILDING - stan->clientsInBuilding;
    sem_post(semaphore);

    if (miejsca > 0) {
        while (true) {
            if (msgrcv(mqidPetent, &msg, sizeof(msg) - sizeof(long), entryType, IPC_NOWAIT) != -1) {
                if (msg.group != MessageGroup::Loader) {
                    continue;
                }

                std::cout << "{loader, " << getpid() << "} recv from=" << msg.senderId << " to=" << msg.receiverId << std::endl;

                if (msg.messageType.loaderType == LoaderMessagesEnum::NowyPetent) {
                    int zajeteMiejsca = msg.data2;
                    if (zajeteMiejsca <= 0) {
                        zajeteMiejsca = 1;
                    }

                    sem_wait(semaphore);
                    if (stan->clientsInBuilding + zajeteMiejsca <= MAX_CLIENTS_IN_BUILDING) {
                        stan->clientsInBuilding += zajeteMiejsca;
                        sem_post(semaphore);

                        Message response{};
                        response.mtype = msg.senderId;
                        response.senderId = getpid();
                        response.receiverId = msg.senderId;
                        response.group = MessageGroup::Petent;
                        response.messageType.petentType = PetentMessagesEnum::WejdzDoBudynku;

                        if (msgsnd(mqidPetent, &response, sizeof(response) - sizeof(long), 0) == -1) {
                            perror("msgsnd failed");
                        }
                    } else {
                        sem_post(semaphore);
                        if (msgsnd(mqidPetent, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
                            perror("msgsnd failed");
                        }
                        break;
                    }
                }
                continue;
            }
            if (errno == EINTR) {
                continue;
            }
            break;
        }
    }

    while (true) {
        if (msgrcv(mqidOther, &msg, sizeof(msg) - sizeof(long), static_cast<long>(ProcessMqType::Loader), IPC_NOWAIT) != -1) {
            if (msg.group == MessageGroup::Biletomat) {
                std::cout << "{loader, " << getpid() << "} recv from=" << msg.senderId << " to=loader" << std::endl;
                if (msg.messageType.biletomatType == BiletomatMessagesEnum::PetentCzekaNaBilet) {
                    sem_wait(semaphore);
                    stan->ticketQueueLen += 1;
                    sem_post(semaphore);
                } else if (msg.messageType.biletomatType == BiletomatMessagesEnum::PetentOdebralBilet) {
                    sem_wait(semaphore);
                    if (stan->ticketQueueLen > 0) {
                        stan->ticketQueueLen -= 1;
                    }
                    sem_post(semaphore);
                }
            }
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        break;
    }
}

void odrzucOczekujacychPetentow(int mqidPetent, int loaderPid) {
    Message msg;
    while (true) {
        if (msgrcv(mqidPetent, &msg, sizeof(msg) - sizeof(long), loaderPid, IPC_NOWAIT) != -1) {
            if (msg.group != MessageGroup::Loader) {
                continue;
            }
            if (msg.messageType.loaderType != LoaderMessagesEnum::NowyPetent) {
                continue;
            }

            Message response{};
            response.mtype = msg.senderId;
            response.senderId = loaderPid;
            response.receiverId = msg.senderId;
            response.group = MessageGroup::Petent;
            response.messageType.petentType = PetentMessagesEnum::Odprawiony;

            if (msgsnd(mqidPetent, &response, sizeof(response) - sizeof(long), 0) == -1) {
                perror("msgsnd failed");
            }
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        break;
    }
}


int main() {
    std::signal(SIGUSR1, SIG_IGN);
    std::signal(SIGUSR2, SIG_IGN);
    std::signal(SIGINT, obsluzSigint);
    std::signal(SIGTERM, obsluzSigint);
    setpgid(0, 0);
    // Shared memory init
    int shmid = initSharedMemory();
    SharedState* stan = static_cast<SharedState*>(shmat(shmid, nullptr, 0));
    if (stan == reinterpret_cast<SharedState*>(-1)) {
        perror("shmat failed");
        exit(EXIT_FAILURE);
    }
    stan->loaderPid = getpid();
    stan->activeOfficers = NUM_SA_OFFICERS + NUM_SC_OFFICERS + NUM_KM_OFFICERS + NUM_ML_OFFICERS + NUM_PD_OFFICERS;
    stan->clientsInBuilding = 0;
    stan->livePetents = 0;
    stan->officeOpen = 1;
    stan->ticketQueueLen = 0;
    stan->activeTicketMachines = TICKET_MACHINES_MIN;
    for (int i = 0; i < 6; ++i) {
        stan->officerStatus[i] = 0;
    }

    int mqidPetent = initMessageQueue(MQ_KEY);      // petent queue
    int mqidOther = initMessageQueue(MQ_KEY + 1);   // other processes queue

    sem_t* semaphore = initSemaphore();

    uruchomProces("monitoring");
    uruchomProces("workers/dyrektor/dyrektor");
    uruchomUrzednikow();
    uruchomProces("workers/kasa/kasa");
    for (int i = 0; i < TICKET_MACHINES_MAX; ++i) {
        uruchomProces("workers/biletomat/biletomat", std::to_string(i));
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::atomic<int> wygenerowani{0};
    std::atomic<int> obsluzeni{0};
    std::thread watekPetentow(zarzadzaniePetentami, stan, semaphore, &wygenerowani);

    // Main MQ loop
    auto lastLog = std::chrono::steady_clock::now();
    while (true) {
        reapZombies();
        sem_wait(semaphore);
        int open = stan->officeOpen;
        if (g_shutdown) {
            stan->officeOpen = 0;
            open = 0;
        }
        sem_post(semaphore);
        if (!open) {
            break;
        }
        obsluzKomunikaty(mqidPetent, mqidOther, stan, semaphore, &obsluzeni);
        sterujBiletomatami(mqidOther, stan, semaphore);
        auto now = std::chrono::steady_clock::now();
        if (now - lastLog >= std::chrono::seconds(2)) {
            sem_wait(semaphore);
            logDev(stan, wygenerowani.load(), obsluzeni.load());
            sem_post(semaphore);
            std::cout << "{loader, " << getpid() << "} petenci_wygenerowani=" << wygenerowani.load()
                      << " petenci_obsluzeni=" << obsluzeni.load() << std::endl;
            lastLog = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    odrzucOczekujacychPetentow(mqidPetent, getpid());

    watekPetentow.join();

    killpg(getpgrp(), SIGINT);

    while (wait(nullptr) > 0);

    shmdt(stan);
    shmctl(shmid, IPC_RMID, nullptr);

    msgctl(mqidPetent, IPC_RMID, nullptr);
    msgctl(mqidOther, IPC_RMID, nullptr);

    sem_close(semaphore);
    sem_unlink(SEMAPHORE_NAME);

    std::cout << "Simulation finished." << std::endl;
    return 0;
}
