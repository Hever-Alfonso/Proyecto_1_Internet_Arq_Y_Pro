/*
 * server.c
 * --------
 * Main entry point for the IoT Equipment Monitoring Server.
 *
 * Modules:
 *   equipment_state     — thread-safe equipment state + simulation
 *   logger              — centralised logging to stderr + file
 *   alert_system        — anomaly detection with ring buffer history
 *   metrics_processor   — periodic capture, alert eval, broadcast
 *   connection_handler  — client registry, sessions, command dispatch
 *   http_handler        — embedded HTTP server (web dashboard)
 *
 * Usage:
 *   ./server <port> <logfile>
 *
 * The TCP protocol server runs on <port>.
 * The HTTP web server runs on <port> + 80 (e.g. 9080 if port=9000).
 */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include "equipment_state.h"
#include "logger.h"
#include "alert_system.h"
#include "metrics_processor.h"
#include "connection_handler.h"
#include "http_handler.h"
#include "auth_service.h"

/* ── Globals ── */

#define LISTEN_BACKLOG  32

static volatile sig_atomic_t g_shutdown_requested = 0;
static int g_server_fd = -1;

/* ── Signal handler ── */

static void on_sigint(int sig) {
    (void)sig;
    g_shutdown_requested = 1;

    if (g_server_fd >= 0) {
        shutdown(g_server_fd, SHUT_RDWR);
        close(g_server_fd);
        g_server_fd = -1;
    }
}

/* ── Main ── */

int main(int argc, char *argv[]) {

    /* ---- 1. Parse arguments ---- */

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <port> <logfile>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Error: invalid port '%s'\n", argv[1]);
        return 1;
    }

    int http_port = port + 80;

    /* ---- 2. Initialise modules ---- */

    if (logger_init(argv[2]) < 0) {
        fprintf(stderr, "Warning: could not open log file '%s', "
                        "logging to stderr only\n", argv[2]);
    }

    equipment_state_init();
    alert_system_init();
    auth_service_init(NULL);   /* reads users.conf (external identity store) */

    metrics_set_broadcast_fn(connection_broadcast);
    metrics_set_interval(5);

    /* ---- 3. Create listening socket ---- */

    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) {
        logger_write(NULL, "FATAL: socket() failed: %s", strerror(errno));
        return 1;
    }

    int opt = 1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in srv_addr;
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family      = AF_INET;
    srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    srv_addr.sin_port        = htons((uint16_t)port);

    if (bind(g_server_fd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
        logger_write(NULL, "FATAL: bind() failed: %s", strerror(errno));
        close(g_server_fd);
        return 1;
    }

    if (listen(g_server_fd, LISTEN_BACKLOG) < 0) {
        logger_write(NULL, "FATAL: listen() failed: %s", strerror(errno));
        close(g_server_fd);
        return 1;
    }

    /* ---- 4. Start metrics broadcast thread ---- */

    pthread_t metrics_tid;
    pthread_create(&metrics_tid, NULL, metrics_broadcast_thread, NULL);

    /* ---- 5. Start HTTP server ---- */

    if (http_server_start(http_port) < 0) {
        logger_write(NULL, "WARNING: HTTP server failed to start on port %d", http_port);
    }

    /* ---- 6. Install signal handler and enter accept loop ---- */

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    logger_write(NULL, "Server listening on port %d (Ctrl+C to stop)", port);
    logger_write(NULL, "HTTP dashboard on http://localhost:%d", http_port);

    while (!g_shutdown_requested) {
        struct sockaddr_in cli_addr;
        socklen_t addr_len = sizeof(cli_addr);

        int cli_fd = accept(g_server_fd,
                            (struct sockaddr *)&cli_addr, &addr_len);

        if (cli_fd < 0) {
            if (errno == EINTR || g_shutdown_requested) break;
            if (errno == EBADF) break;
            logger_write(NULL, "accept() error: %s", strerror(errno));
            break;
        }

        client_node_t *node = calloc(1, sizeof(*node));
        if (!node) {
            logger_write(NULL, "calloc() failed — dropping connection");
            close(cli_fd);
            continue;
        }

        node->fd   = cli_fd;
        node->addr = cli_addr;
        node->role = ROLE_SUPERVISOR;
        node->name[0] = '\0';

        connection_registry_add(node);

        pthread_t th;
        pthread_create(&th, NULL, connection_handle_session, node);
        pthread_detach(th);
    }

    /* ---- 7. Graceful shutdown ---- */

    logger_write(NULL, "Shutdown requested — cleaning up...");

    http_server_stop();
    metrics_request_stop();
    pthread_join(metrics_tid, NULL);

    connection_registry_shutdown();

    if (g_server_fd >= 0) {
        close(g_server_fd);
        g_server_fd = -1;
    }

    alert_system_destroy();
    auth_service_destroy();
    equipment_state_destroy();
    logger_write(NULL, "Server stopped.");
    logger_close();

    return 0;
}