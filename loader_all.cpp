#include <iostream>
#include <thread>
#include <chrono>
#include <ctime>
#include <csignal>
#include <unistd.h> // fork() and exec()
#include <cerrno>
#include <cstring>
#include <atomic>
#include <vector>
#include <mutex>
#include <deque>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <semaphore.h> // POSIX semaphores
#include <fcntl.h> // O_CREAT
#include <sys/stat.h> // permissions
#include "utils/losowosc.h"
#include "utils/mq_semaphore.h"
#include "utils/sem_log.h"
#include "config/config_all.h"
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
        return pid;
    }
    return -1;
}

void logDev(const SharedState* stan, int wygenerowani, int obsluzeni) {
    int poza = stan->livePetents - stan->clientsInBuilding;
    if (poza < 0) poza = 0;
}

void uruchomUrzednikow(std::vector<pid_t>& pids) {
    for (int i = 0; i < NUM_SA_OFFICERS; ++i) {
        pids.push_back(uruchomProces("workers/urzednik/urzednik", "SA"));
    }

    for (int i = 0; i < NUM_SC_OFFICERS; ++i) {
        pids.push_back(uruchomProces("workers/urzednik/urzednik", "SC"));
    }

    for (int i = 0; i < NUM_KM_OFFICERS; ++i) {
        pids.push_back(uruchomProces("workers/urzednik/urzednik", "KM"));
    }

    for (int i = 0; i < NUM_ML_OFFICERS; ++i) {
        pids.push_back(uruchomProces("workers/urzednik/urzednik", "ML"));
    }

    for (int i = 0; i < NUM_PD_OFFICERS; ++i) {
        pids.push_back(uruchomProces("workers/urzednik/urzednik", "PD"));
    }
}

void sterujBiletomatami(int mqidOther, SharedState* stan, sem_t* semaphore, sem_t* otherQueueSem) {
    int dlugoscKolejki = 0;
    int aktywne = 0;

    semWaitLogged(semaphore, SEMAPHORE_NAME, __func__);
    dlugoscKolejki = stan->ticketQueueLen;
    aktywne = stan->activeTicketMachines;
    semPostLogged(semaphore, SEMAPHORE_NAME, __func__);

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

        if (!otherQueueWaitToSend(otherQueueSem)) {
            return;
        }
        if (msgsnd(mqidOther, &ctrl, sizeof(ctrl) - sizeof(long), 0) == -1) {
            perror("msgsnd failed");
            otherQueueReleaseSlot(otherQueueSem);
        } else {
            semWaitLogged(semaphore, SEMAPHORE_NAME, __func__);
            stan->activeTicketMachines = docelowe;
            semPostLogged(semaphore, SEMAPHORE_NAME, __func__);
        }
    }
}

void stworzWszystkichPetentow(SharedState* stan, sem_t* semaphore, std::atomic<int>* wygenerowani,
    std::vector<pid_t>* petentPids, std::mutex* petentMutex) {
    for (int i = 0; i < PETENTS_AMOUNT; ++i) {
        if (g_shutdown) {
            break;
        }
        semWaitLogged(semaphore, SEMAPHORE_NAME, __func__);
        int openNow = stan->officeOpen;
        semPostLogged(semaphore, SEMAPHORE_NAME, __func__);
        if (!openNow) {
            break;
        }
        pid_t pid = uruchomProces("workers/petent/petent");
        if (pid > 0 && petentPids && petentMutex) {
            std::lock_guard<std::mutex> lock(*petentMutex);
            petentPids->push_back(pid);
        }
        ++(*wygenerowani);
        semWaitLogged(semaphore, SEMAPHORE_NAME, __func__);
        stan->livePetents += 1;
        semPostLogged(semaphore, SEMAPHORE_NAME, __func__);
    }
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
    sem_unlink(SEMAPHORE_NAME);
    sem_t* semaphore = sem_open(SEMAPHORE_NAME, O_CREAT, 0666, 1);
    if (semaphore == SEM_FAILED) {
        perror("sem_open failed");
        exit(EXIT_FAILURE);
    }
    return semaphore;
}

