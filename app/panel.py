import os
import time
import tkinter as tk
import socket 
import csv
import threading
from datetime import datetime
import math
from string import Template
import tkinter as tk
from tkinter import ttk

from tkinter import filedialog
from openpyxl import Workbook
from openpyxl.utils import get_column_letter

SAVE_FILE = "charge_state.txt"
OUTPUT_FILE = "log.txt"

RIP = "192.168.1.134"

RHost = 7777
LHost = 3333

CL = 58.1 * math.pi # ОБОРОТ 1200 ТИКОВ  
m = 0

easy_comms = [0,0,0,0,0,0]

JUST_FORWARD = 0 
JUST_BACK = 1
STOP = 2
RESET_ENCODER = 3

FORWARD_FOR = 4
BACK_FOR = 5


deffectArray=[]

Template('Привет, $name! Ваш баланс: $balance')

easy_comms[JUST_FORWARD]  =    Template("|h;") # просто прямо
easy_comms[JUST_BACK]     =    Template("|t;") # просто назад
easy_comms[STOP]          =    Template("|p;") # остановитсяы
easy_comms[RESET_ENCODER] =    Template("|u;") # сбросить энкодер
easy_comms[FORWARD_FOR]   =    Template("|f$pos;") # сбросить энкодер
easy_comms[BACK_FOR]      =    Template("|b$pos;") # сбросить энкодер


def meter_teak(meters):
    teaks = (meters/(CL/1000))*1200
    return teaks

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
# sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
sock.bind(("", LHost)) 


def send_udp(CNum):
    if len(app.distance_history)==0:
        print("ERR DISTANCE")
        return
    cmd = easy_comms[CNum].substitute(pos=int(app.distance_history[len(app.distance_history)-1]))
    sock.sendto(cmd.encode('utf-8'), (RIP, RHost))
    print(f"отправлена команда {cmd}")


