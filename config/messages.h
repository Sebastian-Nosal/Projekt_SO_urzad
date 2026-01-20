#ifndef MESSAGES_H
#define MESSAGES_H

#include <cstdint>

// Enumy dla typów wiadomości
enum class BiletomatMessagesEnum {
    WydanoBiletCzekaj,
    UrzednikWyczerpanyOdejdz,
    ZamykanieUrzeduOdejdz
};

enum class PetentMessagesEnum {
    WejdzDoBudynku,
    OtrzymanoBilet,
    WezwanoDoUrzednika,
    IdzDoKasy,
    IdzDoInnegoUrzędnika,
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
    int senderId; 
    int receiverId;
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