#include "debounce.h"
#include <assert.h>
#include <stdio.h>
static debounce_state d;
static panel_keys raw, press, release;
static void sample(uint32_t t,uint8_t value)
{
    raw.ky308[0]=value;
    debounce_update(&d,&raw,t,&press,&release);
}
int main(void)
{
    sample(0,0); sample(5,1); sample(10,0); sample(15,1);
    sample(20,0); sample(25,1); sample(30,1); sample(35,1); sample(40,1);
    assert(d.stable.ky308[0]==0);
    sample(45,1); assert(press.ky308[0]==1 && !release.ky308[0]);
    sample(50,1); assert(!press.ky308[0]);
    sample(55,0); sample(60,1); sample(65,0); sample(70,0); sample(75,0); sample(80,0);
    assert(d.stable.ky308[0]==1);
    sample(85,0); assert(release.ky308[0]==1 && !press.ky308[0]);
    /* Each key has its own timer; a bouncing neighbor must not postpone it. */
    sample(90,3); sample(95,1); sample(100,3); sample(105,1); sample(110,3);
    assert(press.ky308[0]==1 && d.stable.ky308[0]==1);
    sample(115,3); sample(120,3); sample(125,3); sample(130,3);
    assert(press.ky308[0]==2);
    /* A stalled foreground loop does not manufacture stable observations. */
    sample(135,0); sample(1000,0); assert(!release.ky308[0]);
    sample(1005,0); sample(1010,0); sample(1015,0); sample(1020,0);
    assert(release.ky308[0]==3);
    d=(debounce_state){0};
    sample(0xfffffff0u,1); sample(0xfffffff5u,1); sample(0xfffffffau,1);
    sample(0xffffffffu,1); sample(4,1); assert(press.ky308[0]==1);
    /* A button held at boot is accepted only after a complete interval. */
    assert(d.stable.ky308[0]==1);
    puts("Debounce PASS: press/release bounce, independent keys, gaps, wrap, boot-held");
}
