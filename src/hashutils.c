#include "hashutils.h"
#include <stdio.h>
#include <stdlib.h>

extern size_t DBSIZE;

// Хеш-функция
uint64_t hash_function(uint16_t MCC, uint16_t MNC, uint32_t CID) {
    return ((MCC + MNC + CID) % DBSIZE);
}

// Подсчет строк в БД
size_t count_db_lines(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Cant open file: %s\n", filename);
        return 0;
    }
    size_t count = 0;
    char line[256];
    fgets(line, sizeof(line), file);
    while (fgets(line, sizeof(line), file)) {
        count++;
    }
    fclose(file);
    return count;
}

// Парсинг файла БД и вставка в хеш-таблицу
void parse_and_insert_db(const char *filename, struct Node **hash_table) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Cant open file: %s\n", filename);
        return;
    }

    char line[256];
    // Пропускаем первую строку с заголовком
    //fgets(line, sizeof(line), file);

    while (fgets(line, sizeof(line), file)) {
        uint16_t MCC, MNC, LAC;
        uint32_t CID;
        float LAT, LON;
		printf("Parsing line: %s", line );
        sscanf(line, "%*[^,],%hu,%hu,%hu,%u,%*d,%f,%f,%*d,%*d,%*d,%*d,%*d,%*d", 
               &MCC, &MNC, &LAC, &CID, &LON, &LAT);

        insert_into_hash_table(hash_table, MCC, MNC, CID, LAT, LON);
    }

    fclose(file);
}

// Вставка новой вышки в хеш-таблицу
void insert_into_hash_table(struct Node **hash_table, uint16_t MCC, uint16_t MNC, uint32_t CID, float LAT, float LONG) {
    unsigned int index = hash_function(MCC, MNC, CID);
    
    struct Node *new_Node = (struct Node *) malloc(sizeof(struct Node));
    if (!new_Node) {
        fprintf(stderr, "memory allocation error\n");
        return;
    }
    new_Node->MCC = MCC;
    new_Node->MNC = MNC;
    new_Node->LAC = 0; //LAC оффнул для надежности
    new_Node->CID = CID;
    new_Node->LAT = LAT;
    new_Node->LONG = LONG;
    new_Node->next = NULL;

    if (hash_table[index] == NULL) {
        hash_table[index] = new_Node;
    } else {
        new_Node->next = hash_table[index];
        hash_table[index] = new_Node;
    }
}

// Поиск вышки в хеш-таблице
struct Node *search_in_hash_table(struct Node **hash_table, uint16_t MCC, uint16_t MNC, uint32_t CID) {
    unsigned int index = hash_function(MCC, MNC, CID);
    struct Node *current = hash_table[index];
    while (current != NULL) {
        if (current->MCC == MCC && current->MNC == MNC && current->CID == CID) {
            return current;
        }
        current = current->next;
    }
    fprintf(stderr, "Data not found.\n");
    return NULL;
}

// Очистка памяти хеш-таблицы
void free_hash_table(struct Node **hash_table, size_t size) {
    for (size_t i = 0; i < size; i++) {
        struct Node *current = hash_table[i];
        while (current != NULL) {
            struct Node *temp = current;
            current = current->next;
            free(temp);
        }
    }
    free(hash_table);
}
