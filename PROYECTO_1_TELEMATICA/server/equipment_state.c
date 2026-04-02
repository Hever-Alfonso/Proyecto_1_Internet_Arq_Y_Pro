/*
 * equipment_state.c
 * -----------------
 * Implementation of the IoT equipment state module.
 *
 * A single static structure holds every variable that describes the
 * equipment.  A single mutex serialises all access.  No other module
 * needs to know the lock exists — they call the helpers declared in
 * equipment_state.h and get a consistent snapshot or a safe mutation.
 *
 * The simulate_tick() function is meant to be called once per broadcast
 * cycle (e.g. every 5 s) by the metrics thread.  It models:
 *   - load drain proportional to rpm
 *   - coolant temperature rise under high rpm, slow cool-down otherwise
 *   - hydraulic pressure that fluctuates around a base value
 */

#include "equipment_state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── Internal state ── */

static equipment_snapshot_t g_state = {
    .equipment_rpm        = 0,
    .system_load_percent  = 100,
    .coolant_temperature  = 28,
    .hydraulic_pressure   = 50,
    .equipment_heading    = HEADING_NORTH
};

static pthread_mutex_t g_state_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_rng_seeded = 0;

/* ── Small helpers (internal) ── */

static int clamp(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

/* Bounded random variation: returns a value in [-range, +range]. */
static int jitter(int range) {
    if (!g_rng_seeded) {
        srand((unsigned)time(NULL));
        g_rng_seeded = 1;
    }
    return (rand() % (2 * range + 1)) - range;
}

/* ── Lifecycle ── */

void equipment_state_init(void) {
    pthread_mutex_lock(&g_state_lock);
    g_state.equipment_rpm       = 0;
    g_state.system_load_percent = 100;
    g_state.coolant_temperature = 28;
    g_state.hydraulic_pressure  = 50;
    g_state.equipment_heading   = HEADING_NORTH;
    pthread_mutex_unlock(&g_state_lock);
}

void equipment_state_destroy(void) {
    pthread_mutex_destroy(&g_state_lock);
}

/* ── Atomic read ── */

void equipment_state_read(equipment_snapshot_t *out) {
    pthread_mutex_lock(&g_state_lock);
    *out = g_state;                       /* struct copy under lock */
    pthread_mutex_unlock(&g_state_lock);
}

/* ── Mutators ── */

int equipment_modify_rpm(int delta, char *reason, size_t reason_sz) {
    int ok = 1;

    pthread_mutex_lock(&g_state_lock);

    if (g_state.system_load_percent < 10) {
        snprintf(reason, reason_sz, "system_load_too_low");
        ok = 0;
    } else {
        int new_rpm = g_state.equipment_rpm + delta;

        if (new_rpm < 0) {
            new_rpm = 0;
            snprintf(reason, reason_sz, "rpm_at_minimum");
            ok = 0;
        } else if (new_rpm > 5000) {
            new_rpm = 5000;
            snprintf(reason, reason_sz, "rpm_at_maximum");
            ok = 0;
        } else {
            snprintf(reason, reason_sz, "rpm=%d", new_rpm);
        }

        g_state.equipment_rpm = new_rpm;
    }

    pthread_mutex_unlock(&g_state_lock);
    return ok;
}

void equipment_adjust_heading(int direction) {
    pthread_mutex_lock(&g_state_lock);

    int h = (int)g_state.equipment_heading + direction;
    if (h < 0) h = 3;
    if (h > 3) h = 0;
    g_state.equipment_heading = (heading_t)h;

    pthread_mutex_unlock(&g_state_lock);
}

/* ── Simulation tick ── */

void equipment_simulate_tick(void) {
    pthread_mutex_lock(&g_state_lock);

    /* --- Load drain --- */
    if (g_state.equipment_rpm > 0 && g_state.system_load_percent > 0) {
        int drain = 1;
        if (g_state.equipment_rpm >= 3000) drain = 3;
        else if (g_state.equipment_rpm >= 1500) drain = 2;
        g_state.system_load_percent -= drain;
        if (g_state.system_load_percent < 0)
            g_state.system_load_percent = 0;
    }

    /* --- Coolant temperature --- */
    if (g_state.equipment_rpm > 3500 && g_state.coolant_temperature < 100) {
        g_state.coolant_temperature += 2;
    } else if (g_state.equipment_rpm > 1500 && g_state.coolant_temperature < 85) {
        g_state.coolant_temperature += 1;
    } else if (g_state.coolant_temperature > 28) {
        g_state.coolant_temperature -= 1;
    }
    g_state.coolant_temperature = clamp(g_state.coolant_temperature, 0, 100);

    /* --- Hydraulic pressure (fluctuates around a base proportional to rpm) --- */
    {
        int base_pressure = 30 + (g_state.equipment_rpm * 50) / 5000;
        int noise = jitter(3);
        g_state.hydraulic_pressure = clamp(base_pressure + noise, 0, 100);
    }

    pthread_mutex_unlock(&g_state_lock);
}

/* ── Helpers ── */

const char *heading_to_string(heading_t h) {
    switch (h) {
        case HEADING_NORTH: return "N";
        case HEADING_EAST:  return "E";
        case HEADING_SOUTH: return "S";
        case HEADING_WEST:  return "W";
        default:            return "?";
    }
}