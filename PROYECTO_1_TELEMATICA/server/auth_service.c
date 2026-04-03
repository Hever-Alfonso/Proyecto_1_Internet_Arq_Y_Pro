/*
 * auth_service.c
 * --------------
 * External authentication service implementation.
 *
 * Reads the credentials file on every auth attempt (simulating a
 * query to a remote identity service).  This means:
 *   - Users are NOT stored in server memory
 *   - The file can be updated without restarting the server
 *   - In Docker, the file can be mounted as a volume
 *
 * File format (one user per line):
 *   username:password:role
 *
 * Valid roles: ENGINEER, SUPERVISOR
 * Lines starting with # are comments.
 * Empty lines are ignored.
 */

#define _GNU_SOURCE

#include "auth_service.h"
#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Internal state ── */

#define DEFAULT_AUTH_FILE "users.conf"
#define MAX_LINE 256

static char g_auth_filepath[512] = {0};

/* ── Lifecycle ── */

int auth_service_init(const char *filepath) {
    if (filepath && filepath[0]) {
        strncpy(g_auth_filepath, filepath, sizeof(g_auth_filepath) - 1);
    } else {
        strncpy(g_auth_filepath, DEFAULT_AUTH_FILE, sizeof(g_auth_filepath) - 1);
    }
    g_auth_filepath[sizeof(g_auth_filepath) - 1] = '\0';

    /* Verify the file exists and is readable */
    FILE *f = fopen(g_auth_filepath, "r");
    if (!f) {
        logger_write(NULL, "AUTH_SERVICE: cannot open '%s', "
                     "creating default credentials file", g_auth_filepath);

        /* Create a default credentials file */
        f = fopen(g_auth_filepath, "w");
        if (!f) {
            logger_write(NULL, "AUTH_SERVICE: cannot create '%s'", g_auth_filepath);
            return -1;
        }
        fprintf(f, "# IoT Monitoring System - External Identity Store\n");
        fprintf(f, "# Format: username:password:role\n");
        fprintf(f, "# Roles: ENGINEER, SUPERVISOR\n");
        fprintf(f, "#\n");
        fprintf(f, "engineer:eng2026:ENGINEER\n");
        fprintf(f, "supervisor:sup2026:SUPERVISOR\n");
        fprintf(f, "operator1:op123:SUPERVISOR\n");
        fclose(f);

        logger_write(NULL, "AUTH_SERVICE: created default '%s' with 3 users",
                     g_auth_filepath);
        return 0;
    }

    /* Count users for logging */
    int user_count = 0;
    char line[MAX_LINE];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        user_count++;
    }
    fclose(f);

    logger_write(NULL, "AUTH_SERVICE: loaded '%s' (%d users)",
                 g_auth_filepath, user_count);
    return 0;
}

void auth_service_destroy(void) {
    /* Nothing to clean up — file is re-read each time */
    g_auth_filepath[0] = '\0';
}

/* ── Validation ── */

int auth_service_validate(const char *username, const char *password) {
    if (!username || !password || !g_auth_filepath[0])
        return AUTH_ROLE_NONE;

    FILE *f = fopen(g_auth_filepath, "r");
    if (!f) {
        logger_write(NULL, "AUTH_SERVICE: cannot open '%s' for validation",
                     g_auth_filepath);
        return AUTH_ROLE_NONE;
    }

    char line[MAX_LINE];
    int result = AUTH_ROLE_NONE;

    while (fgets(line, sizeof(line), f)) {
        /* Strip newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        /* Skip comments and empty lines */
        if (line[0] == '#' || line[0] == '\0')
            continue;

        /* Parse: username:password:role */
        char file_user[64] = {0};
        char file_pass[64] = {0};
        char file_role[32] = {0};

        char *tok1 = strtok(line, ":");
        char *tok2 = strtok(NULL, ":");
        char *tok3 = strtok(NULL, ":");

        if (!tok1 || !tok2 || !tok3) continue;

        strncpy(file_user, tok1, sizeof(file_user) - 1);
        strncpy(file_pass, tok2, sizeof(file_pass) - 1);
        strncpy(file_role, tok3, sizeof(file_role) - 1);

        if (strcmp(username, file_user) == 0 &&
            strcmp(password, file_pass) == 0) {

            if (strcmp(file_role, "ENGINEER") == 0) {
                result = AUTH_ROLE_ENGINEER;
            } else {
                result = AUTH_ROLE_SUPERVISOR;
            }

            logger_write(NULL, "AUTH_SERVICE: user '%s' authenticated as %s",
                         username, file_role);
            break;
        }
    }

    fclose(f);

    if (result == AUTH_ROLE_NONE) {
        logger_write(NULL, "AUTH_SERVICE: user '%s' authentication FAILED",
                     username);
    }

    return result;
}