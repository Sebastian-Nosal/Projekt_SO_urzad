/**
 * @file messages.h
 * @brief Definicje typów wiadomości i struktur IPC.
 */

#ifndef MESSAGES_H
#define MESSAGES_H

#include <cstdint>

/**
 * @brief Grupy komunikatów w systemie.
 */
enum class MessageGroup : uint8_t {
    Loader,
    Petent,
    Biletomat,
    Monitoring
};

/**
 * @brief Typy kolejek dla procesów.
 */
enum class ProcessMqType : long {
    Loader = 1,
    Biletomat = 2,
    Monitoring = 3,
    Kasa = 4,
    Dyrektor = 5
};

/**
 * @brief Typy kolejek dla wydziałów.
 */
enum class DepartmentMqType : long {
    SA = 101,
    SC = 102,
    KM = 103,
    ML = 104,
    PD = 105
};

/**
 * @brief Typy wiadomości dla biletomatu.
 */
enum class BiletomatMessagesEnum {
    Aktywuj,
    Dezaktywuj,
    PetentCzekaNaBilet,
    PetentOdebralBilet,
    WydanoBiletCzekaj,
    UrzednikWyczerpanyOdejdz,
    ZamykanieUrzeduOdejdz
};

/**
 * @brief Typy wiadomości dla petenta.
 */
enum class PetentMessagesEnum {
    WejdzDoBudynku,
    OtrzymanoBilet,
    WezwanoDoUrzednika,
    IdzDoKasy,
    IdzDoInnegoUrzednika,
    Obsluzony,
    Odprawiony,
    OpuscBudynek
};

/**
 * @brief Typy wiadomości monitoringu.
 */
enum class MonitoringMessagesEnum {
    Log,
    Error,
    Debug
};

/**
 * @brief Typy wiadomości loadera.
 */
enum class LoaderMessagesEnum {
    NowyPetent,
    PetentOpuszczaBudynek
};

/**
 * @brief Flaga VIP w polu `flags`.
 */
constexpr int MESSAGE_FLAG_VIP = 1;

/**
 * @brief Offset typu mtype dla kolejki VIP w wydziałach.
 */
constexpr long VIP_DEPT_MTYPE_OFFSET = 1000;

/**
 * @brief Struktura wiadomości przesyłanej przez kolejki IPC.
 */
struct Message {
    long mtype;
    int senderId;
    int receiverId;
    int replyQueueId;
    int flags;
    MessageGroup group;
    union {
        BiletomatMessagesEnum biletomatType;
        PetentMessagesEnum petentType;
        MonitoringMessagesEnum monitoringType;
        LoaderMessagesEnum loaderType;
    } messageType;
    int data1;
    int data2;
    char data3[258];
};

#endif // MESSAGES_H