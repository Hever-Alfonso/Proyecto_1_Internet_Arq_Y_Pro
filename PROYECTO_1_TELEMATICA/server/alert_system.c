/*
 * alert_system.c
 * --------------
 * Implementation of the anomaly detection and alert module.
 *
 * Thresholds (configurable via the defines below):
 *
 *   Metric            Warning          Critical
 *   ─────────────     ──────────       ──────────
 *   RPM               > 4000           > 4500
 *   System load       < 20 %           < 10 %
 *   Coolant temp      > 70 °C          > 85 °C
 *   Pressure          > 80 bar         > 90 bar
 *   Pressure (low)    < 15 bar         < 8  bar
 *
 * A ring buffer of ALERT_HISTORY_SIZE entries keeps the most recent
 * alerts so that a client that connects mid-session can request a
 * catch-up.  The buffer is protected by its own mutex.
 */

#define _GNU_SOURCE

#include "alert_system.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* ── Threshold defines ── */

#define RPM_WARN            4000
#define RPM_CRIT            4500

#define LOAD_WARN             20
#define LOAD_CRIT             10

#define TEMP_WARN             70
#define TEMP_CRIT             85

#define PRESSURE_HIGH_WARN    80
#define PRESSURE_HIGH_CRIT    90
#define PRESSURE_LOW_WARN     15
#define PRESSURE_LOW_CRIT      8

/* ── Ring buffer ── */

#define ALERT_HISTORY_SIZE    64

static alert_record_t  g_history[ALERT_HISTORY_SIZE];
static int             g_history_count = 0;   /* total inserted      */
static int             g_history_head  = 0;   /* next write position */
static pthread_mutex_t g_history_lock  = PTHREAD_MUTEX_INITIALIZER;

/* ── Internal helpers ── */

static void now_timestamp(char *buf, size_t sz) {
    time_t     t = time(NULL);
    struct tm  tm_buf;
    localtime_r(&t, &tm_buf);
    strftime(buf, sz, "%Y-%m-%d %H:%M:%S", &tm_buf);
}

/*  Push one alert into the ring buffer. */
static void history_push(const alert_record_t *rec) {
    pthread_mutex_lock(&g_history_lock);
    g_history[g_history_head] = *rec;
    g_history_head = (g_history_head + 1) % ALERT_HISTORY_SIZE;
    g_history_count++;
    pthread_mutex_unlock(&g_history_lock);
}

/*  Build an alert_record_t and push it into the ring buffer.
 *  Returns a copy through *out so the caller can also broadcast it. */
static void emit_alert(alert_severity_t severity,
                       const char *type,
                       const char *message,
                       alert_record_t *out) {
    out->severity = severity;
    strncpy(out->type,    type,    sizeof(out->type)    - 1);
    out->type[sizeof(out->type) - 1] = '\0';
    strncpy(out->message, message, sizeof(out->message) - 1);
    out->message[sizeof(out->message) - 1] = '\0';
    now_timestamp(out->timestamp, sizeof(out->timestamp));

    history_push(out);
}

/* ── Lifecycle ── */

void alert_system_init(void) {
    pthread_mutex_lock(&g_history_lock);
    memset(g_history, 0, sizeof(g_history));
    g_history_count = 0;
    g_history_head  = 0;
    pthread_mutex_unlock(&g_history_lock);
}

void alert_system_destroy(void) {
    pthread_mutex_destroy(&g_history_lock);
}

/* ── Core evaluation ── */

