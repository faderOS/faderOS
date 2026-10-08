#include "board.h"
/* Explicit probes cover both initialized data and zeroed BSS at startup. */
static volatile uint32_t initialized=0x424f4f54u;
static volatile uint32_t zeroed;
_Noreturn void firmware_main(void)
{
    if (initialized!=0x424f4f54u || zeroed!=0) for (;;) {}
    uart_puts("faderOS RESIDENT BKDS-2010 0.2\r\n");
    for (;;) {
        if (image_valid((const uint8_t *)APP_BASE,APP_CAPACITY)) {
            uart_puts("APP VERIFIED\r\n");
            ((void (*)(void))APP_ENTRY)();
            /* Application startup is not allowed to return. */
            for (;;) {}
        }
        uart_puts("RECOVERY: INVALID APP; ENTERING RAM UPDATER\r\n");
        update_enter();

    }
}
