    .text
    .globl timer0_entry
    .extern system_ms
timer0_entry:
    addq.l #1,system_ms
    move.w #0xfeff,0xfffc98
    rte
/* Sony drives three square waves out of the status latch, one per periodic
   source: ext1 toggles bit 4 (FUN_000404fe), timer 2 bit 5 (FUN_000405ae) and
   timer 1 bit 6 (FUN_00040570). All three read-modify-write the same shadow,
   so the C side masks interrupts around its own updates. */
    .globl timer1_entry, timer2_entry, ext1_entry
    .extern gate_shadow
timer1_entry:
    move.l %d0,-(%sp)
    move.b gate_shadow,%d0
    eori.b #0x40,%d0
    move.b %d0,gate_shadow
    move.b %d0,0xd00001
    move.l (%sp)+,%d0
    move.w #0xfdff,0xfffc98
    rte
timer2_entry:
    move.l %d0,-(%sp)
    move.b gate_shadow,%d0
    eori.b #0x20,%d0
    move.b %d0,gate_shadow
    move.b %d0,0xd00001
    move.l (%sp)+,%d0
    move.w #0xfbff,0xfffc98
    rte
ext1_entry:
    move.l %d0,-(%sp)
    move.b gate_shadow,%d0
    eori.b #0x10,%d0
    move.b %d0,gate_shadow
    move.b %d0,0xd00001
    addq.l #1,ext1_count
    move.l (%sp)+,%d0
    move.w #0xfffd,0xfffc98
    rte
    .data
    .globl ext1_count
    .align 2
ext1_count:
    .long 0
    .section .note.GNU-stack,"",@progbits
