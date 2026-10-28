/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stddef.h>
__asm__(".section .rodata\n"
        ".global hyperhen_plugin_worker_start\n"
        "hyperhen_plugin_worker_start:\n"
        ".incbin \"" HYPERHEN_PLUGIN_WORKER_BIN "\"\n"
        ".global hyperhen_plugin_worker_end\n"
        "hyperhen_plugin_worker_end:\n"
        ".previous\n");
