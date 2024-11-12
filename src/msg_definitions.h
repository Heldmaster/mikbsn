// msg_definitions.h
#ifndef MSG_DEFINITIONS_H
#define MSG_DEFINITIONS_H

#include <stdint.h>

#define MSG_KEY_REQUEST 1234
#define MSG_KEY_RESPONSE 5678

typedef struct {
    long msg_type;
    uint16_t MCC;
    uint16_t MNC;
    uint32_t CID;
} search_request_t;

typedef struct {
    long msg_type;
    float LAT;
    float LONG;
} search_response_t;

#endif // MSG_DEFINITIONS_H

