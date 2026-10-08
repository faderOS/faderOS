#include "panel.h"
#define R(a) (*(volatile uint8_t *)(a))
static void put(unsigned address, uint8_t value)
{
    /* Bound the peripheral's busy wait; an absent board must not stall scanning. */
    unsigned timeout=10000;
    while ((R(0x8000c7)&8) && --timeout) {}
    if (!timeout) return;
    R(0x8000c3)=address; R(0x8000c1)=value;
}
void segments_init(void)
{
    /* Sony FUN_0006151e: configure the controller, blank digits 1-6, then enable. */
    put(0x0f,0); put(9,0x3f); put(0x0a,0x0b); put(0x0b,7);
    for (unsigned i=1;i<=6;i++) put(i,0x0f);
    put(0x0c,1);
}
void segments_number(uint32_t n)
{
    for (unsigned i=6;i;i--) { put(i,n%10); n/=10; }
    put(7,0); put(8,0);
}
void segments_test(unsigned digit, unsigned sides)
{
    for (unsigned i=1;i<=6;i++) put(i,digit);
    put(7,sides ? 0x77 : 0); put(8,sides ? 0x70 : 0);
}

void segments_raw(const uint8_t digits[6],uint8_t sides)
{
    for (unsigned i=0;i<6;i++) put(i+1,digits[i]);
    put(7,((sides&1) ? 0x70 : 0)|((sides&2) ? 7 : 0));
    put(8,(sides&4) ? 0x70 : 0);
}
