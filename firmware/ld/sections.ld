ENTRY(_start)
SECTIONS {
 .text : { KEEP(*(.text.entry)) *(.text*) *(.rodata*) . = ALIGN(4); } > FLASH
 .data : { __data_start = .; *(.data*) . = ALIGN(4); __data_end = .; } > RAM AT> FLASH
 __data_load = LOADADDR(.data);
 .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > RAM
 __stack_top = 0x3fffc;
 /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
 ASSERT(__bss_end <= 0x30000, "RAM overlaps reserved stack space")
 ASSERT(_start == ORIGIN(FLASH), "entry must be first")
}
