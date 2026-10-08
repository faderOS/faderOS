#include "board.h"
#include "panel.h"
#include "timing.h"
#include "debounce.h"
#include "link.h"
/* Production bring-up is the sequence the 0.8 ladder showed the desk survives:
   1 kHz tick, panel gate 0x08 (never bit 1), Sony heartbeats, then the link.
   The ladder image is the same sources with -DLADDER; leave it alone. */
static struct {
    debounce_state debounce;
    panel_keys raw,pressed,released;
    panel_analogs analog;
    uint32_t scan,lcd;
} app;
static void link_service(void)
{
    uint32_t now=clock_ms(); link_poll(now);
    if ((uint32_t)(now-app.scan)>=SCAN_MS) {
        app.scan=now; ky_scan(&app.raw);
        debounce_update(&app.debounce,&app.raw,now,&app.pressed,&app.released);
        analog_read(&app.analog); link_sample(&app.debounce.stable,&app.analog,now);
    }
    if ((uint32_t)(now-app.lcd)>=1) { app.lcd=now; lcd_service(); }
}
static void report_diag(void)
{
    uart_puts(" ENTRY ");
    uart_hex(platform_entry,8);
    uart_puts(" RSTVEC ");
    uart_hex(platform_resetvec,6);
    uart_puts(platform_eprom_ran ? " EPROM YES" : " EPROM NO");
    if (platform_last_fault==0xffffffffu) return;
    uart_puts(" LASTFAULT v=");
    uart_hex(platform_last_fault,2);
    uart_puts(" pc=");
    uart_hex(platform_last_fault_pc,6);
}
#ifndef LADDER
_Noreturn void firmware_main(void)
{
    uart_puts("faderOS PANEL BKDS-2010 FW " BKDS_LINK_VERSION " READY BOOT ");
    uart_hex(platform_boots,4);
    report_diag();
    uart_puts("\r\n");
    clock_init();
    panel_init(PANEL_GATE);
    clock_heartbeats();
    link_init();
    for (;;) link_service();
}
#else
#define LADDER_TOP 5u
#define SURVIVE_S 30u
static void bring_up(uint32_t step)
{
    if (step>=1) clock_init();
    /* Step 3 wrote 0x0A and is what reset the CPU. Keep it only here. */
    if (step>=2) panel_init(step==3 ? 0x0au : PANEL_GATE);
    if (step>=4) clock_heartbeats();
    if (step>=5) link_init();
}
static const char *const RUNGS[]={"bare","tick","gate08","gate0A","beats","link"};
static void wait_second(uint32_t step)
{
    if (step==0) { for (unsigned i=0;i<350;i++) delay_us(1000); return; }
    uint32_t t=clock_ms();
    while ((uint32_t)(clock_ms()-t)<1000u) {}
}
static void report(uint32_t step, uint32_t seconds)
{
    uart_puts("S"); uart_hex(step,1);
    uart_puts(" T"); uart_hex(seconds,4);
    uart_puts(" G"); uart_hex(gate_shadow,2);
    uart_puts(" X"); uart_hex(ext1_count,4);
    uart_puts("\r\n");
}
static void summary(void)
{
    uart_puts("LADDER DONE\r\n");
    for (uint32_t i=0;i<=LADDER_TOP;i++) {
        uart_puts("  S"); uart_hex(i,1);
        uart_puts(" "); uart_puts(RUNGS[i]);
        uart_puts((platform_survived>>i)&1u ? " OK\r\n" : " RESET\r\n");
    }
}
static uint32_t best_step(void)
{
    uint32_t best=0;
    for (uint32_t i=0;i<=LADDER_TOP;i++) if ((platform_survived>>i)&1u) best=i;
    return best;
}
_Noreturn static void advance(uint32_t step)
{
    platform_step_passed(step);
    uart_puts("S"); uart_hex(step,1); uart_puts(" PASS; NEXT STEP\r\n");
    __asm__ volatile("move.w #0x2700,%%sr" ::: "memory");
    ((void (*)(void))FLASH_BASE)();
    for (;;) {}
}
_Noreturn void firmware_main(void)
{
    uint32_t seconds=0,beat=0;
    uint32_t step=platform_step_claim(LADDER_TOP+1u);
    int parked=step>LADDER_TOP;
    uart_puts("\r\nfaderOS PANEL BKDS-2010 FW " BKDS_LINK_VERSION " STEP ");
    uart_hex(step,1);
    uart_puts(" BOOT ");
    uart_hex(platform_boots,4);
    uart_puts(" PASSED ");
    uart_hex(platform_survived,2);
    report_diag();
    uart_puts("\r\n");
    if (parked) {
        summary();
        step=best_step();
        uart_puts("PARK STEP "); uart_hex(step,1); uart_puts("\r\n");
    }
    bring_up(step);
    for (;;) {
        if (step>=5) {
            link_service();
            if ((uint32_t)(clock_ms()-beat)<1000u) continue;
            beat=clock_ms();
        } else {
            wait_second(step);
        }
        report(step,++seconds);
        if (!parked && seconds>=SURVIVE_S) advance(step);
    }
}
#endif
