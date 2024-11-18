import sys
import pandas as pd
import numpy as np
import os
import serial
from PyQt5 import QtWidgets, QtGui, QtCore
import pyqtgraph as pg
import glob
import time
import threading
import math

MAP_IMAGE_PATH = "1.jpg"
TOWERS_DATA_PATH = "250.csv"
LOG_FILE_PATH = "/home/anton/Desktop/mikbsn_m/src/location_log.txt"  
MAX_TOWERS_DISPLAY = 2000
MOSCOW_CENTER_LON = 37.618423
MOSCOW_CENTER_LAT = 55.751244
MAP_ZOOM = 260

class Sim800Emulator(QtWidgets.QMainWindow):
    def __init__(self):
        super().__init__()
        self.tower_markers = []  # Список для хранения маркеров вышек
        self.setWindowTitle("Эмулятор SIM800 с картой Москвы")
        self.setGeometry(100, 100, 800, 600)

        central_widget = QtWidgets.QWidget()
        self.setCentralWidget(central_widget)
        layout = QtWidgets.QVBoxLayout(central_widget)

        control_layout = QtWidgets.QHBoxLayout()
        layout.addLayout(control_layout)

        # Поля для ввода радиуса и скорости
        self.radius_input = QtWidgets.QLineEdit()
        self.radius_input.setPlaceholderText("Радиус (м)")
        self.speed_input = QtWidgets.QLineEdit()
        self.speed_input.setPlaceholderText("Скорость (м/с)")
        control_layout.addWidget(QtWidgets.QLabel("Радиус:"))
        control_layout.addWidget(self.radius_input)
        control_layout.addWidget(QtWidgets.QLabel("Скорость:"))
        control_layout.addWidget(self.speed_input)

        # Кнопка "Начать симуляцию"
        self.start_button = QtWidgets.QPushButton("Начать симуляцию")
        self.start_button.clicked.connect(self.start_simulation)
        control_layout.addWidget(self.start_button)

        # Кнопка "Сброс"
        self.reset_button = QtWidgets.QPushButton("Сброс")
        self.reset_button.clicked.connect(self.reset_simulation)
        control_layout.addWidget(self.reset_button)

        #self.output_text_area = QtWidgets.QTextEdit()
        #self.output_text_area.setReadOnly(True)
        #layout.addWidget(self.output_text_area)

        #self.input_text_area = QtWidgets.QTextEdit()
        #self.input_text_area.setReadOnly(True)
        #layout.addWidget(self.input_text_area)

        self.map_view = pg.PlotWidget()
        layout.addWidget(self.map_view)
        self.map_view.setAspectLocked(True)
        self.map_view.setRange(QtCore.QRectF(MOSCOW_CENTER_LON - 0.04, MOSCOW_CENTER_LAT - 0.04, 0.08, 0.08))

        self.towers = None
        self.add_background_image()
        self.load_towers()
        self.init_drone()

        self.coordinates_label = QtWidgets.QLabel("Текущие координаты: N/A")
        layout.addWidget(self.coordinates_label)

        self.waypoints = []
        self.trajectory_lines = []
        self.drone_marker = None
        self.current_waypoint_index = 0
        self.is_moving = False
        self.detection_radius = 0
        self.detection_circle = None
        self.detected_towers_set = set()
        self.map_view.scene().sigMouseClicked.connect(self.add_waypoint)

        # Настройка UART соединений
        self.receive_port = "/dev/pts/2"
        self.send_port = "/dev/pts/1"
        self.baud_rate = 115200
        self.setup_uart_connections()

        # Таймер для обработки команд
        self.command_timer = QtCore.QTimer(self)
        self.start_uart_listener()
        self.command_timer.start(1000)

        # Список для хранения меток расстояний до вышек
        self.tower_distance_labels = []

       # self.logged_points = set()  # Храним уже добавленные точки
       # self.log_timer = QtCore.QTimer(self)
       # self.log_timer.timeout.connect(self.update_log_points)
       # self.log_timer.start(1000)  # Проверяем лог каждые 0,2 секунды

    def update_log_points(self):
        """Читает лог-файл и добавляет новые точки на карту, если они еще не нанесены."""
        if not os.path.exists(LOG_FILE_PATH):
            print(f"Лог-файл {LOG_FILE_PATH} не найден.")
            return
    
        try:
            with open(LOG_FILE_PATH, 'r') as file:
                lines = file.readlines()
    
            previous_timestamp = None
    
            for line in lines:
                parts = line.strip().split(',')
                timestamp = parts[0].strip()
                lat = float(parts[1].split('=')[1])
                lon = float(parts[2].split('=')[1])
                point_key = (lat, lon)  # Координаты как уникальный ключ
    
                # Проверяем, не добавлена ли уже точка с тем же временным штампом
                if timestamp == previous_timestamp:
                    continue  # Пропускаем точку, если время совпадает с предыдущим
    
                # Проверяем, если точка уже добавлена
                if point_key not in self.logged_points:
                    self.logged_points.add(point_key)
    
                    # Добавляем новый зеленый маркер
                    log_point_marker = pg.ScatterPlotItem(
                        x=[lon], y=[lat], pen=pg.mkPen(None), brush=pg.mkBrush(0, 255, 0, 120), size=8
                    )
                    self.map_view.addItem(log_point_marker)
                    self.tower_markers.append(log_point_marker)  # Сохраняем маркер для возможного сброса
    
                # Обновляем предыдущий временной штамп после добавления точки
                previous_timestamp = timestamp
    
        except Exception as e:
            print(f"Ошибка при чтении лог-файла: {e}")

    def load_log_points(self):
            """Загружает точки из лог-файла и отображает их на карте зеленым цветом."""
            if not os.path.exists(LOG_FILE_PATH):
                print(f"Лог-файл {LOG_FILE_PATH} не найден.")
                return
    
            try:
                with open(LOG_FILE_PATH, 'r') as file:
                    lines = file.readlines()
    
                for line in lines:
                    parts = line.strip().split(',')
                    lat = float(parts[1].split('=')[1])
                    lon = float(parts[2].split('=')[1])
                    
                    # Создаем зеленый маркер для каждой точки
                    log_point_marker = pg.ScatterPlotItem(
                        x=[lon], y=[lat], pen=pg.mkPen(None), brush=pg.mkBrush(0, 255, 0, 120), size=8
                    )
                    self.map_view.addItem(log_point_marker)
                    self.tower_markers.append(log_point_marker)  # Сохраняем маркер для возможного сброса
    
            except Exception as e:
                print(f"Ошибка при чтении лог-файла: {e}")

    def add_waypoint(self, event):
        """Добавляет точки пути и активирует кнопку запуска после добавления точки."""
        mouse_point = self.map_view.plotItem.vb.mapSceneToView(event.scenePos())
        waypoint_lon = mouse_point.x()
        waypoint_lat = mouse_point.y()

        self.waypoints.append((waypoint_lon, waypoint_lat))

        waypoint_item = pg.ScatterPlotItem(
            x=[waypoint_lon], y=[waypoint_lat], pen=pg.mkPen(None), brush=pg.mkBrush(255, 255, 0, 255), size=10
        )
        self.map_view.addItem(waypoint_item)
        self.start_button.setEnabled(True)  # Активируем кнопку "Начать симуляцию"
        
        # Сохраняем маркеры для сброса
        self.tower_markers.append(waypoint_item)

        waypoint_number = len(self.waypoints)
        text_item = pg.TextItem(text=str(waypoint_number), anchor=(0.5, 0), color=(255, 255, 255))
        text_item.setPos(waypoint_lon, waypoint_lat + 0.002)
        self.map_view.addItem(text_item)
        self.tower_markers.append(text_item)

        self.update_trajectory()

        if len(self.waypoints) == 1:
            self.set_drone_marker(waypoint_lon, waypoint_lat)

    def reset_simulation(self):
        """Сбрасывает все состояния, останавливает процессы и очищает карту."""
        # Останавливаем таймеры
        if hasattr(self, 'drone_timer'):
            self.drone_timer.stop()
        self.command_timer.stop()

        # Очищаем маркеры вышек и траекторию
        for marker in self.tower_markers:
            self.map_view.removeItem(marker)
        self.tower_markers.clear()

        for line in self.trajectory_lines:
            self.map_view.removeItem(line)
        self.trajectory_lines = []

        if self.drone_marker:
            self.map_view.removeItem(self.drone_marker)
            self.drone_marker = None

        if self.detection_circle:
            self.map_view.removeItem(self.detection_circle)
            self.detection_circle = None

        # Удаляем метки расстояний до вышек
        for label in self.tower_distance_labels:
            self.map_view.removeItem(label)
        self.tower_distance_labels = []

        # Сбрасываем состояние симуляции
        self.waypoints.clear()
        self.current_waypoint_index = 0
        self.is_moving = False
        print("Симуляция сброшена.")

    def setup_uart_connections(self):
        """Настраивает соединения UART для приема и отправки данных."""
        try:
            self.receive_connection = serial.Serial(self.receive_port, self.baud_rate, timeout=1)
            print(f"Прием на UART порту {self.receive_port}")
        except serial.SerialException as e:
            print(f"Ошибка при подключении к UART: {e}")

    def start_uart_listener(self):
        """Запускает поток для постоянного прослушивания UART-порта."""
        uart_thread = threading.Thread(target=self.respond_to_uart_commands, daemon=True)
        uart_thread.start()

    def respond_to_uart_commands(self):
        """Поток, который непрерывно слушает UART-порт для получения команд и немедленно отвечает."""
        UART_PORT = self.receive_port
        BAUD_RATE = self.baud_rate

        try:
            with serial.Serial(UART_PORT, BAUD_RATE, timeout=1) as ser:
                print(f"Прослушивание команд на {UART_PORT}...")

                # Очистка входного буфера при старте
                ser.reset_input_buffer()

                # Непрерывное чтение команд
                while True:
                    if ser.in_waiting > 0:
                        # Чтение команды
                        command = ser.readline().decode('utf-8').strip()
                        print(f"Получена команда: {command}")

                        # Обработка команды
                        if command == "AT+CENG?":
                            # Если команда AT+CENG?, отправляем данные вышек
                            response = self.send_tower_data()
                        elif command == "AT+CENG=1,1":
                            # Команда активации режима отчета о сотах
                            response = "OK"
                        #else:
                            # Ответ по умолчанию на нераспознанные команды
                         #   response = "ERROR"

                        # Отправляем ответ
                        ser.write((response + "\r\n").encode('utf-8'))
                        print(f"Отправка ответа: {response}")

                    # Короткая задержка для предотвращения чрезмерного использования ресурсов
                    time.sleep(0.05)

        except serial.SerialException as e:
            print(f"Ошибка при подключении к UART: {e}")

    def calculate_distance(self, lat1, lon1, lat2, lon2):
        R = 6371000 
        phi1, phi2 = np.radians(lat1), np.radians(lat2)
        dphi = np.radians(lat2 - lat1)
        dlambda = np.radians(lon2 - lon1)
        a = np.sin(dphi / 2) ** 2 + np.cos(phi1) * np.cos(phi2) * np.sin(dlambda / 2) ** 2
        c = 2 * np.arctan2(np.sqrt(a), np.sqrt(1 - a))
        return R * c

    def calculate_rssi(self, lat1, lon1, lat2, lon2):
    
        distance = self.calculate_distance(lat1, lon1, lat2, lon2)  
       #print("distance betweed UAV and TOWER at lat="+str(lat2)+", lon="+str(lon2)+" : " + str(distance))
        tx_power = 20
        fspl_constant = 147.55
        path_loss = 20 * math.log10(distance) + 20 * math.log10(1800) - fspl_constant
        rssi = -1*(tx_power - path_loss)
        rssi = round(rssi)
       # print("calculated RSSI: "+str(rssi) )
        

        '''
        distance = self.calculate_distance(lat1, lon1, lat2, lon2)  # Расстояние в метрах
        if distance < 1:
            distance = 1  # Избегаем log(0)
        A = -40  # RSSI на расстоянии 1 метр
        n = 3    # Коэффициент потерь
        rssi = A - 10 * n * np.log10(distance)
        rssi = round(rssi)
        return rssi
        '''
        
        return rssi
    
    

    def calculate_distances_vectorized(self, lat1, lon1, lat2, lon2):
        """Векторизованное вычисление расстояний между точками."""
        R = 6371000  # Радиус Земли в метрах
        phi1 = np.radians(lat1)
        phi2 = np.radians(lat2)
        dphi = np.radians(lat2 - lat1)
        dlambda = np.radians(lon2 - lon1)
        a = np.sin(dphi / 2) ** 2 + np.cos(phi1) * np.cos(phi2) * np.sin(dlambda / 2) ** 2
        c = 2 * np.arctan2(np.sqrt(a), np.sqrt(1 - a))
        distances = R * c
        return distances

    def get_nearest_towers(self):
        """Получает 7 ближайших вышек к текущей позиции дрона."""
        if self.drone_position is None:
            return []

        drone_lon, drone_lat = self.drone_position[0], self.drone_position[1]

        tower_lons = self.tower_positions[:, 0]
        tower_lats = self.tower_positions[:, 1]

        distances = self.calculate_distances_vectorized(drone_lat, drone_lon, tower_lats, tower_lons)

        # Получаем индексы 7 ближайших вышек
        nearest_indices = np.argsort(distances)[:7]

        nearest_towers = []
        for idx in nearest_indices:
            lon = tower_lons[idx]
            lat = tower_lats[idx]
            mcc = self.towers_df.iloc[idx]['mcc']
            mnc = self.towers_df.iloc[idx]['net']
            cell = self.towers_df.iloc[idx]['cell']
            distance = distances[idx]
            rssi = self.calculate_rssi(drone_lat, drone_lon, lat, lon)
            nearest_towers.append((idx, lon, lat, mcc, mnc, cell, distance, rssi))
        return nearest_towers

    def update_nearest_towers(self):
        """Обновляет цвета вышек, выделяя 7 ближайших красным цветом, и отображает расстояния."""
        if self.drone_position is None:
            return

        nearest_towers = self.get_nearest_towers()
        nearest_indices = [t[0] for t in nearest_towers]

        # Сбрасываем все кисти в синий цвет
        self.tower_brushes = [pg.mkBrush(0, 0, 255, 120) for _ in range(len(self.towers_df))]
        # Выделяем ближайшие вышки красным цветом
        for idx in nearest_indices:
            self.tower_brushes[idx] = pg.mkBrush(255, 0, 0, 255)

        # Обновляем отображение вышек
        self.tower_scatter.setBrush(self.tower_brushes)

        # Удаляем существующие метки расстояний
        for label in self.tower_distance_labels:
            self.map_view.removeItem(label)
        self.tower_distance_labels = []

        # Добавляем метки расстояний для ближайших вышек
        for t in nearest_towers:
            idx, lon, lat, mcc, mnc, cell, distance, rssi = t
            distance_text = f"{int(distance)} м"
            text_item = pg.TextItem(text=distance_text, anchor=(0.5, -1.0), color=(255, 255, 255))
            text_item.setPos(lon, lat)
            self.map_view.addItem(text_item)
            self.tower_distance_labels.append(text_item)

    def send_tower_data(self):
        """Возвращает данные о 7 ближайших вышках с вычисленным RSSI и расстоянием."""
        if self.drone_position is None or not isinstance(self.drone_position, np.ndarray):
            print("Ошибка: координаты дрона не установлены.")
            return "ERROR"

        nearest_towers = self.get_nearest_towers()
        if not nearest_towers:
            return "ERROR"

        tower_data_lines = []
        for i, (idx, lon, lat, mcc, mnc, cell, distance, rssi) in enumerate(nearest_towers):
            distance_with_error = distance + np.random.normal(0, 5)
            distance_with_error = max(0, int(distance_with_error))

            rssi_with_error = rssi + np.random.normal(0, 2)
            rssi_with_error = max(0, int(rssi_with_error))

            if i == 0:
                line = f'+CENG: {i},"0034,{-rssi},00,{mcc},{mnc},40,{cell:04x},01,05,6d07,255"'
            else:
                line = f'+CENG: {i},"0072,{-rssi},44,{cell:04x},{mcc},{mnc},6d07"'
            tower_data_lines.append(line)

        tower_data = "\n".join(tower_data_lines)
        print("Отправлены данные о вышках с расстоянием и RSSI:\n" + tower_data)
        return tower_data

    def load_towers(self):
        self.towers_df = pd.read_csv(TOWERS_DATA_PATH)
        if len(self.towers_df) > MAX_TOWERS_DISPLAY:
            self.towers_df = self.towers_df.sample(n=MAX_TOWERS_DISPLAY, random_state=1)
        
        latitudes = self.towers_df['lat'].values
        longitudes = self.towers_df['lon'].values
        mccs = self.towers_df['mcc'].values
        mncs = self.towers_df['net'].values  # Получаем MNC
        cells = self.towers_df['cell'].values

        self.tower_positions = np.column_stack((longitudes, latitudes))
        self.tower_brushes = [pg.mkBrush(0, 0, 255, 120) for _ in range(len(self.towers_df))]

        self.tower_scatter = pg.ScatterPlotItem(
            x=longitudes, y=latitudes, pen=pg.mkPen(None), brush=self.tower_brushes, size=10
        )
        self.map_view.addItem(self.tower_scatter)
        self.towers = list(zip(longitudes, latitudes, mccs, mncs, cells))  # Добавляем MNC в список вышек

    def add_background_image(self):
        map_pixmap = QtGui.QPixmap(MAP_IMAGE_PATH)
        if map_pixmap.isNull():
            print("Ошибка: Не удалось загрузить изображение карты.")
            return

        map_image = map_pixmap.transformed(QtGui.QTransform().rotate(90), mode=QtCore.Qt.SmoothTransformation)
        map_image = map_image.toImage()
        width, height = map_image.width(), map_image.height()
        map_image = map_image.convertToFormat(QtGui.QImage.Format_RGB32)
        ptr = map_image.bits()
        ptr.setsize(height * width * 4)
        img_array = np.array(ptr).reshape((height, width, 4))

        map_item = pg.ImageItem(img_array)
        scale_factor = 13
        map_item.setRect(MOSCOW_CENTER_LON - 0.04 * scale_factor, MOSCOW_CENTER_LAT - 0.04 * scale_factor, 0.08 * scale_factor, 0.08 * scale_factor)
        map_item.setZValue(-1)
        self.map_view.addItem(map_item)

    def init_drone(self):
        self.drone_position = np.array([MOSCOW_CENTER_LON, MOSCOW_CENTER_LAT])
        self.drone_marker = pg.ScatterPlotItem(
            x=[self.drone_position[0]], y=[self.drone_position[1]], pen=pg.mkPen(None), brush=pg.mkBrush(255, 0, 0, 255), size=8
        )
        self.map_view.addItem(self.drone_marker)

    def set_drone_marker(self, lon, lat):
        if self.drone_marker:
            self.map_view.removeItem(self.drone_marker)

        self.drone_marker = pg.ScatterPlotItem(
            x=[lon], y=[lat], pen=pg.mkPen(None), brush=pg.mkBrush(255, 0, 0, 255), size=12
        )
        self.map_view.addItem(self.drone_marker)
        self.drone_position = np.array([lon, lat])
        self.update_detection_radius(float(self.radius_input.text()) if self.radius_input.text() else 0)

        # Обновляем метку с координатами
        self.coordinates_label.setText(f"Текущие координаты: {lat:.6f}, {lon:.6f}")

    def update_trajectory(self):
        # Удаляем старую траекторию
        for line in self.trajectory_lines:
            self.map_view.removeItem(line)
        self.trajectory_lines = []

        # Отрисовываем линии между вейпоинтами
        for i in range(1, len(self.waypoints)):
            line = pg.PlotDataItem(
                x=[self.waypoints[i - 1][0], self.waypoints[i][0]],
                y=[self.waypoints[i - 1][1], self.waypoints[i][1]],
                pen=pg.mkPen(color=(255, 255, 0), width=2)
            )
            self.map_view.addItem(line)
            self.trajectory_lines.append(line)

    def update_detection_radius(self, radius):
        if self.detection_circle:
            self.map_view.removeItem(self.detection_circle)

        self.detection_radius = radius
        if radius > 0:
            self.detection_circle = pg.CircleROI(
                pos=(self.drone_position[0] - radius / 111320, self.drone_position[1] - radius / 111320),
                size=(radius * 2 / 111320, radius * 2 / 111320),
                pen=pg.mkPen(color=(255, 0, 0), width=1),
                movable=False
            )
            self.map_view.addItem(self.detection_circle)

    def start_simulation(self):
        if not self.waypoints:
            print("Не установлены точки пути для симуляции.")
            return
        self.is_moving = True
        self.current_waypoint_index = 0
        self.drone_timer = QtCore.QTimer(self)
        self.drone_timer.timeout.connect(self.move_drone)
        self.drone_timer.start(1)

    def move_drone(self):
        if self.is_moving and self.waypoints:
            current_waypoint = self.waypoints[self.current_waypoint_index]
            distance = np.sqrt((current_waypoint[0] - self.drone_position[0]) ** 2 + (current_waypoint[1] - self.drone_position[1]) ** 2)

            if distance < 0.001:
                self.current_waypoint_index += 1
                if self.current_waypoint_index >= len(self.waypoints):
                    self.is_moving = False
                    self.drone_timer.stop()  # Останавливаем таймер
                    return
                current_waypoint = self.waypoints[self.current_waypoint_index]

            # Обновляем позицию
            direction = (current_waypoint[0] - self.drone_position[0], current_waypoint[1] - self.drone_position[1])
            norm = np.sqrt(direction[0] ** 2 + direction[1] ** 2)
            if norm != 0:
                direction = (direction[0] / norm, direction[1] / norm)
                speed = float(self.speed_input.text()) if self.speed_input.text() else 1.0
                self.drone_position[0] += direction[0] * speed * 0.0001
                self.drone_position[1] += direction[1] * speed * 0.0001

            self.set_drone_marker(self.drone_position[0], self.drone_position[1])

            # Обновляем траекторию и радиус обнаружения
            self.update_trajectory()
            self.update_detection_radius(self.detection_radius)

            # Обновляем ближайшие вышки и метки расстояний
            self.update_nearest_towers()