void obsluzKomunikaty(int mqidPetent, int mqidPetentExit, int mqidOther, SharedState* stan, sem_t* semaphore, sem_t* otherQueueSem, sem_t* petentQueueSem, std::atomic<int>* obsluzeni) {
    Message msg;
    const long entryType = getpid();
    const long exitType = getpid() + 1;
    static std::deque<Message> pendingPetentOut;

    auto flushPending = [&]() {
        while (!pendingPetentOut.empty()) {
            Message out = pendingPetentOut.front();
            if (out.replyQueueId <= 0) {
                pendingPetentOut.pop_front();
                continue;
            }
            if (msgsnd(out.replyQueueId, &out, sizeof(out) - sizeof(long), 0) == -1) {
                perror("msgsnd failed");
                pendingPetentOut.pop_front();
                continue;
            }
            pendingPetentOut.pop_front();
        }
    };

    flushPending();

    while (true) {
        if (msgrcv(mqidPetentExit, &msg, sizeof(msg) - sizeof(long), exitType, IPC_NOWAIT) != -1) {
            petentQueueReleaseSlot(petentQueueSem);
            if (msg.group != MessageGroup::Loader) {
                continue;
            }

            if (msg.messageType.loaderType == LoaderMessagesEnum::PetentOpuszczaBudynek) {
                int zajeteMiejsca = msg.data2;
                if (zajeteMiejsca <= 0) {
                    zajeteMiejsca = 0;
                }
                semWaitLogged(semaphore, SEMAPHORE_NAME, __func__);
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
                semPostLogged(semaphore, SEMAPHORE_NAME, __func__);
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

    semWaitLogged(semaphore, SEMAPHORE_NAME, __func__);
    int miejsca = MAX_CLIENTS_IN_BUILDING - stan->clientsInBuilding;
    semPostLogged(semaphore, SEMAPHORE_NAME, __func__);

    if (miejsca > 0) {
        while (true) {
            if (msgrcv(mqidPetent, &msg, sizeof(msg) - sizeof(long), entryType, IPC_NOWAIT) != -1) {
                petentQueueReleaseSlot(petentQueueSem);
                if (msg.group != MessageGroup::Loader) {
                    continue;
                }

                if (msg.messageType.loaderType == LoaderMessagesEnum::NowyPetent) {
                    int zajeteMiejsca = msg.data2;
                    if (zajeteMiejsca <= 0) {
                        zajeteMiejsca = 1;
                    }

                    semWaitLogged(semaphore, SEMAPHORE_NAME, __func__);
                    if (stan->clientsInBuilding + zajeteMiejsca <= MAX_CLIENTS_IN_BUILDING) {
                        stan->clientsInBuilding += zajeteMiejsca;
                        semPostLogged(semaphore, SEMAPHORE_NAME, __func__);

                        Message response{};
                        response.mtype = 1;
                        response.senderId = getpid();
                        response.receiverId = msg.senderId;
                        response.replyQueueId = msg.replyQueueId;
                        response.group = MessageGroup::Petent;
                        response.messageType.petentType = PetentMessagesEnum::WejdzDoBudynku;

                        if (msg.replyQueueId <= 0) {
                            continue;
                        }
                        if (msgsnd(msg.replyQueueId, &response, sizeof(response) - sizeof(long), 0) == -1) {
                            perror("msgsnd failed");
                            pendingPetentOut.push_back(response);
                        }
                    } else {
                        semPostLogged(semaphore, SEMAPHORE_NAME, __func__);
                        Message response{};
                        response.mtype = 1;
                        response.senderId = getpid();
                        response.receiverId = msg.senderId;
                        response.replyQueueId = msg.replyQueueId;
                        response.group = MessageGroup::Petent;
                        response.messageType.petentType = PetentMessagesEnum::Odprawiony;
                        if (msg.replyQueueId > 0) {
                            if (msgsnd(msg.replyQueueId, &response, sizeof(response) - sizeof(long), 0) == -1) {
                                perror("msgsnd failed");
                                pendingPetentOut.push_back(response);
                            }
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
            otherQueueReleaseSlot(otherQueueSem);
            if (msg.group == MessageGroup::Biletomat) {
                if (msg.messageType.biletomatType == BiletomatMessagesEnum::PetentCzekaNaBilet) {
                    semWaitLogged(semaphore, SEMAPHORE_NAME, __func__);
                    stan->ticketQueueLen += 1;
                    semPostLogged(semaphore, SEMAPHORE_NAME, __func__);
                } else if (msg.messageType.biletomatType == BiletomatMessagesEnum::PetentOdebralBilet) {
                    semWaitLogged(semaphore, SEMAPHORE_NAME, __func__);
                    if (stan->ticketQueueLen > 0) {
                        stan->ticketQueueLen -= 1;
                    }
                    semPostLogged(semaphore, SEMAPHORE_NAME, __func__);
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

void odrzucOczekujacychPetentow(int mqidPetent, int loaderPid, sem_t* petentQueueSem) {
    Message msg;
    while (true) {
        if (msgrcv(mqidPetent, &msg, sizeof(msg) - sizeof(long), loaderPid, IPC_NOWAIT) != -1) {
            petentQueueReleaseSlot(petentQueueSem);
            if (msg.group != MessageGroup::Loader) {
                continue;
            }
            if (msg.messageType.loaderType != LoaderMessagesEnum::NowyPetent) {
                continue;
            }

            Message response{};
            response.mtype = 1;
            response.senderId = loaderPid;
            response.receiverId = msg.senderId;
            response.replyQueueId = msg.replyQueueId;
            response.group = MessageGroup::Petent;
            response.messageType.petentType = PetentMessagesEnum::Odprawiony;

            if (msg.replyQueueId > 0) {
                if (msgsnd(msg.replyQueueId, &response, sizeof(response) - sizeof(long), 0) == -1) {
                    perror("msgsnd failed");
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

int main() {
    std::signal(SIGUSR1, SIG_IGN);
    std::signal(SIGUSR2, SIG_IGN);
    std::signal(SIGINT, obsluzSigint);
    std::signal(SIGTERM, obsluzSigint);
    setpgid(0, 0);

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
    for (int i = 0; i < 5; ++i) {
        stan->exhaustedCount[i] = 0;
        stan->exhaustedDept[i] = 0;
    }

    int mqidPetent = initMessageQueue(MQ_KEY_ENTRY);      // entry queue
    int mqidOther = initMessageQueue(MQ_KEY_OTHER);       // other processes queue
    int mqidPetentExit = initMessageQueue(MQ_KEY_EXIT);
    setPetentQueueId(mqidPetent);
    setOtherQueueId(mqidOther);

    sem_t* semaphore = initSemaphore();
    sem_t* otherQueueSem = openOtherQueueSemaphore(true);
    sem_t* petentQueueSem = openPetentQueueSemaphore(true);

    pid_t monitoringPid = uruchomProces("monitoring");
    std::vector<pid_t> workerPids;
    workerPids.push_back(uruchomProces("workers/dyrektor/dyrektor"));
    uruchomUrzednikow(workerPids);
    workerPids.push_back(uruchomProces("workers/kasa/kasa"));
    for (int i = 0; i < TICKET_MACHINES_MAX; ++i) {
        workerPids.push_back(uruchomProces("workers/biletomat/biletomat", std::to_string(i)));
    }

    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::atomic<int> wygenerowani{0};
    std::atomic<int> obsluzeni{0};
    std::vector<pid_t> petentPids;
    std::mutex petentMutex;
    std::thread watekPetentow(stworzWszystkichPetentow, stan, semaphore, &wygenerowani, &petentPids, &petentMutex);

    // Main MQ loop
    auto lastLog = std::chrono::steady_clock::now();
    auto timeStart = std::chrono::steady_clock::now();
    bool shutdownSignalsSent = false;
    while (true) {
        reapZombies();
        semWaitLogged(semaphore, SEMAPHORE_NAME, __func__);
        int open = stan->officeOpen;
        if (std::chrono::steady_clock::now() - timeStart >= std::chrono::seconds(SIMULATION_DURATION)) {
            stan->officeOpen = 0;
            open = 0;
        }
        if (g_shutdown) {
            stan->officeOpen = 0;
            open = 0;
        }
        semPostLogged(semaphore, SEMAPHORE_NAME, __func__);
        if (!open && !shutdownSignalsSent) {
            shutdownSignalsSent = true;
            kill(0, SIGUSR2);
        }
        if (!open) {
            break;
        }
        obsluzKomunikaty(mqidPetent, mqidPetentExit, mqidOther, stan, semaphore, otherQueueSem, petentQueueSem, &obsluzeni);
        sterujBiletomatami(mqidOther, stan, semaphore, otherQueueSem);
        auto now = std::chrono::steady_clock::now();
        if (now - lastLog >= std::chrono::seconds(2)) {
            semWaitLogged(semaphore, SEMAPHORE_NAME, __func__);
            logDev(stan, wygenerowani.load(), obsluzeni.load());
            semPostLogged(semaphore, SEMAPHORE_NAME, __func__);
            lastLog = now;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    odrzucOczekujacychPetentow(mqidPetent, getpid(), petentQueueSem);

    if (watekPetentow.joinable()) {
        watekPetentow.join();
    }

    if (TIME_FRUSTRATED > 0) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(TIME_FRUSTRATED);
        while (std::chrono::steady_clock::now() < deadline) {
            reapZombies();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    {
        std::lock_guard<std::mutex> lock(petentMutex);
        for (pid_t pid : petentPids) {
            if (pid > 0) {
                kill(pid, SIGTERM);
            }
        }
    }

    for (pid_t pid : workerPids) {
        if (pid > 0) {
            kill(pid, SIGTERM);
        }
    }
    if (monitoringPid > 0) {
        kill(monitoringPid, SIGTERM);
    }

    std::vector<pid_t> remaining = workerPids;
    while (!remaining.empty()) {
        int status = 0;
        reapZombies();
        for (auto it = remaining.begin(); it != remaining.end(); ) {
            pid_t pid = *it;
            if (pid <= 0) {
                it = remaining.erase(it);
                continue;
            }
            pid_t w = waitpid(pid, &status, WNOHANG);
            if (w == pid) {
                it = remaining.erase(it);
                continue;
            }
            if (w == -1 && errno != EINTR) {
                it = remaining.erase(it);
                continue;
            }
            ++it;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    if (monitoringPid > 0) {
        waitpid(monitoringPid, nullptr, 0);
    }

    int status = 0;
    while (true) {
        pid_t w = waitpid(-1, &status, 0);
        if (w > 0) {
            continue;
        }
        if (w == -1 && errno == EINTR) {
            continue;
        }
        break;
    }

    shmdt(stan);
    shmctl(shmid, IPC_RMID, nullptr);

    msgctl(mqidPetent, IPC_RMID, nullptr);
    msgctl(mqidOther, IPC_RMID, nullptr);
    msgctl(mqidPetentExit, IPC_RMID, nullptr);

    sem_close(semaphore);
    sem_unlink(SEMAPHORE_NAME);
    closeOtherQueueSemaphore(otherQueueSem);
    sem_unlink(OTHER_QUEUE_SEM_NAME);
    closePetentQueueSemaphore(petentQueueSem);
    sem_unlink(PETENT_QUEUE_SEM_NAME);

    std::cout << "Simulation finished." << std::endl;
    return 0;
}
