/*
 * connection_handler.c
 * --------------------
 * Implementation of client session management and command processing.
 *
 * Each accepted client gets its own thread running
 * connection_handle_session().  The thread:
 *
 *   1. Sends a welcome message
 *   2. Enters a recv() loop, splitting input on '\n'
 *   3. Dispatches each line to the appropriate command handler
 *   4. On disconnect or QUIT, removes itself from the registry
 *
 * The client linked list is protected by g_registry_lock.
 * The broadcast function iterates the list under that same lock.
 *
 * Supported commands:
 *   HELLO [name=<text>]
 *   AUTHENTICATE <user> <pass>
 *   ROLE?
 *   LIST USERS                   (ENGINEER)
 *   MODIFY_RPM <delta>           (ENGINEER)
 *   ADJUST_HEADING <LEFT|RIGHT>  (ENGINEER)
 *   GET_STATUS
 *   GET_ALERTS
 *   QUIT
 */

#define _GNU_SOURCE

#include "connection_handler.h"
#include "equipment_state.h"
#include "alert_system.h"
#include "logger.h"

#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* ── Registry internals ── */

static client_node_t   *g_registry_head = NULL;
static pthread_mutex_t  g_registry_lock = PTHREAD_MUTEX_INITIALIZER;

/* ── Authentication via external service ── */
#include "auth_service.h"

/* ── Registry public API ── */

void connection_registry_add(client_node_t *node) {
    pthread_mutex_lock(&g_registry_lock);
    node->next = g_registry_head;
    g_registry_head = node;
    pthread_mutex_unlock(&g_registry_lock);
}

void connection_registry_remove(int fd) {
    pthread_mutex_lock(&g_registry_lock);

    client_node_t **pp = &g_registry_head;
    client_node_t  *cur = g_registry_head;

    while (cur) {
        if (cur->fd == fd) {
            *pp = cur->next;
            close(cur->fd);
            free(cur);
            break;
        }
        pp  = &cur->next;
        cur = cur->next;
    }

    pthread_mutex_unlock(&g_registry_lock);
}

void connection_broadcast(const char *message, size_t length) {
    pthread_mutex_lock(&g_registry_lock);

    for (client_node_t *c = g_registry_head; c; c = c->next)
        send(c->fd, message, length, MSG_NOSIGNAL);

    pthread_mutex_unlock(&g_registry_lock);
}

int connection_registry_count(void) {
    int count = 0;
    pthread_mutex_lock(&g_registry_lock);
    for (client_node_t *c = g_registry_head; c; c = c->next)
        count++;
    pthread_mutex_unlock(&g_registry_lock);
    return count;
}

void connection_registry_shutdown(void) {
    pthread_mutex_lock(&g_registry_lock);

    client_node_t *c = g_registry_head;
    while (c) {
        client_node_t *next = c->next;
        close(c->fd);
        free(c);
        c = next;
    }
    g_registry_head = NULL;

    pthread_mutex_unlock(&g_registry_lock);
}

/* ──────────────────────────────────────────────────────────
 *  Command handlers (all receive the session node + peer id)
 * ────────────────────────────────────────────────────────── */

static void cmd_hello(client_node_t *cli, const char *args, const char *peer) {
    const char *key = strstr(args, "name=");
    if (key) {
        key += 5;
        while (*key == ' ') key++;
        strncpy(cli->name, key, sizeof(cli->name) - 1);
        cli->name[sizeof(cli->name) - 1] = '\0';
    }

    const char *display = cli->name[0] ? cli->name : "supervisor";
    dprintf(cli->fd, "OK hello %s\n", display);
    logger_write(peer, "HELLO name=%s", display);
}

static void cmd_authenticate(client_node_t *cli, const char *args,
                              const char *peer) {
    char user[64] = {0};
    char pass[64] = {0};

    if (sscanf(args, "%63s %63s", user, pass) != 2) {
        dprintf(cli->fd, "ERR invalid_credentials\n");
        logger_write(peer, "AUTH failed — missing arguments");
        return;
    }

    /* Query external identity service */
    int role = auth_service_validate(user, pass);

    if (role == AUTH_ROLE_ENGINEER) {
        cli->role = ROLE_ENGINEER;
        dprintf(cli->fd, "OK authenticated\n");
        logger_write(peer, "AUTH success role=ENGINEER (via external service)");
    } else if (role == AUTH_ROLE_SUPERVISOR) {
        cli->role = ROLE_SUPERVISOR;
        dprintf(cli->fd, "OK authenticated\n");
        logger_write(peer, "AUTH success role=SUPERVISOR (via external service)");
    } else {
        dprintf(cli->fd, "ERR invalid_credentials\n");
        logger_write(peer, "AUTH failed user=%s (external service rejected)", user);
    }
}

