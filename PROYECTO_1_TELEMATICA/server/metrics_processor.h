/*
 * metrics_processor.h
 * -------------------
 * Periodic capture, formatting, and broadcast of equipment metrics.
 *
 * Replaces the old broadcast_tlm() + telemetry_thread() that lived
 * inside server.c.  This module:
 *
 *   1. Captures an equipment_snapshot_t every cycle.
 *   2. Runs alert_evaluate() on that snapshot.
 *   3. Formats a METRIC line for broadcast to all connected clients.
 *   4. Formats any ALERT lines and broadcasts those too.
 *   5. Calls equipment_simulate_tick() to advance the simulation.
 *
 * Protocol lines produced:
 *   METRIC rpm=<int>|load=<int>|temp=<int>|pressure=<int>|heading=<char>|ts=<timestamp>
 *   ALERT  <severity>|<type>|<message>
 *
 * The broadcast function is provided by the caller (connection_handler)
 * through metrics_set_broadcast_fn() so that this module has no direct
 * dependency on the client list.
 */

#ifndef METRICS_PROCESSOR_H
#define METRICS_PROCESSOR_H

#include <stddef.h>

/* ── Broadcast callback type ──
 *
 *  The connection_handler module registers a function with this
 *  signature.  metrics_processor calls it whenever it has a line
 *  to send to every connected client.                              */
typedef void (*broadcast_fn_t)(const char *message, size_t length);

/* ── Configuration ── */

/*  Register the function that will send data to all clients. */
void  metrics_set_broadcast_fn(broadcast_fn_t fn);

/*  Set the interval between broadcast cycles in seconds.
 *  Default is 5.  Minimum is 1. */
void  metrics_set_interval(int seconds);

/* ── Thread entry point ──
 *
 *  Designed to be passed directly to pthread_create().
 *  The arg parameter is unused (pass NULL).
 *  The thread runs until metrics_request_stop() is called.         */
void *metrics_broadcast_thread(void *arg);

/* ── Control ── */

/*  Signal the broadcast thread to finish after its current sleep. */
void  metrics_request_stop(void);

#endif /* METRICS_PROCESSOR_H */