MEMORY {
 FLASH (rx) : ORIGIN = 0x60020, LENGTH = 0xbffe0
 RAM (rw) : ORIGIN = 0x18000, LENGTH = 0x18000
}
INCLUDE ld/sections.ld