class CountdownApp: 

    def __init__(self, root):
        self.root = root
        self.root.title("панель управления")
        self.root.geometry("800x500")

        # Параметры формулы
        self.start_val = 89.0
        self.end_val = 30.0
        self.total_time_minutes = 300
        self.total_time_seconds = self.total_time_minutes * 60

        self.distance_history = [meter_teak(10)]

        # Загружаем сохраненное состояние или создаем новое
        self.load_state()

        # Интерфейс
        self.label = tk.Label(
            root,
            text=f"заряд: {self.counter_value:.2f}",
            font=("Arial", 14),
            pady=10,
        )
        self.label.grid(row=0,column=0)

        self.entry = tk.Entry(root, font=("Arial", 12))
        self.entry.grid(row=1,column=0)
        self.entry.insert(0, 10)

        self.btn_confirm = tk.Button(
            root, text="ввести расстояние", command=self.confirm_input
        )
        self.colCounter = 2
        self.btn_confirm.grid(row=2,column=0)
        #движение по энкодеру в перед
        
        def abstract(direct):
            send_udp(direct) 
            self.add_column(f"с{self.colCounter}",f"проход {self.colCounter-1}") 
            self.colCounter+=1
        
        self.btn_one = tk.Button(
            root, text="вперед на указанное расстояние", command=lambda:abstract(FORWARD_FOR) 
                
        )
        self.btn_one.grid(row=3,column=0)
        #движение по энкодеру назад
        self.btn_two = tk.Button(
            root, text="назад на указанное расстояние", command=lambda: abstract(BACK_FOR)
        )
        self.btn_two.grid(row=4,column=0)

        # Перехватываем нажатие Ctrl + Любая клавиша
        self.root.bind("<Control-KeyPress>", self.check_hotkey)


        # энкодер
        self.btn_five  = tk.Button(
                    root, text = "- энкодер", command=lambda: send_udp(RESET_ENCODER)
                )
        self.btn_five.grid(row=5,column=0)

        # абсолютно вперед
        self.btn_six = tk.Button(
            root, text = "абсолютно вперед", command=lambda: send_udp(JUST_FORWARD)
        )
        self.btn_six.grid(row=6,column=0)

        #абсолютно назад
        self.btn_seven = tk.Button(
                    root, text = "абсолютно назад", command=lambda: send_udp(JUST_BACK)
                )
        self.btn_seven.grid(row=7,column=0)
        
        self.btn_stop = tk.Button(
            root, text = "стоп", command=lambda: send_udp(STOP)
        )
        self.btn_stop.grid(row=8,column=0)

        self.save_bt = tk.Button(text="Сохранить", command= self.save_treeview_to_excel)
        self.save_bt.grid(row=8, column=1)
        self.tree = ttk.Treeview(root,columns=("с1",), show='headings', height=5)
        self.tree.column("с1", anchor=tk.CENTER, width=100)
        self.tree.heading("с1", text="Позиция")
        self.tree.grid(row=0,rowspan=7,column=1) 

        # Запуск цикла обновления
        self.update_label_physics()


    def save_treeview_to_excel(self, filepath=None, include_tree_column=None):
        """
        Сохраняет содержимое Treeview в Excel-файл (.xlsx).
        
        :param tree: объект ttk.Treeview
        :param filepath: путь для сохранения. Если None — откроется диалог выбора.
        :param include_tree_column: сохранять ли колонку #0 (самую левую с треугольниками).
                                    Если None — определяется автоматически по параметру show.
        :return: путь к сохранённому файлу или None, если пользователь отменил.
        """
        # 1. Если путь не задан — просим пользователя выбрать
        if filepath is None:
            filepath = filedialog.asksaveasfilename(
                defaultextension=".xlsx",
                filetypes=[("Excel files", "*.xlsx"), ("All files", "*.*")],
                title="Сохранить таблицу как..."
            )
            if not filepath:  # пользователь нажал "Отмена"
                return None
        
        # 2. Определяем, нужно ли сохранять колонку #0
        if include_tree_column is None:
            show = self.tree.cget("show")  # например: "headings", "tree", "headings tree"
            include_tree_column = "tree" in show
        
        # 3. Собираем заголовки
        headers = []
        if include_tree_column:
            headers.append(self.tree.heading("#0", "text") or "#0")
        for col in self.tree["columns"]:
            headers.append(self.tree.heading(col, "text") or col)
        
        # 4. Собираем данные
        rows = []
        for iid in self.tree.get_children():
            item = self.tree.item(iid)
            row_data = []
            if include_tree_column:
                row_data.append(item.get("text", ""))
            row_data.extend(item.get("values", []))
            rows.append(row_data)
        
        # 5. Записываем в Excel
        wb = Workbook()
        ws = wb.active
        ws.title = "Данные"
        
        # Заголовки
        ws.append(headers)
        # Делаем заголовки жирными
        from openpyxl.styles import Font
        for cell in ws[1]:
            cell.font = Font(bold=True)
        
        # Данные
        for row in rows:
            ws.append(row)
        
        # 6. Автоподбор ширины колонок (чтобы всё было видно)
        for col_idx, header in enumerate(headers, start=1):
            max_len = len(str(header))
            col_letter = get_column_letter(col_idx)
            for row in ws.iter_rows(min_row=2, min_col=col_idx, max_col=col_idx):
                for cell in row:
                    if cell.value is not None:
                        max_len = max(max_len, len(str(cell.value)))
            ws.column_dimensions[col_letter].width = min(max_len + 2, 50)
        
        # 7. Сохраняем
        try:
            wb.save(filepath)
            return filepath
        except PermissionError:
            raise PermissionError(
                f"Не удалось сохранить файл '{filepath}'. "
                "Возможно, он открыт в Excel или нет прав на запись."
            )
        
    def insert_row(self, index, values=None, text="", iid=None):
        """
        Вставляет строку в произвольную позицию Treeview.
        
        :param tree: объект ttk.Treeview
        :param index: позиция вставки (0 — в начало, -1 — перед последней строкой,
                    "end" — в конец). Если индекс больше числа строк — вставляется в конец.
        :param values: список/кортеж значений для колонок данных. 
                    Если None или короче числа колонок — недостающие заполняются "".
        :param text: значение для служебной колонки #0 (по умолчанию пусто).
        :param iid: уникальный идентификатор строки (опционально).
        :return: iid вставленной строки.
        """
        num_cols = len(self.tree["columns"])
        
        # Нормализуем values до нужной длины
        if values is None:
            values = [""] * num_cols
        else:
            values = list(values)
            while len(values) < num_cols:
                values.append("")
            # Если значений больше, чем колонок — обрезаем лишнее
            values = values[:num_cols]
        
        # Нормализуем индекс
        children = self.tree.get_children()
        total = len(children)
        
        if index == "end" or index >= total:
            pos = "end"
        elif index < -total:
            pos = 0
        elif index < 0:
            # Отрицательный индекс: -1 = перед последней строкой
            pos = total + index
        else:
            pos = index
        
        # Формируем параметры для insert
        kwargs = {"values": values}
        if text:
            kwargs["text"] = text
        if iid is not None:
            kwargs["iid"] = iid
        
        return self.tree.insert("", pos, **kwargs)
    
    def add_column(self, col_id, col_title=None, width=100, anchor="center"):
        """Добавляет новую колонку в конец Treeview, сохраняя настройки существующих."""
        if col_title is None:
            col_title = col_id
        
        # 1. Сохраняем настройки всех существующих колонок
        saved_settings = {}
        for col in self.tree["columns"]:
            saved_settings[col] = {
                "heading": self.tree.heading(col, "text"),
                "width": self.tree.column(col, "width"),
                "anchor": self.tree.column(col, "anchor"),
                "minwidth": self.tree.column(col, "minwidth"),
            }
        
        # 2. Добавляем новую колонку в список
        current_cols = list(self.tree["columns"])
        current_cols.append(col_id)
        self.tree["columns"] = current_cols
        
        # 3. Восстанавливаем настройки старых колонок
        for col, settings in saved_settings.items():
            self.tree.heading(col, text=settings["heading"])
            self.tree.column(
                col,
                width=settings["width"],
                anchor=settings["anchor"],
                minwidth=settings["minwidth"],
            )
        
        # 4. Настраиваем новую колонку
        self.tree.heading(col_id, text=col_title)
        self.tree.column(col_id, width=width, anchor=anchor)


    def add_data_to_first_column(self, value):
        """Добавляет новую строку в конец, заполняя первую колонку данных."""
        # Определяем количество колонок данных, чтобы создать кортеж нужной длины
        num_cols = len(self.tree["columns"])
        values = [""] * num_cols
        
        # Заполняем первую колонку данных (индекс 0)
        if num_cols > 0:
            values[0] = value
            
        # Вставляем новую строку в самый конец ("end")
        self.tree.insert("", "end", values=values)


    def set_cell_data(self, row_index, col_index, value):
        """Изменяет данные в произвольной ячейке по индексу строки и колонки."""
        # Получаем все ID строк (items)
        children = self.tree.get_children()
        
        if row_index >= len(children) or row_index < 0:
            raise IndexError(f"Строки с индексом {row_index} не существует")
            
        # Получаем ID нужной строки
        iid = children[row_index]
        
        # Получаем текущие значения строки и преобразуем в список для изменения
        current_values = list(self.tree.item(iid, "values"))
        
        # На случай, если строка была создана пустой, расширяем список до числа колонок
        num_cols = len(self.tree["columns"])
        while len(current_values) < num_cols:
            current_values.append("")
            
        # Изменяем нужную ячейку
        current_values[col_index] = value
        
        # Возвращаем измененные значения обратно в строку
        self.tree.item(iid, values=current_values)

    def check_hotkey(self, event):
        """Проверяет нажатие клавиши R независимо от раскладки клавиатуры"""
        if event.keycode == 82 or event.keysym.lower() == "r":
            self.reset_charge()

    def load_state(self):
        """Загрузка времени старта из файла для непрерывного отсчета"""
        if os.path.exists(SAVE_FILE):
            try:
                with open(SAVE_FILE, "r") as f:
                    self.start_time = float(f.read().strip())
                elapsed = time.time() - self.start_time
                progress = min(1.0, elapsed / self.total_time_seconds)
                self.counter_value = self.start_val - (
                    self.start_val - self.end_val
                ) * (progress**2)
                return
            except Exception:
                pass

        self.reset_state_file()

    def reset_state_file(self):
        """Запись текущего времени как точки отсчета"""
        self.start_time = time.time()
        self.counter_value = self.start_val
        try:
            with open(SAVE_FILE, "w") as f:
                f.write(str(self.start_time))
        except Exception as e:
            print(f"Ошибка сохранения файла: {e}")

    def reset_charge(self):
        """Сброс заряда на 89.0 по горячей клавише"""
        self.reset_state_file()
        self.label.config(text=f"Значение: {self.counter_value:.2f}")

    def confirm_input(self):
        user_text = self.entry.get()
        
        try:
            # Превращаем текст в дробное число
            distance = float(user_text)
            
            # Добавляем в массив
            self.distance_history= [(meter_teak(distance))]
            
            print(f"Расстояние {distance} успешно добавлено!")
            print(f"История всех расстояний: {self.distance_history}")
            
            # Очищаем поле ввода для нового значения
            # self.entry.delete(0, tk.END) 
            
        except ValueError:
            print("Ошибка: введите корректное число (например, 10 или 5.5)")

    def update_label_physics(self):
        """Расчет затухания с ускорением"""
        elapsed_seconds = time.time() - self.start_time

        if elapsed_seconds >= self.total_time_seconds:
            self.counter_value = self.end_val
            self.label.config(text=f"заряд: {self.counter_value:.2f}")
            self.root.after(1000, self.update_label_physics)
            return

        progress = elapsed_seconds / self.total_time_seconds
        self.counter_value = self.start_val - (self.start_val - self.end_val) * (
            progress**2
        )

        self.label.config(text=f"заряд: {self.counter_value:.2f}")
        self.root.after(1000, self.update_label_physics)




