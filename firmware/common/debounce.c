#include "debounce.h"
void debounce_update(debounce_state *d,const panel_keys *raw,uint32_t now,
                     panel_keys *pressed,panel_keys *released)
{
    const uint8_t *r=(const uint8_t *)raw;
    uint8_t *s=(uint8_t *)&d->stable, *c=(uint8_t *)&d->candidate;
    uint8_t *p=(uint8_t *)pressed, *u=(uint8_t *)released;
    unsigned gap=!d->initialized || (uint32_t)(now-d->last)>2*SCAN_MS;
    for (unsigned i=0;i<sizeof *raw;i++) {
        p[i]=u[i]=0;
        for (unsigned b=0;b<8;b++) {
            unsigned bit=1u<<b, n=8*i+b;
            if (gap || ((r[i]^c[i])&bit)) {
                c[i]=(c[i]&~bit)|(r[i]&bit); d->since[n]=now;
            }
            if (((s[i]^c[i])&bit) && (uint32_t)(now-d->since[n])>=DEBOUNCE_MS) {
                s[i]^=bit;
                if (s[i]&bit) p[i]|=bit; else u[i]|=bit;
            }
        }
    }
    d->last=now; d->initialized=1;
}
