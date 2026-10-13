#!/bin/sh
set -e
cd "$(dirname "$0")"

CC=${CC:-gcc}
CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -fno-plt \
  -fno-stack-protector -fno-stack-clash-protection \
  -fomit-frame-pointer -fno-asynchronous-unwind-tables \
  -fcf-protection=none -fno-jump-tables -mgeneral-regs-only \
  -mno-red-zone -Wall -Wextra"

$CC $CFLAGS -c break_hv.c -o break_hv.o
$CC -c entry.S -o entry.o

ld -nostdlib -no-pie -T link.ld entry.o break_hv.o -o break_hv.elf

objcopy -O binary break_hv.elf break_hv.bin
xxd -i -n break_hv break_hv.bin > break_hv.h
