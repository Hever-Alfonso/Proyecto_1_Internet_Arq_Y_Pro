import socket
import time
import random

# ==========================
# CONFIGURACIÓN
# ==========================
HOST = "127.0.0.1"
PORT = 8080

SENSOR_ID = "sensor01"
TIPO = "temperatura"  # temperatura, vibracion, energia

INTERVALO = 3  # segundos


# ==========================
# GENERAR DATOS SEGÚN TIPO
# ==========================
def generar_valor(tipo):
    if tipo == "temperatura":
        return random.uniform(20, 80)
    elif tipo == "vibracion":
        return random.uniform(10, 100)
    elif tipo == "energia":
        return random.uniform(100, 1200)
    else:
        return random.uniform(0, 100)


# ==========================
# CONEXIÓN
# ==========================
def conectar():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((HOST, PORT))
    return s


# ==========================
# ENVIAR MENSAJE
# ==========================
def enviar(sock, mensaje):
    sock.send((mensaje + "\n").encode())
    respuesta = sock.recv(1024).decode()
    print(f"[SERVER] {respuesta.strip()}")


# ==========================
# MAIN
# ==========================
def main():
    print("🔌 Conectando al servidor...")
    sock = conectar()

    # Registrar sensor
    print("📡 Registrando sensor...")
    enviar(sock, f"REGISTER_SENSOR {SENSOR_ID} {TIPO}")

    print("🚀 Enviando datos... (Ctrl+C para detener)")

    while True:
        valor = generar_valor(TIPO)

        mensaje = f"SEND_DATA {SENSOR_ID} {valor:.2f}"
        print(f"[SENSOR] {mensaje}")

        enviar(sock, mensaje)

        time.sleep(INTERVALO)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n⛔ Sensor detenido")