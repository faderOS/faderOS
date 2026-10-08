#include "timing.h"
#define W(a) (*(volatile uint16_t *)(a))
#define B(a) (*(volatile uint8_t *)(a))
extern void timer0_entry(void);
extern void timer1_entry(void);
extern void timer2_entry(void);
extern void ext1_entry(void);
volatile uint32_t system_ms;
void clock_init(void)
{
    __asm__ volatile("move.w #0x2700,%%sr" ::: "memory");
    /* Timer 0 is the 1 kHz tick. Timers 1 and 2 start in clock_heartbeats,
       which matches Sony FUN_000403e6 / FUN_0004046c; they are not a watchdog. */
    W(0xfffe00)=2; W(0xfffe20)=2; W(0xfffe40)=2;
    system_ms=0;
    B(0xfffc9b)=0x40;
    __asm__ volatile("move.l %0,0x110" :: "a"(timer0_entry) : "memory");
    B(0xfffc8f)=4; /* Timer 0 ICR: interrupt level 4. */
    W(0xfffc96)=0xfeff; W(0xfffc98)=0xfeff;
    W(0xfffe04)=16000; /* Internal 16 MHz / 16000 = 1 kHz. */
    /* Sony FUN_0008078c: parallel bits 15-11 are outputs it never drives.
       PCR 2 is not a GPIO mode, so writes to PDR would not reach the pins. */
    W(0xfffd00)=0xf800; B(0xfffd03)=2;
    /* Unmask timer 0. SCI2 stays masked until serial_link_init. */
    W(0xfffc94)=0x06ff;
    W(0xfffe00)=0x0095; /* timer 0: repeat, max1, IRQ, start; divisor 1 */
    __asm__ volatile("move.w #0x2000,%%sr" ::: "memory");
}
void clock_heartbeats(void)
{
    /* Sony FUN_000403e6, FUN_0004046c and FUN_00040354. The 0.8 ladder showed
       the desk lives without these waves (S2) and with them (S4/S5); this desk
       never sees ext1. Keep the Sony programming so the latch matches the
       original firmware, without ever setting D00001 bit 1. */
    __asm__ volatile("move.w #0x2700,%%sr" ::: "memory");
    __asm__ volatile("move.l %0,0x114" :: "a"(timer1_entry) : "memory");
    __asm__ volatile("move.l %0,0x118" :: "a"(timer2_entry) : "memory");
    __asm__ volatile("move.l %0,0x104" :: "a"(ext1_entry) : "memory");
    B(0xfffc91)=5; B(0xfffc93)=5; /* timers 1 and 2 at interrupt level 5 */
    B(0xfffc83)=0x35; /* ext1: level 5, edge triggered */
    W(0xfffe24)=62500; W(0xfffe28)=0; W(0xfffe20)=0x20d5;
    W(0xfffe44)=1250;  W(0xfffe48)=0; W(0xfffe40)=0x20d5;
    W(0xfffc96)=0xf8fd; W(0xfffc98)=0xf8fd;
    W(0xfffc94)=0x00fd; /* unmask ext1 and timers 0-2; SCI2 still masked */
    __asm__ volatile("move.w #0x2000,%%sr" ::: "memory");
}
uint32_t clock_ms(void)
{
    /* A single 68000 move.l is indivisible with respect to this ISR. */
    return system_ms;
}
