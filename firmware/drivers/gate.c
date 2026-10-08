#include "panel.h"
/* 0xD00001 is write-only, so the shadow is the only readable copy. Sony keeps
   the same single byte at DAT_000237ac: FUN_0004001a sets the gate, the buzzer
   path ORs bit 2, and three periodic ISRs XOR bits 4, 5 and 6 into it. Every
   writer must go through here or one of them will erase another's bits. */
volatile uint8_t gate_shadow;
static inline uint16_t hold(void)
{
    uint16_t sr;
    __asm__ volatile("move.w %%sr,%0\n\tmove.w #0x2700,%%sr" : "=d"(sr) :: "memory");
    return sr;
}
static inline void release(uint16_t sr)
{
    __asm__ volatile("move.w %0,%%sr" :: "d"(sr) : "memory");
}
void gate_write(uint8_t value)
{
    uint16_t sr=hold();
    gate_shadow=value;
    *(volatile uint8_t *)0xd00001=value;
    release(sr);
}
void gate_set(uint8_t mask)
{
    uint16_t sr=hold();
    gate_shadow|=mask;
    *(volatile uint8_t *)0xd00001=gate_shadow;
    release(sr);
}
void gate_clear(uint8_t mask)
{
    uint16_t sr=hold();
    gate_shadow&=(uint8_t)~mask;
    *(volatile uint8_t *)0xd00001=gate_shadow;
    release(sr);
}
