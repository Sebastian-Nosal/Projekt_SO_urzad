/**
 * @file petent.h
 * @brief Deklaracje interfejsu procesu petenta.
 */

#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>

/**
 * @brief Kolejka logów z udziałem dziecka petenta.
 */
struct ChildLogQueue {
	std::mutex mutex;
	std::condition_variable cv;
	std::deque<std::string> queue;
	bool done = false;
};

/**
 * @brief Wysyła log z adnotacją dziecka petenta.
 * @param mqidOther ID kolejki „other”.
 * @param senderId PID petenta.
 * @param text Treść logu.
 * @param maDziecko Czy petent ma dziecko.
 * @param childState Stan kolejki dziecka.
 */
void logujZAdnotacja(int mqidOther, int senderId, const std::string& text, bool maDziecko, ChildLogQueue* childState);

/**
 * @brief Obsługa sygnału wymuszonego wyjścia.
 * @param sig Numer sygnału.
 */
void obsluzSigusr2(int sig);

/**
 * @brief Punkt wejścia procesu petenta.
 */
int main();
