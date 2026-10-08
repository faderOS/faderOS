    .section .text.entry,"ax"
    .globl update_start
update_start:
    move.w #0x2700,%sr
    move.l #0x3fffc,%sp
    lea __bss_start,%a0
    lea __bss_end,%a1
1:  cmpa.l %a1,%a0
    bcc 2f
    clr.b (%a0)+
    bra 1b
2:  lea update_fault,%a0
    lea 8,%a1
    move.w #253,%d0
3:  move.l %a0,(%a1)+
    dbra %d0,3b
    jsr update_main
update_fault:
    move.w #0x2700,%sr
4:  bra 4b
    .section .note.GNU-stack,"",@progbits
