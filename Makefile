# Пути
SRC_DIR = src
BUILD_DIR = build

# Объявление целей
all: $(BUILD_DIR) $(BUILD_DIR)/cordcalculation $(BUILD_DIR)/dbsearch $(BUILD_DIR)/sim_handler

# Создание папки для сборки
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Сборка cordcalculation
$(BUILD_DIR)/cordcalculation: $(SRC_DIR)/cordcalculation.c $(SRC_DIR)/geoprocessing.c
	gcc $(SRC_DIR)/cordcalculation.c $(SRC_DIR)/geoprocessing.c -o $(BUILD_DIR)/cordcalculation -lm

# Сборка dbsearch
$(BUILD_DIR)/dbsearch: $(SRC_DIR)/dbsearch.c $(SRC_DIR)/hashutils.c
	gcc $(SRC_DIR)/dbsearch.c $(SRC_DIR)/hashutils.c -o $(BUILD_DIR)/dbsearch

# Сборка sim_handler (учитывает изменения config.h)
$(BUILD_DIR)/sim_handler: $(SRC_DIR)/sim_handler.c $(SRC_DIR)/geoprocessing.c $(SRC_DIR)/hashutils.c $(SRC_DIR)/config.h
	gcc $(SRC_DIR)/sim_handler.c $(SRC_DIR)/geoprocessing.c $(SRC_DIR)/hashutils.c -o $(BUILD_DIR)/sim_handler -lm

# Очистка
clean:
	rm -rf $(BUILD_DIR)

# Запуск программ в отдельных терминалах
run: all
	@echo "Запуск cordcalculation в новом терминале..."
	gnome-terminal -- bash -c "$(BUILD_DIR)/cordcalculation; exec bash" &
	@echo "Запуск dbsearch в новом терминале..."
	gnome-terminal -- bash -c "$(BUILD_DIR)/dbsearch; exec bash" &
	@echo "Запуск sim_handler в новом терминале..."
	gnome-terminal -- bash -c "$(BUILD_DIR)/sim_handler; exec bash" &

