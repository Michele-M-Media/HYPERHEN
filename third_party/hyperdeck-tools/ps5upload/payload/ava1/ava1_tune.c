#include "ava1_tune.h"

#include <string.h>

void ava1_wtune_init(ava1_wtune_t *t, uint8_t start, uint8_t min, uint8_t max) {
    memset(t, 0, sizeof *t);
    t->workers = t->start = start;
    t->min = min;
    t->max = max;
}

uint8_t ava1_wtune_step(ava1_wtune_t *t, double files_per_s, int backlog) {
    if (t->hold) t->hold--;
    if (t->trying) {
        t->trying = 0;
        if (files_per_s < t->before * AVA1_WTUNE_GAIN) {
            t->workers--;
            t->hold = AVA1_WTUNE_HOLD_STEPS;
        }
        return t->workers;
    }
    if (!backlog) {
        if (++t->idle >= AVA1_WTUNE_IDLE_STEPS && t->workers > t->min) {
            t->workers--;
            t->idle = 0;
        }
        return t->workers;
    }
    t->idle = 0;
    if (!t->hold && t->workers < t->max) {
        t->before = files_per_s;
        t->workers++;
        t->trying = 1;
    }
    return t->workers;
}
