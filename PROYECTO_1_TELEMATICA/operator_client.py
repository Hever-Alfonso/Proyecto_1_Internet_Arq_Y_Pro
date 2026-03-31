import socket
import threading
import tkinter as tk
from tkinter import scrolledtext

# ==========================
# CONFIG
# ==========================
HOST = "127.0.0.1"
PORT = 8080

sock = None


# ==========================
# RECIBIR MENSAJES (HILO)
# ==========================
def recibir():
    while True:
        try:
            data = sock.recv(1024).decode()
            if not data:
                break
            mostrar_mensaje(data.strip())
        except:
            break


# ==========================
# MOSTRAR MENSAJE EN GUI
# ==========================
def mostrar_mensaje(msg):
    chat.config(state=tk.NORMAL)
    chat.insert(tk.END, msg + "\n")
    chat.config(state=tk.DISABLED)
    chat.yview(tk.END)


# ==========================
# CONECTAR
# ==========================
def conectar():
    global sock
    nombre = entry_nombre.get()

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((HOST, PORT))

    enviar(f"REGISTER_OPERATOR {nombre}")

    hilo = threading.Thread(target=recibir)
    hilo.daemon = True
    hilo.start()

    mostrar_mensaje("✅ Conectado como operador")


# ==========================
# ENVIAR
# ==========================
def enviar(msg):
    sock.send((msg + "\n").encode())


# ==========================
# COMANDOS
# ==========================
def ver_sensores():
    enviar("GET_SENSORS")


def consultar_dato():
    sensor_id = entry_sensor.get()
    enviar(f"GET_DATA {sensor_id}")


# ==========================
# GUI
# ==========================
root = tk.Tk()
root.title("Operador IoT")

# Nombre operador
tk.Label(root, text="Nombre:").pack()
entry_nombre = tk.Entry(root)
entry_nombre.pack()

tk.Button(root, text="Conectar", command=conectar).pack()

# Chat
chat = scrolledtext.ScrolledText(root, width=50, height=20, state=tk.DISABLED)
chat.pack()

# Botones
tk.Button(root, text="Ver sensores", command=ver_sensores).pack()

tk.Label(root, text="Sensor ID:").pack()
entry_sensor = tk.Entry(root)
entry_sensor.pack()

tk.Button(root, text="Consultar dato", command=consultar_dato).pack()

root.mainloop()