/*
 * equipment_state.h
 * -----------------
 * Thread-safe management of IoT equipment state.
 *
 * Replaces the old loose globals (g_speed, g_battery, g_temp, g_dir)
 * with a single structure protected by a mutex, exposing clean
 * read/write helpers so no other module touches the lock directly.
 */

#ifndef EQUIPMENT_STATE_H
#define EQUIPMENT_STATE_H

#include <pthread.h>

/* ── Heading cardinal directions ── */
typedef enum {
    HEADING_NORTH = 0,
    HEADING_EAST  = 1,
    HEADING_SOUTH = 2,
    HEADING_WEST  = 3
} heading_t;

/* ── Snapshot of the equipment at a point in time ── */
typedef struct {
    int  equipment_rpm;            /* 0 .. 5000   */
    int  system_load_percent;      /* 0 .. 100  % */
    int  coolant_temperature;      /* 0 .. 100  °C */
    int  hydraulic_pressure;       /* 0 .. 100  bar (simulated) */
    heading_t equipment_heading;   /* N / E / S / W */
} equipment_snapshot_t;

/* ── Lifecycle ── */
void  equipment_state_init(void);
void  equipment_state_destroy(void);

/* ── Atomic read: fills *out with a consistent copy ── */
void  equipment_state_read(equipment_snapshot_t *out);

/* ── Mutators (each one locks, modifies, unlocks) ── */

/*  Returns 1 on success, 0 on clamped/refused.
 *  *reason is filled with a short explanation (up to reason_sz bytes). */
int   equipment_modify_rpm(int delta, char *reason, size_t reason_sz);

/*  Rotates heading: +1 = clockwise, -1 = counter-clockwise. */
void  equipment_adjust_heading(int direction);

/*  Called every tick by the metrics thread to simulate wear:
 *    - load drains when rpm > 0
 *    - temperature rises with high rpm, cools otherwise
 *    - pressure fluctuates around a base value              */
void  equipment_simulate_tick(void);

/* ── Helpers ── */
const char *heading_to_string(heading_t h);

#endif /* EQUIPMENT_STATE_H */