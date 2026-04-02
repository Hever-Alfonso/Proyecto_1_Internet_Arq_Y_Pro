/*
 * alert_system.h
 * --------------
 * Anomaly detection and alert generation for the IoT monitoring server.
 *
 * This module is entirely NEW — the original project had no alert
 * system.  It inspects an equipment_snapshot_t and decides whether
 * any metric has crossed a warning or critical threshold.
 *
 * Severity levels:
 *   WARNING  — value approaching limit, operator should be aware
 *   CRITICAL — value exceeded safe range, immediate attention needed
 *
 * Alert types:
 *   rpm_high          RPM above safe operating range
 *   load_low          System load (energy) dangerously low
 *   temp_high         Coolant temperature above safe range
 *   pressure_high     Hydraulic pressure above safe range
 *   pressure_low      Hydraulic pressure below safe range
 *
 * Each generated alert is formatted as:
 *   ALERT <severity>|<type>|<message>\n
 *
 * The module also keeps a small in-memory ring buffer of the last N
 * alerts so that a newly connected operator can catch up.
 */

#ifndef ALERT_SYSTEM_H
#define ALERT_SYSTEM_H

#include "equipment_state.h"

#include <stddef.h>   /* size_t */

/* ── Severity ── */
typedef enum {
    ALERT_WARNING  = 0,
    ALERT_CRITICAL = 1
} alert_severity_t;

/* ── Single alert record ── */
typedef struct {
    alert_severity_t severity;
    char  type[32];          /* e.g. "temp_high"                  */
    char  message[128];      /* human-readable explanation        */
    char  timestamp[32];     /* YYYY-MM-DD HH:MM:SS              */
} alert_record_t;

/* ── Lifecycle ── */
void  alert_system_init(void);
void  alert_system_destroy(void);

/* ── Core ──
 *
 *  alert_evaluate()
 *    Receives a snapshot, checks every metric against thresholds,
 *    and stores any new alerts in the ring buffer.
 *
 *    out_alerts  — caller-provided array to receive new alerts
 *    max_alerts  — capacity of that array
 *
 *    Returns the number of alerts generated (0 if everything is OK).
 */
int   alert_evaluate(const equipment_snapshot_t *snap,
                     alert_record_t *out_alerts, int max_alerts);

/* ── History ──
 *
 *  alert_history_copy()
 *    Copies up to max_records entries from the ring buffer into out[].
 *    Returns the number actually copied (oldest first).
 */
int   alert_history_copy(alert_record_t *out, int max_records);

/* ── Formatting ──
 *
 *  alert_format()
 *    Writes the protocol line into buf:
 *      ALERT critical|temp_high|Coolant temperature at 92°C\n
 *    Returns the number of bytes written (excluding '\0').
 */
int   alert_format(const alert_record_t *alert, char *buf, size_t buf_sz);

/* ── Helpers ── */
const char *alert_severity_string(alert_severity_t s);

#endif /* ALERT_SYSTEM_H */