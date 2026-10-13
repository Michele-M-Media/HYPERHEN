ENTRY(shellcode)
SECTIONS
{
  . = 0;
  .text : {
    entry.o(.text .text.*)     /* shellcode MUST be first */
    *(.text .text.*)
  }
  .rodata : { *(.rodata .rodata.*) }
  .data   : { *(.data .data.*)   }
  /DISCARD/ : {
    *(.eh_frame .eh_frame_hdr)
    *(.note .note.*)
    *(.comment)
    *(.bss .bss.*)
  }
}
