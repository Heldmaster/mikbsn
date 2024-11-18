// main_process.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "hashutils.h"
#include "msg_definitions.h"

#define SOCKET_PATH "/tmp/gsm_socket"
#define SOCKET_PATH_DISPLAY "/tmp/display_socket"

struct display_message {
    long msg_type;
    uint16_t MCC;
    uint16_t MNC;
    uint32_t CID;
    int receive_level;
    float LAT;
    float LONG;
};

size_t DBSIZE;

int main() {
    char *file = "250.csv";
    DBSIZE = count_db_lines(file);
    struct Node **hash_table = (struct Node **)calloc(DBSIZE, sizeof(struct Node));
    if (!hash_table) {
        perror("Failed to allocate memory for hash table");
        exit(EXIT_FAILURE);
    }
    parse_and_insert_db(file, hash_table);
    printf("Hash table created and waiting for requests...\n");

    // Создаем серверный сокет
    int server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("socket creation failed");
        free(hash_table);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);
    unlink(SOCKET_PATH);

    if (bind(server_socket, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind failed");
        close(server_socket);
        free(hash_table);
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, 5) == -1) {
        perror("listen failed");
        close(server_socket);
        free(hash_table);
        exit(EXIT_FAILURE);
    }

    // Создаем клиентский сокет для передачи данных в console_display
    int display_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (display_socket == -1) {
        perror("Ошибка создания сокета для console_display");
        free(hash_table);
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_un display_addr;
    memset(&display_addr, 0, sizeof(display_addr));
    display_addr.sun_family = AF_UNIX;
    strncpy(display_addr.sun_path, SOCKET_PATH_DISPLAY, sizeof(display_addr.sun_path) - 1);

    if (connect(display_socket, (struct sockaddr*)&display_addr, sizeof(display_addr)) == -1) {
        perror("Ошибка соединения с сокетом console_display");
        close(display_socket);
        free(hash_table);
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    while (1) {
        int client_socket = accept(server_socket, NULL, NULL);
        if (client_socket == -1) {
            perror("accept failed");
            continue;
        }

        while (1) {
            struct {
                uint16_t MCC;
                uint16_t MNC;
                uint32_t CID;
                int receive_level;
            } level_data;

            ssize_t bytes_received = recv(client_socket, &level_data, sizeof(level_data), 0);
            if (bytes_received == -1) {
                perror("recv failed");
                break;
            } else if (bytes_received == 0) {
                // Клиент завершил соединение
                break;
            }

            printf("Received data: MCC=%d, MNC=%d, CID=%u, receive_level=%d\n",
                   level_data.MCC, level_data.MNC, level_data.CID, level_data.receive_level);

            struct Node *result = search_in_hash_table(hash_table, level_data.MCC, level_data.MNC, level_data.CID);
            struct display_message msg = {
                .msg_type = 1,
                .MCC = level_data.MCC,
                .MNC = level_data.MNC,
                .CID = level_data.CID,
                .receive_level = level_data.receive_level,
                .LAT = result ? result->LAT : 0.0,
                .LONG = result ? result->LONG : 0.0
            };

            if (send(display_socket, &msg, sizeof(msg), 0) == -1) {
                perror("Ошибка отправки данных через сокет display");
            } else {
                printf("Sent to console_display: LAT=%.6f, LONG=%.6f\n", msg.LAT, msg.LONG);
            }
        }

        // Отправка сигнала окончания передачи
        struct display_message end_msg = {.msg_type = 2, .MCC = 0, .MNC = 0, .CID = 0, .receive_level = 0, .LAT = 0.0, .LONG = 0.0};
        if (send(display_socket, &end_msg, sizeof(end_msg), 0) == -1) {
            perror("Ошибка отправки конечного сигнала через сокет display");
        }
        close(client_socket);
    }

    close(display_socket); // Закрываем сокет display при завершении
    free(hash_table);
    close(server_socket);
    unlink(SOCKET_PATH);
    return 0;
}