static void cmd_role(client_node_t *cli, const char *peer) {
    const char *role_str = (cli->role == ROLE_ENGINEER) ? "ENGINEER"
                                                        : "SUPERVISOR";
    dprintf(cli->fd, "OK %s\n", role_str);
    logger_write(peer, "ROLE? -> %s", role_str);
}

static void cmd_list_users(client_node_t *cli, const char *peer) {
    if (cli->role != ROLE_ENGINEER) {
        dprintf(cli->fd, "ERR forbidden\n");
        logger_write(peer, "LIST USERS denied (supervisor)");
        return;
    }

    pthread_mutex_lock(&g_registry_lock);

    int count = 0;
    for (client_node_t *c = g_registry_head; c; c = c->next)
        count++;

    dprintf(cli->fd, "OK %d users\n", count);

    for (client_node_t *c = g_registry_head; c; c = c->next) {
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &c->addr.sin_addr, ip, sizeof(ip));
        const char *r = (c->role == ROLE_ENGINEER) ? "ENGINEER" : "SUPERVISOR";
        const char *n = c->name[0] ? c->name : "-";
        dprintf(cli->fd, "USER %s:%u ROLE=%s NAME=%s\n",
                ip, ntohs(c->addr.sin_port), r, n);
    }

    pthread_mutex_unlock(&g_registry_lock);
    logger_write(peer, "LIST USERS -> %d", count);
}

static void cmd_modify_rpm(client_node_t *cli, const char *args,
                            const char *peer) {
    if (cli->role != ROLE_ENGINEER) {
        dprintf(cli->fd, "ERR forbidden\n");
        logger_write(peer, "MODIFY_RPM denied (supervisor)");
        return;
    }

    int delta = 0;
    if (sscanf(args, "%d", &delta) != 1) {
        dprintf(cli->fd, "ERR invalid_value\n");
        logger_write(peer, "MODIFY_RPM bad arg: %s", args);
        return;
    }

    char reason[64];
    int ok = equipment_modify_rpm(delta, reason, sizeof(reason));
    dprintf(cli->fd, "%s %s\n", ok ? "OK" : "ERR", reason);
    logger_write(peer, "MODIFY_RPM delta=%d -> %s %s",
                 delta, ok ? "OK" : "ERR", reason);
}

static void cmd_adjust_heading(client_node_t *cli, const char *args,
                                const char *peer) {
    if (cli->role != ROLE_ENGINEER) {
        dprintf(cli->fd, "ERR forbidden\n");
        logger_write(peer, "ADJUST_HEADING denied (supervisor)");
        return;
    }

    int direction = 0;

    if (strstr(args, "LEFT"))       direction = -1;
    else if (strstr(args, "RIGHT")) direction = +1;
    else {
        dprintf(cli->fd, "ERR invalid_value\n");
        logger_write(peer, "ADJUST_HEADING bad arg: %s", args);
        return;
    }

    equipment_adjust_heading(direction);

    equipment_snapshot_t snap;
    equipment_state_read(&snap);
    const char *hdg = heading_to_string(snap.equipment_heading);

    dprintf(cli->fd, "OK heading=%s\n", hdg);
    logger_write(peer, "ADJUST_HEADING -> %s", hdg);
}

static void cmd_get_status(client_node_t *cli, const char *peer) {
    equipment_snapshot_t snap;
    equipment_state_read(&snap);

    dprintf(cli->fd, "STATUS rpm=%d|load=%d|temp=%d|pressure=%d|heading=%s\n",
            snap.equipment_rpm,
            snap.system_load_percent,
            snap.coolant_temperature,
            snap.hydraulic_pressure,
            heading_to_string(snap.equipment_heading));

    logger_write(peer, "GET_STATUS -> rpm=%d load=%d temp=%d pres=%d hdg=%s",
                 snap.equipment_rpm,
                 snap.system_load_percent,
                 snap.coolant_temperature,
                 snap.hydraulic_pressure,
                 heading_to_string(snap.equipment_heading));
}

