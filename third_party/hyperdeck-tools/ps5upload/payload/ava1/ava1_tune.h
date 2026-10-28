/* Receiver worker tuning (SPEC.md §16). Pure. */
#ifndef AVA1_TUNE_H
#define AVA1_TUNE_H

#include <stdint.h>

#define AVA1_WTUNE_GAIN 1.10   /* §16: an addition must raise files/s by >= 10 % */
#define AVA1_WTUNE_IDLE_STEPS 3 /* §16: release one worker after 3 idle steps */
#define AVA1_WTUNE_HOLD_STEPS 15 /* §16: hold 30 s at one 2 s step */

typedef struct {
    uint8_t workers, start, min, max;
    double before; /* files/s before the last addition */
    int trying;    /* 1 while judging the last addition */
    uint32_t hold; /* steps to wait before trying again */
    uint32_t idle; /* consecutive steps without backlog */
} ava1_wtune_t;

void ava1_wtune_init(ava1_wtune_t *t, uint8_t start, uint8_t min, uint8_t max);
/* Every 2 s: files applied per second, and whether work stayed queued. Returns the
 * worker count to run. */
uint8_t ava1_wtune_step(ava1_wtune_t *t, double files_per_s, int backlog);

#endif