if __name__ == "__main__":
    
    app = QtWidgets.QApplication(sys.argv)
    app.setStyle("Fusion")
    dark_palette = QtGui.QPalette()
    dark_palette.setColor(QtGui.QPalette.Window, QtGui.QColor(53, 53, 53))
    dark_palette.setColor(QtGui.QPalette.WindowText, QtCore.Qt.white)
    dark_palette.setColor(QtGui.QPalette.Base, QtGui.QColor(25, 25, 25))
    dark_palette.setColor(QtGui.QPalette.AlternateBase, QtGui.QColor(53, 53, 53))
    dark_palette.setColor(QtGui.QPalette.ToolTipBase, QtCore.Qt.white)
    dark_palette.setColor(QtGui.QPalette.ToolTipText, QtCore.Qt.white)
    dark_palette.setColor(QtGui.QPalette.Text, QtCore.Qt.white)
    dark_palette.setColor(QtGui.QPalette.Button, QtGui.QColor(53, 53, 53))
    dark_palette.setColor(QtGui.QPalette.ButtonText, QtCore.Qt.white)
    dark_palette.setColor(QtGui.QPalette.BrightText, QtCore.Qt.red)
    dark_palette.setColor(QtGui.QPalette.Link, QtGui.QColor(42, 130, 218))
    dark_palette.setColor(QtGui.QPalette.Highlight, QtGui.QColor(42, 130, 218))
    dark_palette.setColor(QtGui.QPalette.HighlightedText, QtCore.Qt.black)
    app.setPalette(dark_palette)
    
    
    emulator = Sim800Emulator()
    emulator.show()
    sys.exit(app.exec_())