int alert_evaluate(const equipment_snapshot_t *snap,
                   alert_record_t *out_alerts, int max_alerts) {
    int n = 0;
    char msg[128];

    /* --- RPM --- */
    if (n < max_alerts && snap->equipment_rpm > RPM_CRIT) {
        snprintf(msg, sizeof(msg),
                 "RPM at %d — exceeds critical limit (%d)",
                 snap->equipment_rpm, RPM_CRIT);
        emit_alert(ALERT_CRITICAL, "rpm_high", msg, &out_alerts[n++]);
    } else if (n < max_alerts && snap->equipment_rpm > RPM_WARN) {
        snprintf(msg, sizeof(msg),
                 "RPM at %d — approaching limit (%d)",
                 snap->equipment_rpm, RPM_WARN);
        emit_alert(ALERT_WARNING, "rpm_high", msg, &out_alerts[n++]);
    }

    /* --- System load --- */
    if (n < max_alerts && snap->system_load_percent < LOAD_CRIT) {
        snprintf(msg, sizeof(msg),
                 "System load at %d%% — critically low",
                 snap->system_load_percent);
        emit_alert(ALERT_CRITICAL, "load_low", msg, &out_alerts[n++]);
    } else if (n < max_alerts && snap->system_load_percent < LOAD_WARN) {
        snprintf(msg, sizeof(msg),
                 "System load at %d%% — below warning threshold",
                 snap->system_load_percent);
        emit_alert(ALERT_WARNING, "load_low", msg, &out_alerts[n++]);
    }

    /* --- Coolant temperature --- */
    if (n < max_alerts && snap->coolant_temperature > TEMP_CRIT) {
        snprintf(msg, sizeof(msg),
                 "Coolant temperature at %d C — exceeds critical limit",
                 snap->coolant_temperature);
        emit_alert(ALERT_CRITICAL, "temp_high", msg, &out_alerts[n++]);
    } else if (n < max_alerts && snap->coolant_temperature > TEMP_WARN) {
        snprintf(msg, sizeof(msg),
                 "Coolant temperature at %d C — above normal range",
                 snap->coolant_temperature);
        emit_alert(ALERT_WARNING, "temp_high", msg, &out_alerts[n++]);
    }

    /* --- Pressure high --- */
    if (n < max_alerts && snap->hydraulic_pressure > PRESSURE_HIGH_CRIT) {
        snprintf(msg, sizeof(msg),
                 "Hydraulic pressure at %d bar — exceeds critical limit",
                 snap->hydraulic_pressure);
        emit_alert(ALERT_CRITICAL, "pressure_high", msg, &out_alerts[n++]);
    } else if (n < max_alerts && snap->hydraulic_pressure > PRESSURE_HIGH_WARN) {
        snprintf(msg, sizeof(msg),
                 "Hydraulic pressure at %d bar — approaching limit",
                 snap->hydraulic_pressure);
        emit_alert(ALERT_WARNING, "pressure_high", msg, &out_alerts[n++]);
    }

    /* --- Pressure low --- */
    if (n < max_alerts && snap->hydraulic_pressure < PRESSURE_LOW_CRIT) {
        snprintf(msg, sizeof(msg),
                 "Hydraulic pressure at %d bar — critically low",
                 snap->hydraulic_pressure);
        emit_alert(ALERT_CRITICAL, "pressure_low", msg, &out_alerts[n++]);
    } else if (n < max_alerts && snap->hydraulic_pressure < PRESSURE_LOW_WARN) {
        snprintf(msg, sizeof(msg),
                 "Hydraulic pressure at %d bar — below normal range",
                 snap->hydraulic_pressure);
        emit_alert(ALERT_WARNING, "pressure_low", msg, &out_alerts[n++]);
    }

    return n;
}

/* ── History ── */

int alert_history_copy(alert_record_t *out, int max_records) {
    pthread_mutex_lock(&g_history_lock);

    int available = g_history_count < ALERT_HISTORY_SIZE
                  ? g_history_count
                  : ALERT_HISTORY_SIZE;

    int to_copy = available < max_records ? available : max_records;

    /* Read from oldest to newest. */
    int start;
    if (g_history_count < ALERT_HISTORY_SIZE) {
        start = 0;
    } else {
        start = g_history_head;   /* oldest entry */
    }

    for (int i = 0; i < to_copy; i++) {
        int idx = (start + (available - to_copy) + i) % ALERT_HISTORY_SIZE;
        out[i] = g_history[idx];
    }

    pthread_mutex_unlock(&g_history_lock);
    return to_copy;
}

/* ── Formatting ── */

int alert_format(const alert_record_t *alert, char *buf, size_t buf_sz) {
    return snprintf(buf, buf_sz, "ALERT %s|%s|%s\n",
                    alert_severity_string(alert->severity),
                    alert->type,
                    alert->message);
}

/* ── Helpers ── */

const char *alert_severity_string(alert_severity_t s) {
    switch (s) {
        case ALERT_WARNING:  return "warning";
        case ALERT_CRITICAL: return "critical";
        default:             return "unknown";
    }
}