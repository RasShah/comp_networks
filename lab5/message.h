#pragma once

#include <stdint.h>
#include <time.h>

#define MAX_NAME     32
#define MAX_PAYLOAD  256
#define MAX_TIME_STR 32

// Расширенный формат сообщения для ЛР5
typedef struct {
    uint32_t length;                 // длина payload с учетом '\0'
    uint8_t  type;                   // тип сообщения
    uint32_t msg_id;                 // уникальный идентификатор сообщения
    char     sender[MAX_NAME];       // ник отправителя
    char     receiver[MAX_NAME];     // ник получателя или "" для broadcast
    time_t   timestamp;              // время создания
    char     payload[MAX_PAYLOAD];   // текст / данные команды
} MessageEx;

enum {
    MSG_HELLO        = 1,
    MSG_WELCOME      = 2,
    MSG_TEXT         = 3,
    MSG_PING         = 4,
    MSG_PONG         = 5,
    MSG_BYE          = 6,

    MSG_AUTH         = 7,
    MSG_PRIVATE      = 8,
    MSG_ERROR        = 9,
    MSG_SERVER_INFO  = 10,

    MSG_LIST         = 11,
    MSG_HISTORY      = 12,
    MSG_HISTORY_DATA = 13,
    MSG_HELP         = 14
};

static inline const char* messageTypeToString(uint8_t type) {
    switch (type) {
        case MSG_HELLO:        return "MSG_HELLO";
        case MSG_WELCOME:      return "MSG_WELCOME";
        case MSG_TEXT:         return "MSG_TEXT";
        case MSG_PING:         return "MSG_PING";
        case MSG_PONG:         return "MSG_PONG";
        case MSG_BYE:          return "MSG_BYE";
        case MSG_AUTH:         return "MSG_AUTH";
        case MSG_PRIVATE:      return "MSG_PRIVATE";
        case MSG_ERROR:        return "MSG_ERROR";
        case MSG_SERVER_INFO:  return "MSG_SERVER_INFO";
        case MSG_LIST:         return "MSG_LIST";
        case MSG_HISTORY:      return "MSG_HISTORY";
        case MSG_HISTORY_DATA: return "MSG_HISTORY_DATA";
        case MSG_HELP:         return "MSG_HELP";
        default:               return "MSG_UNKNOWN";
    }
}
