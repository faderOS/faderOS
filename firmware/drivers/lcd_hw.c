#include "panel.h"
#define R(a) (*(volatile uint8_t *)(a))
#define CTRL 0x300003u
#define DATA 0x300007u
/* Conservative busy delays for 16 MHz 68000 bring-up; not a system clock. */
void delay_us(unsigned us)
{
    while (us--) __asm__ volatile("nop\n\tnop\n\tnop\n\tnop" ::: "memory");
}
static void ctrl_rmw(uint8_t clear, uint8_t set)
{
    R(CTRL)=(R(CTRL)&(uint8_t)~clear)|set;
}
/* M66011-style expander on 0x300003: bit2=DATA, bit3=CLOCK, bit4=STROBE.
   Sony FUN_000480aa. LCD init sends 0x20 (default contrast/brightness). */
static void expander_send(uint8_t value)
{
    uint8_t pre=1;
    for (unsigned i=0;i<4;i++) {
        ctrl_rmw(8,0);
        if (pre&1) ctrl_rmw(0,4); else ctrl_rmw(4,0);
        pre>>=1;
        ctrl_rmw(0,8);
    }
    for (unsigned i=0;i<8;i++) {
        ctrl_rmw(8,0);
        if (value&0x80) ctrl_rmw(0,4); else ctrl_rmw(4,0);
        value<<=1;
        ctrl_rmw(0,8);
    }
    ctrl_rmw(8,0);
    ctrl_rmw(0,0x10);
    ctrl_rmw(0x10,0);
    ctrl_rmw(0,8);
}
void lcd_write_byte(uint8_t value, uint8_t rs)
{
    /* Preserve expander bits. Sony FUN_00047eb8: RS, E high, data, E low. */
    uint8_t v=(R(CTRL)&(uint8_t)~3u)|(rs&2u);
    R(CTRL)=v;
    R(CTRL)=v|1u;
    __asm__ volatile("nop\n\tnop\n\tnop\n\tnop" ::: "memory");
    R(DATA)=value;
    __asm__ volatile("nop\n\tnop\n\tnop\n\tnop" ::: "memory");
    R(CTRL)=v;
    delay_us(50);
}
void lcd_init(void)
{
    delay_us(20000);
    expander_send(0x20);
    lcd_write_byte(0x30,0); delay_us(5000);
    lcd_write_byte(0x30,0); delay_us(700);
    lcd_write_byte(0x30,0); delay_us(500);
    lcd_write_byte(0x38,0); delay_us(500);
    lcd_write_byte(0x08,0); delay_us(500);
    lcd_write_byte(0x01,0); delay_us(4000);
    lcd_write_byte(0x06,0); delay_us(100);
    lcd_write_byte(0x0c,0); delay_us(100);
}
