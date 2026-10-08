    .text
    .globl serial_entry
serial_entry:
    movem.l %d0-%d1/%a0-%a1,-(%sp)
    jsr serial_rx_interrupt
    movem.l (%sp)+,%d0-%d1/%a0-%a1
    rte
    .section .note.GNU-stack,"",@progbits