def socket_listener_thread():
    """Фоновая функция для постоянного чтения данных из сокета"""
    print("Фоновый поток чтения логов запущен...")
    try:
        while True:
            # Открываем файл в режиме дозаписи ('a')
            with open(OUTPUT_FILE, "a", encoding="utf-8") as file:
                while True:
                    try:
                        # Принимаем пакет (размер буфера 1024 байта)
                        data, client_address = sock.recvfrom(1024)
                        
                        # Декодируем байты в строку, игнорируя битые символы
                        decoded_data = data.decode("utf-8", errors="ignore")

                        buf = decoded_data.split(',')
                        dist = 0
                        arg = ""
                        if len(buf)==4:
                            dist = buf[2] 
                            typ = buf[0]
                            side = buf[1]
                            size = buf[3]
                            arg = f"{typ}-{side}-{size}"
                        
                        if len(buf)==2:
                            dist = buf[1] 
                            typ = buf[0]
                            arg = f"{typ}"

                        appendData(dist,arg)
                        # Записываем данные в файл и сбрасываем буфер на диск
                        file.write(decoded_data)
                        file.flush()
                        
                        # Дублируем вывод в консоль для контроля
                        print(f"Записано от {client_address}: {decoded_data}")
                    except Exception as e:
                        print(e)
                        # Если сокет закроется при выходе из приложения, выходим из цикла
                        break
    except Exception as e:
        print(f"Ошибка в потоке сокета: {e}")




