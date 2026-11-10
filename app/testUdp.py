import tkinter as tk
from tkinter import ttk
import socket

# ──────── Настройки UDP ────────
UDP_IP   = "192.168.1.134"   # IP получателя
UDP_PORT = 7777          # Порт получателя

# ──────── Массив команд ────────
# Каждая строка массива = одна кнопкаПротокол от макса:

COMMANDS = [
    "|f1200;",
    "|b1500;",
    "|h;",
    "|t;",
    "|p;",
    "|e;",
    "|r;",
    "|u;",

    
]

# Единый сокет на всё время работы программы
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)


def send_udp(command: str) -> None:
    """Отправляет строку по UDP и обновляет статус."""
    try:
        sock.sendto(command.encode("utf-8"), (UDP_IP, UDP_PORT))
        status_var.set(f"✔ Отправлено: {command!r}")
    except Exception as e:
        status_var.set(f"✖ Ошибка: {e}")


def build_ui(root: tk.Tk) -> None:
    """Создаёт все элементы интерфейса, включая кнопки из COMMANDS."""
    header = tk.Label(
        root,
        text=f"UDP → {UDP_IP}:{UDP_PORT}",
        font=("Arial", 11, "bold"),
    )
    header.pack(pady=(10, 5))

    # Прокручиваемая область (на случай, если кнопок много)
    container = ttk.Frame(root)
    container.pack(fill="both", expand=True, padx=10, pady=5)

    canvas = tk.Canvas(container, highlightthickness=0)
    scrollbar = ttk.Scrollbar(container, orient="vertical", command=canvas.yview)
    canvas.configure(yscrollcommand=scrollbar.set)

    scrollbar.pack(side="right", fill="y")
    canvas.pack(side="left", fill="both", expand=True)

    btn_frame = ttk.Frame(canvas)
    canvas.create_window((0, 0), window=btn_frame, anchor="nw")

    def on_frame_config(event):
        canvas.configure(scrollregion=canvas.bbox("all"))

    btn_frame.bind("<Configure>", on_frame_config)

    # Создаём по кнопке на каждую команду.
    # ВАЖНО: c=cmd в lambda — иначе все кнопки отправят последнюю команду
    for cmd in COMMANDS:
        btn = tk.Button(
            btn_frame,
            text=cmd,
            width=30,
            height=2,
            font=("Consolas", 10),
            command=lambda c=cmd: send_udp(c),
        )
        btn.pack(pady=3, padx=5, fill="x")

    # Строка состояния
    global status_var
    status_var = tk.StringVar(value="Готово")
    status_label = tk.Label(root, textvariable=status_var, anchor="w", fg="darkgreen")
    status_label.pack(side="bottom", fill="x", padx=10, pady=5)


def main() -> None:
    root = tk.Tk()
    root.title("UDP-отправитель команд")
    root.geometry("360x480")
    build_ui(root)
    root.mainloop()
    sock.close()


if __name__ == "__main__":
    main()