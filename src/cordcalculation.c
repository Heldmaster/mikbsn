#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>

#define SOCKET_PATH_DISPLAY "/tmp/display_socket"
#define DISPLAY_COUNT 7
#define MIN_TOWERS_REQUIRED 3
#define LOCATION_HISTORY_SIZE 10
#define EARTH_RADIUS 6371000.0  // Радиус Земли в метрах
#define SIGNAL_THRESHOLD 5     // Минимальный уровень сигнала
#define COORDINATE_CHANGE_THRESHOLD 0.00001 // Порог изменения координат для логирования

typedef struct {
    long msg_type;
    uint16_t MCC;
    uint16_t MNC;
    uint32_t CID;
    int receive_level;
    float LAT;
    float LONG;
} tower_info_t;

struct Location {
    double latitude;
    double longitude;
};

struct Location locationHistory[LOCATION_HISTORY_SIZE];
struct Location last_logged_location = {0.0, 0.0}; // Последнее залогированное местоположение

// Добавить новую координату в историю
void update_location_history(struct Location newLocation) {
    for (int i = LOCATION_HISTORY_SIZE - 1; i > 0; i--) {
        locationHistory[i] = locationHistory[i - 1];
    }
    locationHistory[0] = newLocation;
}

// Проверка на значительное изменение координат
int has_significant_location_change(struct Location new_location) {
    double lat_diff = fabs(new_location.latitude - last_logged_location.latitude);
    double lon_diff = fabs(new_location.longitude - last_logged_location.longitude);
    return (lat_diff > COORDINATE_CHANGE_THRESHOLD || lon_diff > COORDINATE_CHANGE_THRESHOLD);
}

// Логирование местоположения в файл
void log_location(struct Location location) {
    FILE *file = fopen("location_log.txt", "a");
    if (!file) {
        perror("Ошибка открытия файла для логирования");
        return;
    }

    time_t current_time = time(NULL);
    struct tm *time_info = localtime(&current_time);
    fprintf(file, "%04d-%02d-%02d %02d:%02d:%02d, LAT=%.6f, LONG=%.6f\n",
            time_info->tm_year + 1900, time_info->tm_mon + 1, time_info->tm_mday,
            time_info->tm_hour, time_info->tm_min, time_info->tm_sec,
            location.latitude, location.longitude);

    fclose(file);
    last_logged_location = location;
}

// Очищает массив вышек после использования
void clear_tower_data(tower_info_t *towers, int towerCount) {
    memset(towers, 0, sizeof(tower_info_t) * towerCount);
}

// Функция трилатерации
struct Location trilaterate(tower_info_t *towers, int towerCount) {
    double totalLat = 0.0, totalLon = 0.0;
    double totalWeight = 0.0;

    for (int i = 0; i < towerCount; i++) {
        // Пропускаем вышки с уровнем сигнала ниже порога
        if (towers[i].receive_level < SIGNAL_THRESHOLD) {
            continue;
        }

        // Вычисляем расстояние (в данном случае RSSI как вес)
        double weight = pow(10, towers[i].receive_level / 10.0); // Прямой вес на основе RSSI

        totalLat += towers[i].LAT * weight;
        totalLon += towers[i].LONG * weight;
        totalWeight += weight;
    }

    struct Location location;
    if (totalWeight > 0) {
        // Средневзвешенные значения широты и долготы
        location.latitude = totalLat / totalWeight;
        location.longitude = totalLon / totalWeight;
    } else {
        location.latitude = 0.0;
        location.longitude = 0.0;
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
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH_DISPLAY, sizeof(addr.sun_path) - 1);
    unlink(SOCKET_PATH_DISPLAY);

    if (bind(server_socket, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("Ошибка связывания сокета");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    if (listen(server_socket, 5) == -1) {
        perror("Ошибка прослушивания на сервере");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    int client_socket = accept(server_socket, NULL, NULL);
    if (client_socket == -1) {
        perror("Ошибка подключения клиента");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    tower_info_t towers[DISPLAY_COUNT] = {0};
    int tower_count = 0;
    int is_ready_for_calculation = 0;

    while (1) {
        tower_info_t received_msg;
        ssize_t bytes_received = recv(client_socket, &received_msg, sizeof(received_msg), 0);

        if (bytes_received == -1) {
            perror("Ошибка получения данных");
            continue;
        } else if (bytes_received == 0) {
            close(client_socket);
            client_socket = accept(server_socket, NULL, NULL);
            continue;
        }

        if (received_msg.msg_type == 1) {
            towers[tower_count++] = received_msg;

            if (tower_count >= MIN_TOWERS_REQUIRED) {
                is_ready_for_calculation = 1;
            }

            if (is_ready_for_calculation) {
                struct Location new_location = trilaterate(towers, tower_count);
                
                // Логируем только при значительном изменении координат
                if (has_significant_location_change(new_location)) {
                    update_location_history(new_location);
                    log_location(new_location);
                }

                // Очищаем данные для следующего расчета
                clear_tower_data(towers, DISPLAY_COUNT);
                tower_count = 0;
                is_ready_for_calculation = 0;
            }
        } else if (received_msg.msg_type == 2) {
            struct Location final_location = trilaterate(towers, tower_count);

            if (has_significant_location_change(final_location)) {
                update_location_history(final_location);
                log_location(final_location);
            }

            clear_tower_data(towers, DISPLAY_COUNT);
            tower_count = 0;
            is_ready_for_calculation = 0;
        }
    }

    close(client_socket);
    close(server_socket);
    unlink(SOCKET_PATH_DISPLAY);
    return 0;
}
