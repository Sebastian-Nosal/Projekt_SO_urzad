#ifndef MESSAGES_H
#define MESSAGES_H

#include <cstdint>

enum class MessageGroup : uint8_t {
    Loader,
    Petent,
    Biletomat,
    Monitoring
};

enum class ProcessMqType : long {
    Loader = 1,
    Biletomat = 2,
    Monitoring = 3,
    Kasa = 4,
    Dyrektor = 5
};

enum class DepartmentMqType : long {
    SA = 101,
    SC = 102,
    KM = 103,
    ML = 104,
    PD = 105
};

// Enumy dla typów wiadomości
enum class BiletomatMessagesEnum {
    Aktywuj,
    Dezaktywuj,
    PetentCzekaNaBilet,
    PetentOdebralBilet,
    WydanoBiletCzekaj,
    UrzednikWyczerpanyOdejdz,
    ZamykanieUrzeduOdejdz
};

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

enum class MonitoringMessagesEnum {
    Log,
    Error,
    Debug
};

enum class LoaderMessagesEnum {
    NowyPetent,
    PetentOpuszczaBudynek
};

// Struktura wiadomości
struct Message {
    long mtype;
    int senderId;
    int receiverId;
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