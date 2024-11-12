#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/select.h>
#include "geoprocessing.h"
#include "hashutils.h"
#include "msg_definitions.h"
#include "config.h"

#define SOCKET_PATH "/tmp/gsm_socket"

size_t DBSIZE;

// Функция для отправки команды на SIM800
void send_command(int uart_fd, const char *command) {
    printf("Отправка команды: %s\n", command);
    if (write(uart_fd, command, strlen(command)) == -1) {
        perror("Ошибка при отправке команды на SIM800");
        exit(EXIT_FAILURE);
    }
}

// Функция для получения ответа от SIM800 с отладочным выводом
int read_response(int uart_fd, char *buffer, size_t buffer_size) {
    int total_bytes_read = 0;
    int bytes_read;

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(uart_fd, &read_fds);

    // Используем select() без таймаута для немедленной обработки поступающих данных
    int select_result = select(uart_fd + 1, &read_fds, NULL, NULL, NULL);
    if (select_result < 0) {
        perror("Ошибка select()");
        return -1;
    }

    if (FD_ISSET(uart_fd, &read_fds)) {
        // Читаем данные, если они доступны
        while ((bytes_read = read(uart_fd, buffer + total_bytes_read, buffer_size - total_bytes_read - 1)) > 0) {
            total_bytes_read += bytes_read;
        }
        if (bytes_read < 0 && errno != EAGAIN) {
            perror("Ошибка при чтении из UART");
            return -1;
        }
    }

    if (total_bytes_read == 0) {
        fprintf(stderr, "Ошибка: получен пустой ответ от SIM800\n");
        return -1;
    }

    buffer[total_bytes_read] = '\0';
    printf("Получен полный ответ от SIM800 (всего байт: %d):\n%s\n", total_bytes_read, buffer);
    return total_bytes_read;
}

int main() {
    // Настройка UART
    int uart_fd = open(SIM_UART_PATH, O_RDWR | O_NOCTTY | O_NDELAY);
    if (uart_fd == -1) {
        perror("Ошибка открытия UART");
        exit(EXIT_FAILURE);
    }
    printf("Opening UART on %s\n", SIM_UART_PATH); 

    struct termios options;
    tcgetattr(uart_fd, &options);
    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);
    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    tcsetattr(uart_fd, TCSANOW, &options);

    // Настройка UNIX-сокета для отправки данных
    int client_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client_socket == -1) {
        perror("Ошибка создания сокета");
        close(uart_fd);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(client_socket, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("Ошибка соединения с сокетом");
        close(client_socket);
        close(uart_fd);
        exit(EXIT_FAILURE);
    }

    // Буфер для данных
    char response_buffer[2048];
    struct celltower towers[7] = {0};

    while (1) {
        // Отправка команды AT+CENG? для получения информации о вышках
        send_command(uart_fd, "AT+CENG?\r");

        // Ожидание и чтение ответа от SIM800
        int response_length = read_response(uart_fd, response_buffer, sizeof(response_buffer));
        if (response_length <= 0) {
            fprintf(stderr, "Ошибка: получен пустой ответ от SIM800\n");
            continue;
        }

        // Парсинг ответа
        uint8_t parsed_count = parse_ceng_response(response_buffer, towers);
        printf("Количество распознанных вышек: %d\n", parsed_count);

        // Вывод информации о каждой распознанной вышке для отладки
        for (int i = 0; i < parsed_count; i++) {
            printf("Вышка %d: MCC=%d, MNC=%d, CID=%d, Уровень сигнала=%d\n",
                   i + 1, towers[i].MCC, towers[i].MNC, towers[i].CID, towers[i].RECEIVELEVEL);
        }

        // Отправка данных о вышках через UNIX-сокет на сервер
        for (int i = 0; i < parsed_count; i++) {
            struct {
                uint16_t MCC;
                uint16_t MNC;
                uint32_t CID;
                int receive_level;
            } level_data;

            level_data.MCC = towers[i].MCC;
            level_data.MNC = towers[i].MNC;
            level_data.CID = towers[i].CID;
            level_data.receive_level = towers[i].RECEIVELEVEL;

            printf("Отправка данных вышки через сокет: MCC=%d, MNC=%d, CID=%d, Уровень сигнала=%d\n",
                   level_data.MCC, level_data.MNC, level_data.CID, level_data.receive_level);

            if (send(client_socket, &level_data, sizeof(level_data), 0) == -1) {
                perror("Ошибка при отправке данных через сокет");
                close(client_socket);
                close(uart_fd);
                exit(EXIT_FAILURE);
            }
        }

        // Пауза перед следующим запросом
        sleep(5);
    }

    close(client_socket);
    close(uart_fd);
    return 0;
}
