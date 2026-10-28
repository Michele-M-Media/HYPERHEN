/* The in-flight RPC count the exit waits on, and the gate in-process callers (the console web
 * UI) pass through to join it. Header-only so the host test exercises the exact code.
 *
 * Ordering: enter increments FIRST, then reads `closed`; close sets `closed`, then the exit polls
 * the count. With sequentially consistent atomics one of the two always sees the other: either
 * the caller sees `closed` and backs out, or the exit sees the caller's increment and waits. */
#ifndef AVA1_RPC_GATE_H
#define AVA1_RPC_GATE_H

/* 0: admitted (call ava1_gate_leave when done); -1: closed (the count is left as it was). */
static inline int ava1_gate_enter(int *count, const int *closed) {
    __atomic_add_fetch(count, 1, __ATOMIC_SEQ_CST);
    if (__atomic_load_n(closed, __ATOMIC_SEQ_CST)) {
        __atomic_sub_fetch(count, 1, __ATOMIC_SEQ_CST);
        return -1;
    }
    return 0;
}

static inline void ava1_gate_leave(int *count) { __atomic_sub_fetch(count, 1, __ATOMIC_SEQ_CST); }

static inline void ava1_gate_close(int *closed) { __atomic_store_n(closed, 1, __ATOMIC_SEQ_CST); }

#endif