static void cmd_get_alerts(client_node_t *cli, const char *peer) {
    #define MAX_HIST 16
    alert_record_t history[MAX_HIST];
    int count = alert_history_copy(history, MAX_HIST);

    dprintf(cli->fd, "OK %d alerts\n", count);

    for (int i = 0; i < count; i++) {
        char line[256];
        alert_format(&history[i], line, sizeof(line));
        send(cli->fd, line, strlen(line), MSG_NOSIGNAL);
    }

    logger_write(peer, "GET_ALERTS -> %d records", count);
    #undef MAX_HIST
}

/* ──────────────────────────────────────────────────────────
 *  Command dispatcher
 * ────────────────────────────────────────────────────────── */

/*  Returns 1 if the session should continue, 0 on QUIT. */
static int dispatch_command(client_node_t *cli, char *line,
                            const char *peer) {

    /* Strip trailing \r if present (telnet compatibility) */
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\r')
        line[--len] = '\0';

    logger_write(peer, "REQ: %s", line);

    if (strcmp(line, "QUIT") == 0) {
        dprintf(cli->fd, "BYE\n");
        logger_write(peer, "RESP: BYE");
        return 0;

    } else if (strncmp(line, "HELLO", 5) == 0) {
        cmd_hello(cli, line + 5, peer);

    } else if (strncmp(line, "AUTHENTICATE ", 13) == 0) {
        cmd_authenticate(cli, line + 13, peer);

    } else if (strcmp(line, "ROLE?") == 0) {
        cmd_role(cli, peer);

    } else if (strcmp(line, "LIST USERS") == 0) {
        cmd_list_users(cli, peer);

    } else if (strncmp(line, "MODIFY_RPM ", 11) == 0) {
        cmd_modify_rpm(cli, line + 11, peer);

    } else if (strncmp(line, "ADJUST_HEADING ", 15) == 0) {
        cmd_adjust_heading(cli, line + 15, peer);

    } else if (strcmp(line, "GET_STATUS") == 0) {
        cmd_get_status(cli, peer);

    } else if (strcmp(line, "GET_ALERTS") == 0) {
        cmd_get_alerts(cli, peer);

    } else if (strncmp(line, "SENSOR_DATA ", 12) == 0) {
        /* Sensor readings are logged but not responded to —
           the server just acknowledges receipt silently. */
        logger_write(peer, "SENSOR: %s", line + 12);
        dprintf(cli->fd, "OK sensor_received\n");

    } else {
        dprintf(cli->fd, "ERR unknown_command\n");
    }

    return 1;
}

/* ──────────────────────────────────────────────────────────
 *  Session thread
 * ────────────────────────────────────────────────────────── */

void *connection_handle_session(void *arg) {
    client_node_t *cli = (client_node_t *)arg;

    char peer[80];
    logger_peer_id(&cli->addr, peer, sizeof(peer));
    logger_write(peer, "session_start (fd=%d)", cli->fd);

    /* Welcome */
    dprintf(cli->fd,
        "OK Welcome to IoT Equipment Monitor. "
        "Commands: HELLO|AUTHENTICATE|ROLE?|LIST USERS|"
        "MODIFY_RPM|ADJUST_HEADING|GET_STATUS|GET_ALERTS|QUIT\n");

    /* Receive loop — accumulate data, split on '\n' */
    char buf[4096];
    char carry[4096];
    int  carry_len = 0;

    for (;;) {
        ssize_t n = recv(cli->fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;          /* disconnect or error */
        buf[n] = '\0';

        /* Prepend any leftover from previous recv */
        char combined[8192];
        if (carry_len > 0) {
            memcpy(combined, carry, carry_len);
            memcpy(combined + carry_len, buf, n + 1);  /* +1 for '\0' */
        } else {
            memcpy(combined, buf, n + 1);
        }
        carry_len = 0;

        /* Split on newlines */
        char *p   = combined;
        char *nl;

        while ((nl = strchr(p, '\n')) != NULL) {
            *nl = '\0';

            if (!dispatch_command(cli, p, peer))
                goto session_end;

            p = nl + 1;
        }

        /* Save any incomplete trailing data for next recv */
        size_t remaining = strlen(p);
        if (remaining > 0 && remaining < sizeof(carry)) {
            memcpy(carry, p, remaining);
            carry_len = (int)remaining;
        }
    }

session_end:
    logger_write(peer, "session_end (fd=%d)", cli->fd);
    connection_registry_remove(cli->fd);
    return NULL;
}