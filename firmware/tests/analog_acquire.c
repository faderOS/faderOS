#include <assert.h>
#include <stdio.h>
#include <stdint.h>
static uint8_t regs[0x1000000];
#define R(a) regs[a]
#include "../drivers/analog.c"
int main(void) {
    panel_analogs a={0};analog_init();assert(R(0x300033)==3);
    R(0x8000e3)=4;R(0x8000e1)=17;
    R(0x300033)=249;analog_read(&a);assert(R(0x300031)==0&&a.x==128&&a.y==128);
    R(0x300033)=61;analog_read(&a);assert(a.y==61&&a.x==128);
    R(0x300033)=249;analog_read(&a);assert(R(0x300031)==1&&a.y==61&&a.x==128);
    R(0x300033)=173;analog_read(&a);assert(a.x==173&&a.y==61&&a.tbar==1041);
    analog_read(&a);assert(a.x==173&&a.y==61);
    R(0x300033)=62;analog_read(&a);assert(a.y==62&&a.x==173);
    puts("ADC scheduling PASS: deferred reads, independent channels, retained values, T-bar unchanged");
}
