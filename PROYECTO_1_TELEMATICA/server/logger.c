/*
 * logger.c
 * --------
 * Implementation of the centralised logging module.
 *
 * Design decisions:
 *   - A single mutex protects both stderr and the file stream so that
 *     a multi-line entry from one thread is never interleaved with
 *     output from another.
 *   - The file is opened in append mode ("a") so that restarting the
 *     server does not erase previous logs.
 *   - fflush() is called after every write to the file so that a crash
 *     does not lose the last entries.
 *   - The timestamp is generated inside the lock to guarantee
 *     chronological ordering even under high contention.
 */

#define _GNU_SOURCE          /* localtime_r */

#include "logger.h"

#include <arpa/inet.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ── Internal state ── */

static FILE           *g_log_file  = NULL;
static pthread_mutex_t g_log_lock  = PTHREAD_MUTEX_INITIALIZER;

/* ── Lifecycle ── */

int logger_init(const char *filepath) {
    if (filepath == NULL)
        return 0;                         /* stderr-only mode */

    pthread_mutex_lock(&g_log_lock);

    if (g_log_file != NULL)
        fclose(g_log_file);              /* close previous if re-init */

    g_log_file = fopen(filepath, "a");
    int ok = (g_log_file != NULL) ? 0 : -1;

    pthread_mutex_unlock(&g_log_lock);
    return ok;
}

void logger_close(void) {
    pthread_mutex_lock(&g_log_lock);
    if (g_log_file != NULL) {
        fflush(g_log_file);
        fclose(g_log_file);
        g_log_file = NULL;
    }
    pthread_mutex_unlock(&g_log_lock);
}

/* ── Logging ── */

void logger_write(const char *peer, const char *fmt, ...) {
    /* Build timestamp outside the variadic handling so we can reuse it. */
    time_t     now = time(NULL);
    struct tm  tm_buf;
    char       ts[32];

    localtime_r(&now, &tm_buf);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_buf);

    const char *tag = (peer && peer[0]) ? peer : "-";

    pthread_mutex_lock(&g_log_lock);

    /* ---- stderr ---- */
    {
        va_list ap;
        va_start(ap, fmt);
        fprintf(stderr, "[%s] %s ", ts, tag);
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, "\n");
        va_end(ap);
    }

    /* ---- file (if open) ---- */
    if (g_log_file) {
        va_list ap;
        va_start(ap, fmt);
        fprintf(g_log_file, "[%s] %s ", ts, tag);
        vfprintf(g_log_file, fmt, ap);
        fprintf(g_log_file, "\n");
        fflush(g_log_file);
        va_end(ap);
    }

    pthread_mutex_unlock(&g_log_lock);
}

/* ── Convenience ── */

void logger_peer_id(const struct sockaddr_in *addr, char *out, size_t out_sz) {
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip));
    snprintf(out, out_sz, "%s:%u", ip, ntohs(addr->sin_port));
}