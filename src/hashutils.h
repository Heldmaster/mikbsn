#ifndef HASHUTILS_H
#define HASHUTILS_H

#include <stdint.h>
#include <stdlib.h>

extern size_t DBSIZE;

struct Node {
    uint16_t MCC;      // Код страны
    uint16_t MNC;      // Код оператора
    uint16_t LAC;      // Код региона
    uint32_t CID;      // CellID
    float LAT, LONG;   // Долгота и широта
    struct Node *next; 
};

uint64_t hash_function(uint16_t MCC, uint16_t MNC, uint32_t CID);
size_t count_db_lines(const char *filename);
void parse_and_insert_db(const char *filename, struct Node **hash_table);
void insert_into_hash_table(struct Node **hash_table, uint16_t MCC, uint16_t MNC, uint32_t CID, float ALT, float LONG);
struct Node *search_in_hash_table(struct Node **hash_table, uint16_t MCC, uint16_t MNC, uint32_t CID);
void free_hash_table(struct Node **hash_table, size_t size);
#endif 
