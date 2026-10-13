#ifndef OFFSETS_13_60_H
#define OFFSETS_13_60_H

uint64_t g_sym_map_1360[] = {
  0x3B49294,  // KERNEL_SYM_DMPML4I
  0x3B49298,  // KERNEL_SYM_DMPDPI
  0x3B47E4C,  // KERNEL_SYM_PML4PML4I
  0x2a8d60,   // KERNEL_SYM_SYS_GETUID
  0x4ADD60,   // KERNEL_SYM_PRINTF
  0x3913B0,   // KERNEL_SYM_ACPI_SLEEP_MACHDEP_HOOK
  0xC16EE0,   // KERNEL_SYM_HV_MP_STARTUP_CALL
  0x269FB8,   // KERNEL_SYM_MP_STARTUP_INIT_VECTOR
  0x3B48FF8,  // KERNEL_SYM_PMAP_STORE
  0x2D39000,  // KERNEL_SYM_DATA_CAVE
  0x3BB4300,  // KERNEL_SYM_IDT_ARRAY
  0x3BB6FE0,  // KERNEL_SYM_COMMON_TSS
  0x83028,    // HV_SYM_HEAP_BASE
  0x82A60,    // HV_SYM_PCPUS
};

uint64_t g_gadget_map_1360[] = {
  0x25213E,   // KERNEL_GADGET_PIVOT_ADD_RSP28_RET  add rsp,0x28 ; ret
  0x257B3D,   // KERNEL_GADGET_IRETQ                iretq
  0x2912A1,   // KERNEL_GADGET_POP_RAX              pop rax ; ret
  0x2918B2,   // KERNEL_GADGET_POP_RCX              pop rcx ; ret
  0x28CE6D,   // KERNEL_GADGET_POP_RDI              pop rdi ; ret
  0x34CB91,   // KERNEL_GADGET_POP_RSI              pop rsi ; ret
  0x2A0F7B,   // KERNEL_GADGET_POP_RDX              pop rdx ; ret
  0xB8CA6D,   // KERNEL_GADGET_POP_R8               pop r8 ; ret
  0x291C42,   // KERNEL_GADGET_POP_RSP              pop rsp ; ret
  0x474389,   // KERNEL_GADGET_STORE_RCX_R8         mov [rcx], r8 ; ret
  0x32693F,   // KERNEL_GADGET_STORE_RDI8_RCX       mov [rdi+8], rcx ; ret
  0x321EA0,   // KERNEL_GADGET_OR_RCX_EAX           or [rcx], eax ; ret
  0xA07BA6,   // KERNEL_GADGET_SET_DR7              mov dr7,rax ; mov eax,r8d ; ret
  0x82B915,   // KERNEL_GADGET_ST_RCX_RDX           mov [rcx], rdx ; ret
  0x2BB28F,   // KERNEL_GADGET_ST_RCX_RAX           mov [rcx], rax ; xor eax,eax ; ret
  0x8E3A44,   // KERNEL_GADGET_MOV_RAX_RSI          mov rax, rsi ; ret
  0xBECDEA,   // KERNEL_GADGET_MOV_RAX_RDI          mov rax, rdi ; ret
  0x25D027,   // KERNEL_GADGET_REP_MOVSB            rep movsb ; nop ; rep ret
  0x30644F,   // KERNEL_GADGET_CLD                  cld ; ret
  0x2912A2,   // KERNEL_GADGET_RET_TRAMP            ret
  0x2529D8,   // KERNEL_GADGET_DBREG_RESUME         resumectx tail
};

#endif // OFFSETS_13_60_H