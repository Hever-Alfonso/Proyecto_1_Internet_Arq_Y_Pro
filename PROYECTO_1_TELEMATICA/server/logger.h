/*
 * logger.h
 * --------
 * Centralised, thread-safe logging for the IoT monitoring server.
 *
 * Every log entry carries:
 *   [YYYY-MM-DD HH:MM:SS] <peer_ip:port> <message>
 *
 * Output goes to stderr (always) and to an optional log file that is
 * opened once at startup with logger_init() and closed with
 * logger_close().
 *
 * All public functions acquire an internal mutex, so callers from any
 * thread can log without external synchronisation.
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <netinet/in.h>   /* struct sockaddr_in */
#include <stddef.h>       /* size_t             */

/* ── Lifecycle ── */

/*  Open the log file in append mode.
 *  Pass NULL to disable file logging (stderr-only). */
int   logger_init(const char *filepath);

/*  Flush and close the log file.  Safe to call even if logger_init()
 *  was never called or was called with NULL. */
void  logger_close(void);

/* ── Logging ── */

/*  Write a free-form log line.
 *  peer  — human-readable "ip:port" string, or NULL / "-" for server
 *          events that have no associated client.
 *  fmt   — printf-style format string followed by its arguments.       */
void  logger_write(const char *peer, const char *fmt, ...);

/* ── Convenience ── */

/*  Build an "ip:port" string from a sockaddr_in.
 *  Writes at most out_sz bytes (including '\0'). */
void  logger_peer_id(const struct sockaddr_in *addr, char *out, size_t out_sz);

#endif /* LOGGER_H */