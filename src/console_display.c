// console_display.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include "msg_definitions.h"
#include "geoprocessing.h"
#include <math.h>

#define SOCKET_PATH_DISPLAY "/tmp/display_socket"
#define DISPLAY_COUNT 7
#define MIN_TOWERS_REQUIRED 3
#define LOCATION_HISTORY_SIZE 10


typedef struct {
    long msg_type;
    uint16_t MCC;
    uint16_t MNC;
    uint32_t CID;
    int receive_level;
    float LAT;
    float LONG;
} tower_info_t;

struct Location locationHistory[LOCATION_HISTORY_SIZE];

// Добавить новую координату в начало массива, сдвинув старые
void update_location_history(struct Location newLocation) {
    for (int i = LOCATION_HISTORY_SIZE - 1; i > 0; i--) {
        locationHistory[i] = locationHistory[i - 1];
    }
    locationHistory[0] = newLocation;
}

// Функция трилатерации
struct Location trilaterate(tower_info_t *towers, uint8_t towerCount) {
    double totalX = 0.0, totalY = 0.0;
    double totalWeight = 0.0;

    for (int i = 0; i < towerCount; i++) {
        if (towers[i].receive_level == 0) {
            continue;  // Пропускаем вышки с нулевым уровнем сигнала
        }

        double distance = pow(10, (towers[i].receive_level - 20 * log10(900e6) + 147.55) / 20);  // Пример расчета дистанции
        totalX += towers[i].LONG * distance;
        totalY += towers[i].LAT * distance;
        totalWeight += distance;
    }

    struct Location location;
    if (totalWeight > 0) {
        location.latitude = totalY / totalWeight;
        location.longitude = totalX / totalWeight;
    } else {
        location.latitude = 0;
        location.longitude = 0;
    }

    return location;
}

int main() {
    printf("[DEBUG] Starting console_display server...\n");

    int server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Ошибка создания серверного сокета");
        exit(EXIT_FAILURE);
    }
    printf("[DEBUG] Server socket created.\n");

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH_DISPLAY, sizeof(addr.sun_path) - 1);

    unlink(SOCKET_PATH_DISPLAY);  // Удаление старого сокета, если он существует
    printf("[DEBUG] Old socket file unlinked if existed.\n");

    if (bind(server_socket, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("Ошибка связывания сокета с адресом");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    printf("[DEBUG] Socket bound to path: %s\n", SOCKET_PATH_DISPLAY);

    if (listen(server_socket, 5) == -1) {
        perror("Ошибка установки прослушивания на серверном сокете");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    printf("[DEBUG] Server is now listening for connections...\n");

    int client_socket = accept(server_socket, NULL, NULL);
    if (client_socket == -1) {
        perror("Ошибка принятия подключения на серверном сокете");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    printf("[DEBUG] Connection accepted on console_display server.\n");

    tower_info_t towers[DISPLAY_COUNT] = {0};
    int index = 0;

    while (1) {
        tower_info_t received_msg;
        ssize_t bytes_received = recv(client_socket, &received_msg, sizeof(received_msg), 0);

        printf("[DEBUG] Bytes received: %zd\n", bytes_received);
        if (bytes_received == -1) {
            perror("Ошибка получения данных из сокета");
            continue;
        } else if (bytes_received == 0) {
            printf("[DEBUG] Client disconnected, waiting for reconnection...\n");
            close(client_socket);
            client_socket = accept(server_socket, NULL, NULL);
            continue;
        }

        printf("[DEBUG] Received Message - Type: %ld, MCC: %d, MNC: %d, CID: %u, Receive Level: %d, LAT: %.6f, LONG: %.6f\n",
               received_msg.msg_type, received_msg.MCC, received_msg.MNC, received_msg.CID,
               received_msg.receive_level, received_msg.LAT, received_msg.LONG);

        if (received_msg.msg_type == 1) {
            towers[index] = received_msg;
            printf("[DEBUG] Storing tower data at index %d\n", index); // Отладка сохранения данных в массиве
            index = (index + 1) % DISPLAY_COUNT;

            // Проверка, если накоплено три или больше вышек для триангуляции
            if (index >= MIN_TOWERS_REQUIRED) {
                struct Location new_location = trilaterate(towers, index);
                update_location_history(new_location);
                printf("Calculated Position: LAT=%.6f, LONG=%.6f\n\n", new_location.latitude, new_location.longitude);
            }
        } else if (received_msg.msg_type == 2) {
            printf("\nTransmission Complete. Current Tower Data:\n");
            for (int i = 0; i < DISPLAY_COUNT; i++) {
                if (towers[i].LAT != 0.0 && towers[i].LONG != 0.0) {
                    printf("Tower %d: LAT=%.6f, LONG=%.6f\n", i + 1, towers[i].LAT, towers[i].LONG);
                }
            }
            // Финальное вычисление позиции по накопленным вышкам
            struct Location final_location = trilaterate(towers, DISPLAY_COUNT);
            update_location_history(final_location);
            printf("Final Calculated Position: LAT=%.6f, LONG=%.6f\n\n", final_location.latitude, final_location.longitude);
        }
    }

    close(client_socket);
    close(server_socket);
    unlink(SOCKET_PATH_DISPLAY);
    return 0;
}
