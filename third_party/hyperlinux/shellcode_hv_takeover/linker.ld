/* linker.ld */
ENTRY(takeover)

SECTIONS
{
    . = 0x1000;
    .shell_code :
    {
        *(.entry_point)
        *(.text)
        *(.text.*)
        *(.rodata*)
        *(.data*)
    }
    /DISCARD/ : {
        *(.eh_frame .eh_frame_hdr)
        *(.note .note.*)
        *(.comment)
    }
}
