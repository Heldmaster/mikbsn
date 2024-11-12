#ifndef GEOPROCESSING_H
#define GEOPROCESSING_H

#include <stdint.h>
#include <stdlib.h>
#include "hashutils.h"
#include <math.h>
// Структуры данных
/*
struct coords {
    uint16_t H, M, S, MS; // Время
    float ALT, LONG;      // Высота и долгота
};
*/


struct Location {
    double latitude;
    double longitude;
};

struct celltower {
    uint16_t MCC;      // Код страны
    uint16_t MNC;      // Код оператора
    uint16_t LAC;      // Код региона
    uint32_t CID;      // CellID
    int16_t RECEIVELEVEL;
};

uint8_t parse_ceng_response(char *response, struct celltower *);
//struct Location trilaterate(struct celltower *towers, uint8_t towerCount, struct Node **hash_table);

#endif
