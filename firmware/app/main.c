#include "board.h"
#include "panel.h"
#include "timing.h"
#include "debounce.h"
static void hex(char *p,unsigned v,unsigned digits)
{
    while (digits) { p[--digits]="0123456789ABCDEF"[v&15]; v>>=4; }
}
_Noreturn void firmware_main(void)
{
    panel_keys keys, raw, pressed, released;
    static debounce_state debounce;
    panel_analogs analog;

    unsigned mode=0, phase=0, last=0, held=0;
    uint32_t scan_at=0, display_at=0, lamp_at=0, lcd_at=0;
    uint32_t rotary[6]={0};
    panel_init(PANEL_GATE); ky_lamp_test(0);
    uart_puts("BKDS APP 0.1 READY\r\nDEMO: F1 interactive; F2 lamps; ? alive\r\n");
    clock_init();
    clock_heartbeats();
    for (;;) {
        uint32_t now=clock_ms();
        if ((uint32_t)(now-lcd_at)>=1) { lcd_at=now; lcd_service(); }
        if ((uint32_t)(now-scan_at)<SCAN_MS) continue;
        scan_at=now;
        ky_scan(&raw);
        debounce_update(&debounce,&raw,now,&pressed,&released);
        keys=debounce.stable;
        analog_read(&analog);
        if (pressed.ky308[0]&1) mode=0;
        if (pressed.ky308[0]&2) { mode=1; lamp_at=now; }
        held=0;
        for (unsigned i=0;i<sizeof keys;i++) {
            uint8_t value=((uint8_t *)&keys)[i];
            for (unsigned b=0;b<8;b++) if (value&(1u<<b)) {
                held++;
                if (((uint8_t *)&pressed)[i]&(1u<<b)) last=i*8+b;
            }

        }
        for (unsigned i=0;i<6;i++) rotary[i]+=analog.delta[i];
        if (mode) {
            phase=((uint32_t)(now-lamp_at)/500)%3;
            ky_lamp_test(phase);
            segments_test(phase ? 0x88 : 0x0f,phase);
        } else {
            ky_feedback(&keys,1);
            ky_indicators(0);
            segments_number(analog.tbar);
        }
        if ((uint32_t)(now-display_at)>=100) {
            display_at=now;
            char line[41]="T:000 X:00 Y:00 KEY:000 HELD:00";
            hex(line+2,analog.tbar,3); hex(line+8,analog.x,2); hex(line+13,analog.y,2);
            hex(line+20,last,3); hex(line+29,held,2);
            /* Header labels and fields are fixed-width; LCD pads trailing spaces. */
            lcd_line(0,mode ? "BKDS DEMO LAMPS - F1:INPUT F2:LAMPS" : line);
            char enc[41]="R:0000 0000 0000 0000 0000 0000";
            for (unsigned i=0;i<6;i++) hex(enc+2+5*i,(uint32_t)rotary[i],4);
            lcd_line(1,enc);
        }
        if (uart_getc()=='?') uart_puts("BKDS APP 0.1 ALIVE\r\n");

    }
}
