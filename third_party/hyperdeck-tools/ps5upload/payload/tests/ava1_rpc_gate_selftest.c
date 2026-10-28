/* Host test for the in-flight RPC gate (ava1/ava1_rpc_gate.h): the console web UI's calls are
 * counted with the AVA1 workers' so the exit waits for them, and refused once the exit began.
 * The race test hammers enter/close from many threads: no caller may be admitted after the
 * closer saw the count reach zero. */
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "ava1_rpc_gate.h"

static int failures = 0;
#define CHECK(expr)                                                 \
    do {                                                            \
        if (!(expr)) {                                              \
            fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); \
            failures++;                                             \
        }                                                           \
    } while (0)

static int g_count, g_closed, g_drained, g_late;

/* Enters until refused; each admitted call checks that the exit has not finished draining. */
static void *caller(void *arg) {
    (void)arg;
    for (;;) {
        if (ava1_gate_enter(&g_count, &g_closed) != 0) return NULL;
        if (__atomic_load_n(&g_drained, __ATOMIC_SEQ_CST)) __atomic_add_fetch(&g_late, 1, __ATOMIC_SEQ_CST);
        ava1_gate_leave(&g_count);
    }
}

static void basic(void) {
    int count = 0, closed = 0;
    CHECK(ava1_gate_enter(&count, &closed) == 0 && count == 1);
    CHECK(ava1_gate_enter(&count, &closed) == 0 && count == 2);
    ava1_gate_close(&closed);
    CHECK(ava1_gate_enter(&count, &closed) == -1 && count == 2); /* refused, count untouched */
    ava1_gate_leave(&count);
    ava1_gate_leave(&count);
    CHECK(count == 0);
}

/* Many rounds of: callers hammering enter/leave, the exit closing then waiting for the count to
 * drain (as ava1_payload_stop polls it). A caller admitted after the drain is a bug. */
static void close_races_enter(void) {
    int round;
    for (round = 0; round < 400; round++) {
        pthread_t t[6];
        int i;
        volatile int spin;
        g_count = g_closed = g_drained = 0;
        for (i = 0; i < 6; i++) pthread_create(&t[i], NULL, caller, NULL);
        for (spin = 0; spin < 2000 * (round % 7 + 1); spin++) {
        }
        ava1_gate_close(&g_closed);
        while (__atomic_load_n(&g_count, __ATOMIC_SEQ_CST) != 0) {
        }
        __atomic_store_n(&g_drained, 1, __ATOMIC_SEQ_CST);
        for (i = 0; i < 6; i++) pthread_join(t[i], NULL);
        CHECK(g_count == 0);
    }
    CHECK(g_late == 0);
}

int main(void) {
    basic();
    close_races_enter();
    if (failures) {
        fprintf(stderr, "ava1_rpc_gate_selftest: %d failure(s)\n", failures);
        return 1;
    }
    printf("ava1_rpc_gate_selftest: all passed\n");
    return 0;
}
