#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>
#include "geoprocessing.h"

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

// Преобразование градусов в радианы
double deg_to_rad(double deg) {
    return deg * M_PI / 180.0;
}

// Преобразование радианов в градусы
double rad_to_deg(double rad) {
    return rad * 180.0 / M_PI;
}

// Формула Хаверсина для вычисления расстояния между двумя точками
double haversine(double lat1, double lon1, double lat2, double lon2) {
    double dlat = deg_to_rad(lat2 - lat1);
    double dlon = deg_to_rad(lon2 - lon1);
    lat1 = deg_to_rad(lat1);
    lat2 = deg_to_rad(lat2);

    double a = sin(dlat / 2) * sin(dlat / 2) +
               cos(lat1) * cos(lat2) * sin(dlon / 2) * sin(dlon / 2);
    double c = 2 * atan2(sqrt(a), sqrt(1 - a));
    return EARTH_RADIUS * c;
}

// Преобразование сферических координат в трёхмерные
void spherical_to_cartesian(double lat, double lon, double *x, double *y, double *z) {
    lat = deg_to_rad(lat);
    lon = deg_to_rad(lon);
    *x = EARTH_RADIUS * cos(lat) * cos(lon);
    *y = EARTH_RADIUS * cos(lat) * sin(lon);
    *z = EARTH_RADIUS * sin(lat);
}

// Преобразование трёхмерных координат обратно в сферические
void cartesian_to_spherical(double x, double y, double z, double *lat, double *lon) {
    *lat = rad_to_deg(asin(z / EARTH_RADIUS));
    *lon = rad_to_deg(atan2(y, x));
}

// Основная функция трилатерации с отладочными сообщениями
struct Location trilaterate(tower_info_t *towers, int towerCount) {
    if (towerCount < 3) {
        printf("[ERROR] Not enough towers for trilateration (need at least 3, got %d)\n", towerCount);
        struct Location invalidLocation = {0.0, 0.0};
        return invalidLocation;
    }

    // Координаты и расстояния
    double x1, y1, z1, x2, y2, z2, x3, y3, z3;
    spherical_to_cartesian(towers[0].LAT, towers[0].LONG, &x1, &y1, &z1);
    spherical_to_cartesian(towers[1].LAT, towers[1].LONG, &x2, &y2, &z2);
    spherical_to_cartesian(towers[2].LAT, towers[2].LONG, &x3, &y3, &z3);

    double r1 = signal_to_distance(towers[0].receive_level, 1800);
    double r2 = signal_to_distance(towers[1].receive_level, 1800);
    double r3 = signal_to_distance(towers[2].receive_level, 1800);

    printf("[DEBUG] Cartesian coordinates:\n");
    printf("  Tower 1: x=%f, y=%f, z=%f, r1=%f\n", x1, y1, z1, r1);
    printf("  Tower 2: x=%f, y=%f, z=%f, r2=%f\n", x2, y2, z2, r2);
    printf("  Tower 3: x=%f, y=%f, z=%f, r3=%f\n", x3, y3, z3, r3);

    // Решение системы уравнений в 3D
    double ex[3], ey[3], ez[3], d, i, j, x, y, z;

    // Вектор между первой и второй точками
    ex[0] = x2 - x1;
    ex[1] = y2 - y1;
    ex[2] = z2 - z1;
    d = sqrt(ex[0] * ex[0] + ex[1] * ex[1] + ex[2] * ex[2]);
    printf("[DEBUG] Distance between Tower 1 and Tower 2: d=%f\n", d);

    for (int k = 0; k < 3; k++) ex[k] /= d;  // Нормализация
    printf("[DEBUG] Unit vector ex: x=%f, y=%f, z=%f\n", ex[0], ex[1], ex[2]);

    // Вектор от первой до третьей точки
    double t3[3] = {x3 - x1, y3 - y1, z3 - z1};
    i = ex[0] * t3[0] + ex[1] * t3[1] + ex[2] * t3[2];
    printf("[DEBUG] Projection of Tower 3 onto ex: i=%f\n", i);

    // Вектор ортогонален ex
    for (int k = 0; k < 3; k++) ey[k] = t3[k] - i * ex[k];
    j = sqrt(ey[0] * ey[0] + ey[1] * ey[1] + ey[2] * ey[2]);
    printf("[DEBUG] Distance orthogonal to ex (j): j=%f\n", j);

    for (int k = 0; k < 3; k++) ey[k] /= j;  // Нормализация
    printf("[DEBUG] Unit vector ey: x=%f, y=%f, z=%f\n", ey[0], ey[1], ey[2]);

    // Вектор, ортогональный ex и ey
    ez[0] = ex[1] * ey[2] - ex[2] * ey[1];
    ez[1] = ex[2] * ey[0] - ex[0] * ey[2];
    ez[2] = ex[0] * ey[1] - ex[1] * ey[0];
    printf("[DEBUG] Unit vector ez: x=%f, y=%f, z=%f\n", ez[0], ez[1], ez[2]);

    x = (r1 * r1 - r2 * r2 + d * d) / (2 * d);
    y = (r1 * r1 - r3 * r3 + i * i + j * j) / (2 * j) - (i / j) * x;
    z = sqrt(fabs(r1 * r1 - x * x - y * y));

    printf("[DEBUG] Calculated x=%f, y=%f, z=%f\n", x, y, z);

    // Перевод в глобальные координаты
    double result_x = x1 + x * ex[0] + y * ey[0] + z * ez[0];
    double result_y = y1 + x * ex[1] + y * ey[1] + z * ez[1];
    double result_z = z1 + x * ex[2] + y * ey[2] + z * ez[2];

    printf("[DEBUG] Result Cartesian coordinates: x=%f, y=%f, z=%f\n", result_x, result_y, result_z);

    struct Location result;
    cartesian_to_spherical(result_x, result_y, result_z, &result.latitude, &result.longitude);

    printf("[DEBUG] Calculated lat=%f, long=%f\n", result.latitude, result.longitude);
    log_location(result);
    return result;
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
                    //log_location(new_location);
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
                //log_location(final_location);
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
