#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>

#define BUFFER_SIZE 1024
#define MAX_SENSORS 100
#define MAX_OPERATORS 100

// ==========================
// SENSORES
// ==========================
typedef struct {
    char id[50];
    char tipo[50];
    float ultimo_valor;
    int activo;
} Sensor;

Sensor sensores[MAX_SENSORS];
int total_sensores = 0;
pthread_mutex_t sensores_mutex = PTHREAD_MUTEX_INITIALIZER;

// ==========================
// OPERADORES
// ==========================
typedef struct {
    int socket;
    char nombre[50];
    int activo;
} Operador;

Operador operadores[MAX_OPERATORS];
int total_operadores = 0;
pthread_mutex_t operadores_mutex = PTHREAD_MUTEX_INITIALIZER;

// ==========================
// LOGGING
// ==========================
FILE *log_file;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// ==========================
// CLIENTE
// ==========================
typedef struct {
    int client_sock;
    struct sockaddr_in client_addr;
} client_info_t;

// ==========================
// FUNCIONES SENSORES
// ==========================
int buscar_sensor(const char *id) {
    for (int i = 0; i < total_sensores; i++) {
        if (strcmp(sensores[i].id, id) == 0) return i;
    }
    return -1;
}

int registrar_sensor(const char *id, const char *tipo) {
    pthread_mutex_lock(&sensores_mutex);

    if (total_sensores >= MAX_SENSORS) {
        pthread_mutex_unlock(&sensores_mutex);
        return -1;
    }

    if (buscar_sensor(id) != -1) {
        pthread_mutex_unlock(&sensores_mutex);
        return -2;
    }

    strcpy(sensores[total_sensores].id, id);
    strcpy(sensores[total_sensores].tipo, tipo);
    sensores[total_sensores].ultimo_valor = 0.0;
    sensores[total_sensores].activo = 1;

    total_sensores++;

    pthread_mutex_unlock(&sensores_mutex);
    return 0;
}

int actualizar_sensor(const char *id, float valor) {
    pthread_mutex_lock(&sensores_mutex);

    int index = buscar_sensor(id);
    if (index == -1) {
        pthread_mutex_unlock(&sensores_mutex);
        return -1;
    }

    sensores[index].ultimo_valor = valor;

    pthread_mutex_unlock(&sensores_mutex);
    return index;
}

int detectar_anomalia(const char *tipo, float valor, char *tipo_alerta) {

    if (strcmp(tipo, "temperatura") == 0 && valor > 50) {
        strcpy(tipo_alerta, "HIGH_TEMP");
        return 1;
    }

    if (strcmp(tipo, "vibracion") == 0 && valor > 80) {
        strcpy(tipo_alerta, "HIGH_VIBRATION");
        return 1;
    }

    if (strcmp(tipo, "energia") == 0 && valor > 1000) {
        strcpy(tipo_alerta, "HIGH_ENERGY");
        return 1;
    }

    return 0;
}

void obtener_lista_sensores(char *response) {
    pthread_mutex_lock(&sensores_mutex);

    strcpy(response, "SENSORS");

    for (int i = 0; i < total_sensores; i++) {
        if (sensores[i].activo) {
            strcat(response, " ");
            strcat(response, sensores[i].id);
        }
    }

    strcat(response, "\n");

    pthread_mutex_unlock(&sensores_mutex);
}

int obtener_dato_sensor(const char *id, float *valor) {
    pthread_mutex_lock(&sensores_mutex);

    int index = buscar_sensor(id);
    if (index == -1) {
        pthread_mutex_unlock(&sensores_mutex);
        return -1;
    }

    *valor = sensores[index].ultimo_valor;

    pthread_mutex_unlock(&sensores_mutex);
    return 0;
}

// ==========================
// FUNCIONES OPERADORES
// ==========================
int registrar_operador(int socket, const char *nombre) {
    pthread_mutex_lock(&operadores_mutex);

    if (total_operadores >= MAX_OPERATORS) {
        pthread_mutex_unlock(&operadores_mutex);
        return -1;
    }

    operadores[total_operadores].socket = socket;
    strcpy(operadores[total_operadores].nombre, nombre);
    operadores[total_operadores].activo = 1;

    total_operadores++;

    pthread_mutex_unlock(&operadores_mutex);
    return 0;
}

void enviar_alerta_a_operadores(const char *mensaje) {
    pthread_mutex_lock(&operadores_mutex);

    for (int i = 0; i < total_operadores; i++) {
        if (operadores[i].activo) {
            send(operadores[i].socket, mensaje, strlen(mensaje), 0);
        }
    }

    pthread_mutex_unlock(&operadores_mutex);
}

void eliminar_operador(int socket) {
    pthread_mutex_lock(&operadores_mutex);

    for (int i = 0; i < total_operadores; i++) {
        if (operadores[i].socket == socket) {
            operadores[i].activo = 0;
        }
    }

    pthread_mutex_unlock(&operadores_mutex);
}

// ==========================
// LOG
// ==========================
void write_log(const char *level, const char *client_ip, int client_port, const char *message, const char *response) {
    pthread_mutex_lock(&log_mutex);

    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);

    printf("[%s] [%s] IP:%s Port:%d Msg:%s Resp:%s\n",
           timestamp, level, client_ip, client_port,
           message ? message : "-",
           response ? response : "-");

    if (log_file) {
        fprintf(log_file, "[%s] [%s] IP:%s Port:%d Msg:%s Resp:%s\n",
                timestamp, level, client_ip, client_port,
                message ? message : "-",
                response ? response : "-");
        fflush(log_file);
    }

    pthread_mutex_unlock(&log_mutex);
}

