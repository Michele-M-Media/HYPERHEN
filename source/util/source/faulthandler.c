/* Copyright (C) 2025 OnionHEN / LightningMods

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>.  */

#include "faulthandler.h"
#include "common_utils.h"

#include <onion/fault_frame.h>
#include <onion/log.h>

#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void (*g_cleanup_handler)(void) = NULL;


// NOLINTBEGIN(bugprone-signal-handler)

extern void shutdown_ipc(void);
extern void kill_loading_app(void);

static void __attribute__((used)) cleanup_and_throw(void) {
	//onion_notify(true, "Fatal error occured. Cleaning up, catching and exiting...");
	if (g_cleanup_handler != NULL) {
		g_cleanup_handler();
		g_cleanup_handler = NULL;
	}
	longjmp(g_catch_buf, 1);
	onion_notify(true, "notify.util.crashed");
	// TODO longjump here
}

static uintptr_t __attribute__((naked, noinline)) get_cleanup_function(void) {
	__asm__ volatile(
		"lea cleanup_and_throw(%rip), %rax\n"
		"ret\n"
	);
}
bool is_handler_enabled = true;
static void fault_handler(int sig) {
	if(!is_handler_enabled) {
		onion_log_emergency("signal handler disabled, ignoring signal %d", sig);
		return;
	}
	onion_log_emergency("signal %d received", sig);
	onion_print_backtrace(onion_log_emergency);
	/* Must read this frame directly — see onion/fault_frame.h. */
	onion_frame_t *frame = onion_current_frame();
	frame->addr = get_cleanup_function();
}

// NOLINTEND(bugprone-signal-handler)

void fault_handler_init(void (*cleanup_handler)(void)) {
	g_cleanup_handler = cleanup_handler;
	signal(SIGSEGV, fault_handler);
	signal(SIGILL, fault_handler);
	signal(10, fault_handler);
	signal(9, fault_handler);

}
