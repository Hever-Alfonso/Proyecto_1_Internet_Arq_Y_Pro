/*
 * metrics_processor.c
 * -------------------
 * Implementation of the periodic metrics capture and broadcast module.
 *
 * Each cycle (default 5 s):
 *   1. equipment_simulate_tick()   — advance wear / temperature / pressure
 *   2. equipment_state_read()      — take a consistent snapshot
 *   3. Format a METRIC line and broadcast it
 *   4. alert_evaluate()            — check thresholds on the snapshot
 *   5. Format and broadcast any ALERT lines
 *   6. Log everything
 *   7. Sleep (interruptible in 1-second slices)
 */

#define _GNU_SOURCE

#include "metrics_processor.h"
#include "equipment_state.h"
#include "alert_system.h"
#include "logger.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ── Internal state ── */

static broadcast_fn_t  g_broadcast   = NULL;
static int             g_interval_s  = 5;
static atomic_int      g_stop_flag   = 0;

/* ── Configuration ── */

void metrics_set_broadcast_fn(broadcast_fn_t fn) {
    g_broadcast = fn;
}

void metrics_set_interval(int seconds) {
    if (seconds < 1) seconds = 1;
    g_interval_s = seconds;
}

/* ── Control ── */

void metrics_request_stop(void) {
    atomic_store(&g_stop_flag, 1);
}

/* ── Helpers ── */

static void now_timestamp(char *buf, size_t sz) {
    time_t     t = time(NULL);
    struct tm  tm_buf;
    localtime_r(&t, &tm_buf);
    strftime(buf, sz, "%Y-%m-%d %H:%M:%S", &tm_buf);
}

/* ── Thread entry point ── */

void *metrics_broadcast_thread(void *arg) {
    (void)arg;

    /* Maximum alerts per cycle — one per metric type is more than enough. */
    #define MAX_CYCLE_ALERTS 8
    alert_record_t alerts[MAX_CYCLE_ALERTS];

    logger_write(NULL, "metrics_processor started (interval=%ds)", g_interval_s);

    while (!atomic_load(&g_stop_flag)) {

        /* 1. Advance simulation */
        equipment_simulate_tick();

        /* 2. Capture snapshot */
        equipment_snapshot_t snap;
        equipment_state_read(&snap);

        /* 3. Format and broadcast METRIC line */
        {
            char ts[32];
            now_timestamp(ts, sizeof(ts));

            char line[512];
            int len = snprintf(line, sizeof(line),
                "METRIC rpm=%d|load=%d|temp=%d|pressure=%d|heading=%s|ts=%s\n",
                snap.equipment_rpm,
                snap.system_load_percent,
                snap.coolant_temperature,
                snap.hydraulic_pressure,
                heading_to_string(snap.equipment_heading),
                ts);

            if (g_broadcast && len > 0)
                g_broadcast(line, (size_t)len);

            logger_write(NULL, "METRIC rpm=%d load=%d temp=%d pres=%d hdg=%s",
                         snap.equipment_rpm,
                         snap.system_load_percent,
                         snap.coolant_temperature,
                         snap.hydraulic_pressure,
                         heading_to_string(snap.equipment_heading));
        }

        /* 4. Evaluate alerts */
        int alert_count = alert_evaluate(&snap, alerts, MAX_CYCLE_ALERTS);

        /* 5. Format and broadcast each alert */
        for (int i = 0; i < alert_count; i++) {
            char alert_line[256];
            int alen = alert_format(&alerts[i], alert_line, sizeof(alert_line));

            if (g_broadcast && alen > 0)
                g_broadcast(alert_line, (size_t)alen);

            logger_write(NULL, "ALERT [%s] %s: %s",
                         alert_severity_string(alerts[i].severity),
                         alerts[i].type,
                         alerts[i].message);
        }

        /* 6. Interruptible sleep (1-second slices) */
        for (int i = 0; i < g_interval_s && !atomic_load(&g_stop_flag); i++)
            sleep(1);
    }

    logger_write(NULL, "metrics_processor stopped");
    return NULL;

    #undef MAX_CYCLE_ALERTS
}