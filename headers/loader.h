#ifndef LOADER_H
#define LOADER_H

#include "../config.h"

void start_simulation(int liczba_petentow);
#include <csignal>

extern volatile pid_t g_dyrektor_pid;
extern volatile sig_atomic_t g_pending_sigint;

// Globalne PIDs dla zarządzania procesami
extern pid_t* g_all_pids;
extern int g_all_pids_count;

#endif
