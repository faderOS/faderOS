    .section .rodata.updater,"a"
    .balign 4
    .globl updater_blob,updater_blob_end
updater_blob:
    .incbin "build/update.bin"
updater_blob_end:
    .section .note.GNU-stack,"",@progbits
