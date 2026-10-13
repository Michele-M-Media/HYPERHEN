/* Copyright (C) 2025 OnionHEN / LightningMods
 *
 * Frame-pointer walking primitives shared by the util and bootstrapper fault
 * handlers.
 *
 * These are header-inline on purpose. onion_current_frame() returns the frame
 * of *its caller*, and a fault handler relies on that to overwrite its own
 * return address with the cleanup routine. Moving these into a library .c
 * would insert an extra frame and silently shift which frame gets rewritten,
 * so each translation unit keeps its own copy instead.
 *
 * Requires the link to provide __text_start / __text_end.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct onion_frame onion_frame_t;

struct onion_frame {
  onion_frame_t *next;
  uintptr_t addr;
};

/** Raw %rbp of the caller. Naked: it must not touch the frame it reads. */
static onion_frame_t *__attribute__((naked, unused)) onion_frame_head(void) {
  __asm__ volatile("push %rbp\n"
                   "pop %rax\n"
                   "ret\n");
}

/** Frame of the function calling this — skips onion_frame_head's own frame. */
static inline onion_frame_t *onion_current_frame(void) {
  onion_frame_t *head = onion_frame_head();
  return head != NULL ? head->next : NULL;
}

static uintptr_t __attribute__((naked, noinline, unused)) onion_text_start(void) {
  __asm__ volatile("lea __text_start(%rip), %rax\n"
                   "ret\n");
}

static uintptr_t __attribute__((naked, noinline, unused)) onion_text_end(void) {
  __asm__ volatile("lea __text_end(%rip), %rax\n"
                   "ret\n");
}

/**
 * Walk the frame chain from the caller outwards, reporting each return
 * address through `log`. Addresses inside .text are reported relative to
 * __text_start so they can be matched against a disassembly.
 *
 * The addresses are accumulated into one line before being emitted, so the
 * whole backtrace reaches the sink — the util handler previously printed the
 * frames with raw printf() while the surrounding text went through
 * crash_log(), which left the crash log with the markers but no addresses.
 *
 * noinline so the walk always starts from this function's own frame; if the
 * compiler were free to inline it the backtrace would start one frame higher
 * depending on optimisation level.
 */
static void __attribute__((noinline, unused))
onion_print_backtrace(void (*log)(const char *fmt, ...)) {
  const uintptr_t start = onion_text_start();
  const uintptr_t stop = onion_text_end();
  char line[0x400];
  size_t used = 0;

  log(".text: 0x%08llx", (unsigned long long)start);
  log("---backtrace start---");

  line[0] = '\0';
  for (const onion_frame_t *__restrict frame = onion_current_frame();
       frame != NULL; frame = frame->next) {
    if (frame->addr == 0)
      continue;

    const int n =
        (frame->addr >= start && frame->addr <= stop)
            ? __builtin_snprintf(line + used, sizeof(line) - used, "0x%llx ",
                                 (unsigned long long)(frame->addr - start))
            : __builtin_snprintf(line + used, sizeof(line) - used, "0x%lx ",
                                 (unsigned long)frame->addr);
    if (n <= 0 || (size_t)n >= sizeof(line) - used)
      break; /* out of room — emit what we have */
    used += (size_t)n;
  }

  log("%s", line);
  log("---backtrace end---");
}

#ifdef __cplusplus
}
#endif
