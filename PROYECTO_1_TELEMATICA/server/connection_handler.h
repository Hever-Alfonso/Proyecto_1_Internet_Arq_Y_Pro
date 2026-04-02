/*
 * connection_handler.h
 * --------------------
 * Client registry, session management, and command processing.
 *
 * Replaces the old client_t linked list, add_client(), remove_client(),
 * list_users_to(), and client_thread() that were all inside server.c.
 *
 * Responsibilities:
 *   - Maintain a thread-safe linked list of connected clients
 *   - Handle per-client sessions (1 thread each)
 *   - Parse and dispatch every protocol command
 *   - Provide the broadcast function that metrics_processor uses
 *   - Support two roles: SUPERVISOR (read-only) and ENGINEER (control)
 *
 * Protocol commands handled:
 *   HELLO [name=<text>]
 *   AUTHENTICATE <user> <pass>
 *   ROLE?
 *   LIST USERS                       (ENGINEER only)
 *   MODIFY_RPM <delta>               (ENGINEER only)
 *   ADJUST_HEADING <LEFT|RIGHT>      (ENGINEER only)
 *   GET_STATUS
 *   GET_ALERTS
 *   QUIT
 */

#ifndef CONNECTION_HANDLER_H
#define CONNECTION_HANDLER_H

#include <netinet/in.h>
#include <stddef.h>

/* ── Roles ── */
typedef enum {
    ROLE_SUPERVISOR = 0,   /* read-only: receives metrics + alerts       */
    ROLE_ENGINEER   = 1    /* full access: can also control equipment    */
} client_role_t;

/* ── Client node (linked list) ── */
typedef struct client_node {
    int                   fd;
    struct sockaddr_in    addr;
    client_role_t         role;
    char                  name[64];
    struct client_node   *next;
} client_node_t;

/* ── Registry ── */

/*  Add a freshly accepted client to the list.
 *  The node must be heap-allocated by the caller. */
void  connection_registry_add(client_node_t *node);

/*  Remove a client by fd, close the fd, and free the node. */
void  connection_registry_remove(int fd);

/*  Broadcast a message to every connected client.
 *  This is the function registered with metrics_set_broadcast_fn(). */
void  connection_broadcast(const char *message, size_t length);

/*  Return the current number of connected clients. */
int   connection_registry_count(void);

/* ── Session thread ──
 *
 *  Entry point for pthread_create().
 *  arg is a pointer to the client_node_t already inserted in the list.
 *  The thread detaches itself and cleans up on exit.                   */
void *connection_handle_session(void *arg);

/* ── Cleanup ──
 *
 *  Close all client fds and free every node.
 *  Called once during server shutdown.  */
void  connection_registry_shutdown(void);

#endif /* CONNECTION_HANDLER_H */