#pragma once

#include <string>

#ifndef ZAPIS_FILE
#define ZAPIS_FILE "logi_symulacji.txt"
#endif

/**
 * @brief Zapisuje wiadomość do logu symulacji.
 * 
 * @param worker Nazwa pracownika, który zapisuje log.
 * @param worker_id ID pracownika zapisującego log.
 * @param wiadomosc Treść wiadomości do zapisania w logu.
 */
void zapisz_log(const std::string& worker, int worker_id, const std::string& wiadomosc);

/**
 * @brief Zapisuje wiadomość do logu symulacji.
 * 
 * @param worker Nazwa pracownika, który zapisuje log (w formacie C-string).
 * @param worker_id ID pracownika zapisującego log.
 * @param wiadomosc Treść wiadomości do zapisania w logu.
 */
void zapisz_log(const char* worker, int worker_id, const std::string& wiadomosc);