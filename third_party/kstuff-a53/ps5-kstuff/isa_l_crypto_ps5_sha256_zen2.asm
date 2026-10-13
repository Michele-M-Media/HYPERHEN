default rel

section .text

%ifidn __OUTPUT_FORMAT__, elf64
%define arg0 rdi
%define arg1 rsi
%else
%define arg0 rcx
%define arg1 rdx
%endif

extern sha256_ni_x1

global _sha256_ni_x1_zen2
_sha256_ni_x1_zen2:
        mov     r10d, 0x4000
        jmp     sha256_ni_x1
