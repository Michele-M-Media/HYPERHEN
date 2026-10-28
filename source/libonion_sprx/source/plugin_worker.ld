ENTRY(hyperhen_plugin_worker)
SECTIONS {
  . = 0;
  .text : { *(.text.entry) *(.text .text.*) *(.rodata .rodata.*) }
  /DISCARD/ : { *(.eh_frame*) *(.comment*) *(.note*) }
}