def appendData(dist,arg):
    print(dist,arg)
    dist = int(dist)
    if app.colCounter==3:
        deffectArray.append(dist)
        app.add_data_to_first_column(dist)    
        app.set_cell_data(len(deffectArray)-1,1,arg)
    else:
        idx = 0

        min = deffectArray[0]
        for i in range(len(deffectArray)):
            if(math.fabs(dist-deffectArray[i])<min):
                min = math.fabs(dist-deffectArray[i])
                idx = i
        if min > 150:
            idx = -1
                
        if idx<0:
            idx = -1
            for i in range(len(deffectArray)):
                if(deffectArray[i]>dist):            
                    idx = i
            if idx<0:
                idx = len(deffectArray)
            app.insert_row(idx,[dist])
            idx+=1
            deffectArray.insert(idx-1,dist)

        idx=0
        min = deffectArray[0]
        for i in range(len(deffectArray)):
            if(math.fabs(dist-deffectArray[i])<min):
                min = math.fabs(dist-deffectArray[i])
                idx = i
        
            
        if min<200:
            print("повторное обнаружение дефекта")
            app.set_cell_data(idx,app.colCounter-2,arg)
        
if __name__ == "__main__":
    # 1. Запускаем фоновый поток для логов
    # daemon=True означает, что поток автоматически закроется при закрытии окна
    listener = threading.Thread(target=socket_listener_thread, daemon=True)
    listener.start()

    # 2. Создаем и запускаем графический интерфейс Tkinter
    print("Запуск графического интерфейса...")
    root = tk.Tk()
    app = CountdownApp(root)

    try:
        root.mainloop()
    finally:
        # Закрываем сокет при закрытии окна, чтобы освободить порт
        sock.close()
        print("Работа программы успешно завершена.")

# CSV_FILE = "udp_received_data.csv"  # Имя файла для сохранения

# # Проверяем, существует ли файл, чтобы записать заголовки
# file_exists = os.path.isfile(CSV_FILE)

# print(f"Запуск UDP-слушателя на {RIP}:{LHost}...")
# print(f"Данные будут сохраняться в {CSV_FILE}")

# # Создаем UDP сокет
# sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
# sock.bind((RIP, LHost))

# try:
#     with open(CSV_FILE, mode="a", newline="", encoding="utf-8") as file:
#         writer = csv.writer(file)

#         # Если файл новый, пишем шапку таблицы
#         if not file_exists:
#             writer.writerow(["Timestamp", "Sender_IP", "Sender_Port", "Payload"])
#             file.flush()

#         while True:
#             # Читаем данные из сокета (буфер 1024 байта)
#             data, addr = sock.recvfrom(1024)

#             # Декодируем байты в строку (при необходимости меняйте utf-8 на cp1251)
#             try:
#                 payload = data.decode("utf-8")
#             except UnicodeDecodeError:
#                 payload = str(data)  # Если не текст, сохраняем как сырые байты

#             # Получаем текущее время
#             timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]

#             # Записываем строку в CSV
#             writer.writerow([timestamp, addr[0], addr[1], payload])
#             file.flush()  # Принудительно сбрасываем буфер на диск

#             print(f"[{timestamp}] Получено от {addr[0]}:{addr[1]} -> {payload}")



# except KeyboardInterrupt:
#     print("\nСлушатель остановлен пользователем.")
# finally:
#     sock.close()