// ==========================
// HANDLE CLIENT
// ==========================
void *handle_client(void *arg) {
    client_info_t *client = (client_info_t *)arg;
    int sock = client->client_sock;
    char buffer[BUFFER_SIZE];
    char client_ip[INET_ADDRSTRLEN];
    int client_port = ntohs(client->client_addr.sin_port);

    inet_ntop(AF_INET, &(client->client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);

    write_log("INFO", client_ip, client_port, "Cliente conectado", "OK");

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_received = recv(sock, buffer, BUFFER_SIZE - 1, 0);

        if (bytes_received <= 0) {
            eliminar_operador(sock);
            break;
        }

        buffer[bytes_received] = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0';

        char comando[50], arg1[50], arg2[50];
        memset(comando, 0, sizeof(comando));
        memset(arg1, 0, sizeof(arg1));
        memset(arg2, 0, sizeof(arg2));

        int partes = sscanf(buffer, "%s %s %s", comando, arg1, arg2);

        char response[BUFFER_SIZE];

        // REGISTER_SENSOR
        if (strcmp(comando, "REGISTER_SENSOR") == 0) {

            if (partes != 3)
                snprintf(response, sizeof(response), "ERROR FORMATO_INVALIDO\n");
            else {
                int r = registrar_sensor(arg1, arg2);

                if (r == 0)
                    snprintf(response, sizeof(response), "OK SENSOR_REGISTRADO\n");
                else if (r == -2)
                    snprintf(response, sizeof(response), "ERROR SENSOR_YA_EXISTE\n");
                else
                    snprintf(response, sizeof(response), "ERROR LIMITE_SENSORES\n");
            }
        }

        // REGISTER_OPERATOR
        else if (strcmp(comando, "REGISTER_OPERATOR") == 0) {

            if (partes != 2)
                snprintf(response, sizeof(response), "ERROR FORMATO_INVALIDO\n");
            else {
                int r = registrar_operador(sock, arg1);

                if (r == 0)
                    snprintf(response, sizeof(response), "OK OPERADOR_REGISTRADO\n");
                else
                    snprintf(response, sizeof(response), "ERROR LIMITE_OPERADORES\n");
            }
        }

        // SEND_DATA
        else if (strcmp(comando, "SEND_DATA") == 0) {

            if (partes != 3)
                snprintf(response, sizeof(response), "ERROR FORMATO_INVALIDO\n");
            else {
                float valor = atof(arg2);
                int index = actualizar_sensor(arg1, valor);

                if (index == -1) {
                    snprintf(response, sizeof(response), "ERROR SENSOR_NO_REGISTRADO\n");
                } else {
                    char tipo_alerta[50];

                    if (detectar_anomalia(sensores[index].tipo, valor, tipo_alerta)) {

                        char alerta_msg[BUFFER_SIZE];
                        snprintf(alerta_msg, sizeof(alerta_msg),
                                 "ALERT %s %s %.2f\n",
                                 arg1, tipo_alerta, valor);

                        printf("🚨 %s", alerta_msg);
                        enviar_alerta_a_operadores(alerta_msg);
                    }

                    snprintf(response, sizeof(response), "OK DATA_RECIBIDA\n");
                }
            }
        }

        // GET_SENSORS
        else if (strcmp(comando, "GET_SENSORS") == 0) {

            if (partes != 1)
                snprintf(response, sizeof(response), "ERROR FORMATO_INVALIDO\n");
            else
                obtener_lista_sensores(response);
        }

        // GET_DATA
        else if (strcmp(comando, "GET_DATA") == 0) {

            if (partes != 2)
                snprintf(response, sizeof(response), "ERROR FORMATO_INVALIDO\n");
            else {
                float valor;

                if (obtener_dato_sensor(arg1, &valor) == -1)
                    snprintf(response, sizeof(response), "ERROR SENSOR_NO_REGISTRADO\n");
                else
                    snprintf(response, sizeof(response), "DATA %s %.2f\n", arg1, valor);
            }
        }

        // GET_STATUS
        else if (strcmp(comando, "GET_STATUS") == 0) {
            snprintf(response, sizeof(response),
                     "STATUS SENSORS:%d OPERATORS:%d\n",
                     total_sensores, total_operadores);
        }

        // PING
        else if (strcmp(comando, "PING") == 0) {
            snprintf(response, sizeof(response), "PONG\n");
        }

        else {
            snprintf(response, sizeof(response), "ERROR COMANDO_INVALIDO\n");
        }

        write_log("INFO", client_ip, client_port, buffer, response);
        send(sock, response, strlen(response), 0);
    }

    close(sock);
    free(client);
    pthread_exit(NULL);
}

// ==========================
// MAIN
// ==========================
int main(int argc, char *argv[]) {

    if (argc != 3) {
        printf("Uso: %s <puerto> <log>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    log_file = fopen(argv[2], "a");

    int server_sock = socket(AF_INET, SOCK_STREAM, 0);

    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(server_sock, 10);

    printf("Servidor en puerto %d...\n", port);

    while (1) {
        client_info_t *client = malloc(sizeof(client_info_t));
        socklen_t len = sizeof(client->client_addr);

        client->client_sock = accept(server_sock,
                                     (struct sockaddr *)&client->client_addr,
                                     &len);

        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, client);
        pthread_detach(tid);
    }

    return 0;
}