#include "geoprocessing.h"
#include "hashutils.h"
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

uint8_t parse_ceng_response(char *response, struct celltower *towers) {
    uint8_t count = 0;
    char *line = strtok(response, "\r\n");

    while (line != NULL && count < 7) {
        printf("Parsing line: %s\n", line); 

        if (count == 0 && strstr(line, "+CENG: 0") != NULL) {
            int tempMCC, tempMNC, tempLAC, tempCID, tempLevel;
            if (sscanf(line, "+CENG: 0,\"%*[^,],%d,%*[^,],%d,%d,%*d,%d,%*[^,],%*[^,],%x",
                       &tempLevel, &tempMCC, &tempMNC, &tempLAC, &tempCID) == 5) {
                towers[count].MCC = tempMCC;
                towers[count].MNC = tempMNC;
                towers[count].LAC = tempLAC;
                towers[count].CID = tempCID;
                towers[count].RECEIVELEVEL = tempLevel;
                count++;
                printf("Parsed main tower: MCC=%d, MNC=%d, LAC=%d, CID=%d, RECEIVELEVEL=%d\n",
                       tempMCC, tempMNC, tempLAC, tempCID, tempLevel);
            } else {
                printf("Failed to parse main tower.\n"); 
            }
        } 
        else if (strstr(line, "+CENG:") != NULL) {
            int tempMCC, tempMNC, tempLAC, tempCID, tempLevel;
            if (sscanf(line, "+CENG: %*d,\"%*[^,],%d,%*[^,],%x,%d,%d,%x",
                       &tempLevel, &tempCID, &tempMCC, &tempMNC, &tempLAC) >= 4) {
                if (tempMCC != 0xFFFF && tempMNC != 0xFFFF && tempLAC != 0xFFFF && tempCID != 0xFFFF) {
                    towers[count].MCC = tempMCC;
                    towers[count].MNC = tempMNC;
                    towers[count].LAC = tempLAC;
                    towers[count].CID = tempCID;
                    towers[count].RECEIVELEVEL = tempLevel;
                    count++;
                    printf("Parsed tower: MCC=%d, MNC=%d, LAC=%d, CID=%d, RECEIVELEVEL=%d\n",
                           tempMCC, tempMNC, tempLAC, tempCID, tempLevel);
                }
            } else {
                printf("Failed to parse line: %s\n", line); 
            }
        }

        line = strtok(NULL, "\r\n");
    }

    printf("Total parsed towers: %d\n", count); 
    return count;
}

double signal_to_distance(int16_t RECEIVELEVEL, double frequency) {
    double PL = RECEIVELEVEL; 
    double d = pow(10, (PL - 20 * log10(frequency) + 147.55) / 20);
    return d; 
}

// трилатерация
/*
struct Location trilaterate(struct celltower *towers, uint8_t towerCount, struct Node **hash_table) {
    double totalX = 0.0, totalY = 0.0;
    double totalWeight = 0.0;

    for (int i = 0; i < towerCount; i++) {
        struct Node *tower = search_in_hash_table(hash_table, towers[i].MCC, towers[i].MNC, towers[i].CID);
        
        if (tower == NULL) {
            fprintf(stderr, "Error finding tower location in database for tower %d.\n", i);
            continue; 
        }

        double distance = signal_to_distance(towers[i].RECEIVELEVEL, 900e6);
        
        if (distance <= 0) {
            fprintf(stderr, "Calculated distance is non-positive for tower %d. Skipping...\n", i);
            continue;
        }

        totalX += tower->LONG * distance;
        totalY += tower->LAT * distance;
        totalWeight += distance; 
    }

    if (totalWeight > 0) {
        totalX /= totalWeight;
        totalY /= totalWeight;
    } else {
        fprintf(stderr, "Total weight is zero, cannot calculate location.\n");
        return (struct Location){0, 0};
    }

    printf("Calculated location: LAT=%.6f, LONG=%.6f\n", totalY, totalX);
    return (struct Location){totalY, totalX};
}
*/
