ENTRY(update_start)
MEMORY { RAM (rwx) : ORIGIN = 0x31000, LENGTH = 0xB000 }
SECTIONS {
 .text : { KEEP(*(.text.entry)) *(.text*) *(.rodata*) . = ALIGN(4); } > RAM
 .data : { *(.data*) . = ALIGN(4); } > RAM
 __image_end = .;
 .bss (NOLOAD) : { __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(4); __bss_end = .; } > RAM
 /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
 ASSERT(__bss_end <= 0x3c000, "updater overlaps 16 KiB stack")
 ASSERT(update_start == 0x31000, "RAM entry mismatch")
}
