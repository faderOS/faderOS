#include "board.h"
extern const uint8_t updater_blob[],updater_blob_end[];
_Noreturn void update_enter(void)
{
    __asm__ volatile("move.w #0x2700,%%sr" ::: "memory","cc");
    volatile uint8_t *dest=(volatile uint8_t *)0x31000;
    for (const uint8_t *p=updater_blob;p<updater_blob_end;++p) *dest++=*p;
    /* Assembly entry resets SP/BSS/vectors before receiving a sector at 10000.
       It never returns to flash or to the application's stack. */
    ((void (*)(void))0x31000)();
    for (;;) {}
}
