    .section .text.entry,"ax"
    .globl _start
_start:
    move.w #0x2700,%sr
    move.l #__stack_top,%sp
    lea __data_load,%a0
    lea __data_start,%a1
    lea __data_end,%a2
1:  cmpa.l %a2,%a1
    bcc 2f
    move.b (%a0)+,(%a1)+
    bra 1b
2:  lea __bss_start,%a1
    lea __bss_end,%a2
3:  cmpa.l %a2,%a1
    bcc 4f
    clr.b (%a1)+
    bra 3b
4:  jsr platform_init
    jsr firmware_main
5:  bra 5b
/* One stub per vector: the 68000 exception frame carries no vector number,
   so the entry point is the only place the identity still exists. */
    .globl fault_table, fault_table_end
fault_table:
    .set vector, 0
    .rept 256
    move.w #vector,%d0
    bra.w fault_common
    .set vector, vector+1
    .endr
fault_table_end:
fault_common:
    move.w #0x2700,%sr
    movea.l %sp,%a0
    move.l #__stack_top,%sp
    move.l %a0,-(%sp)
    moveq #0,%d1
    move.w %d0,%d1
    move.l %d1,-(%sp)
    jsr fault_main
6:  bra 6b
    .section .note.GNU-stack,"",@progbits
