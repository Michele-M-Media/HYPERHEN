.intel_syntax noprefix
.text

.section .text.prologue
.global _start
_start:
    jmp		kernel_main

.text
.global cpuset_setaffinity
cpuset_setaffinity:
    mov		rax, 488
    mov		r10, rcx
    syscall
    ret
