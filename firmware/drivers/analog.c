#include "panel.h"
#ifndef R
#define R(a) (*(volatile uint8_t *)(a))
#endif
static uint8_t previous[6];
static unsigned joy_phase;
static uint8_t joy_x=128,joy_y=128;
void analog_init(void)
{
    /* D4701 / companion: Sony FUN_000485d4 before any LCD or lamp enable. */
    R(0x30000d)=3; R(0x30000f)=0; R(0x30001d)=0xc3; R(0x30001f)=0;
    /* Sony FUN_0007fea6: ROM-PACK / analog companion next to the D4701. */
    R(0x30005d)=0xff; R(0x30005f)=0x0f; R(0x300051)=0; R(0x300053)=0; R(0x300055)=0xe0;
    R(0x8000f0)=0;
    /* Sony 6397E/63A00 initializes the ADC at its data address. */
    R(0x300033)=3;
    joy_phase=0;joy_x=joy_y=128;
    /* Preserve free-running hardware counters; baseline avoids a startup jump. */
    for (unsigned i=0;i<6;i++) previous[i]=R(0x300061+2*i);
}
void analog_read(panel_analogs *v)
{
    uint8_t hi,lo,next;
    /* Avoid a torn read when the counter crosses a byte boundary. */
    unsigned tries=4;
    do { hi=R(0x8000e3)&15; lo=R(0x8000e1); next=R(0x8000e3)&15; }
    while (hi!=next && --tries);
    v->tbar=((uint16_t)hi<<8)|lo;
    /* Sony 499FC: select 0, read on next call, select 1, read on next
       call. Never read immediately after selection. At 5ms scan each axis
       is refreshed at 50Hz, independently of T-bar and encoder sampling. */
    switch(joy_phase) {
    case 0: R(0x300031)=0;break;
    case 1: joy_y=R(0x300033);break;
    case 2: R(0x300031)=1;break;
    case 3: joy_x=R(0x300033);break;
    }
    joy_phase=(joy_phase+1)&3;
    v->x=joy_x;v->y=joy_y;
    for (unsigned i=0;i<6;i++) {
        uint8_t now=R(0x300061+2*i), d=now-previous[i];
        v->delta[i]=d<128 ? d : (int)d-256;
        previous[i]=now;
    }
}
