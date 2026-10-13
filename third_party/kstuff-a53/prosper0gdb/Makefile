PROSPER0GDB_OPT ?= -O0
PAYLOAD_OPT ?= -O0

all: payload.bin prosper0gdb.o

clean:
	rm -f payload.elf payload.bin r0run.o prosper0gdb.o

../lib/lib-elf-ps5.a:
	cd ../lib; $(MAKE)

r0run.o: r0run.asm
	yasm -f elf64 -g dwarf2 r0run.asm -o r0run.o

prosper0gdb.o: r0gdb.c r0run.o offsets.c offsets.h offsets/offset_list.txt offsets/*
	gcc $(PROSPER0GDB_OPT) -g -isystem ../freebsd-headers -nostdinc -nostdlib -fno-stack-protector -r -Wl,--unique='*' -ffunction-sections -fdata-sections $(EXTRA_CFLAGS) -DPS5KEK r0gdb.c r0run.o offsets.c -o prosper0gdb.o -fPIE -ffreestanding -fno-unwind-tables -fno-asynchronous-unwind-tables

payload.elf: ../lib/lib-elf-ps5.a main.c prosper0gdb.o dbg.c 
	gcc $(PAYLOAD_OPT) -g -isystem ../freebsd-headers -nostdinc -nostdlib -fno-stack-protector -static ../lib/lib-elf-ps5.a $(EXTRA_CFLAGS) main.c prosper0gdb.o -DPS5KEK dbg.c -o payload.elf -fPIE -ffreestanding -no-pie -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384

payload.bin: payload.elf
	objcopy payload.elf --only-section .text --only-section .data --only-section .bss --only-section .rodata -O binary payload.bin
	python3 ../lib/frankenelf.py payload.bin
