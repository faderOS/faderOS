#include "engine.h"
#include "display.h"
#include "board.h"
#define B(a) (*(volatile uint8_t *)(a))
#define W(a) (*(volatile uint16_t *)(a))
static uint32_t ticks;
uint32_t update_ms(void)
{
    if (W(0xfffc96)&0x100u) { W(0xfffc96)=0xfeff; ++ticks; }
    return ticks;
}
void update_hw_init(void)
{
    W(0xfffc94)=0x7ff; /* all interrupts masked; every active instruction is RAM */
    W(0xfffe00)=2; W(0xfffe20)=2; W(0xfffe40)=2;
    W(0xfffc96)=0xfeff; W(0xfffc98)=0xfeff;
    W(0xfffe04)=16000; W(0xfffe00)=0x95;
    /* Keep the physically verified gate only. NEVER set D00001 bit 1. */
    B(0xd00001)=0x08;
    while (!(B(0xfffda7)&4u)) {}
    B(0xfffd8f)=0xa1; B(0xfffd8f)=0x81;
    B(0xfffd8d)=26; B(0xfffda5)=8;
    B(0xfffda1)=0xce; B(0xfffda3)=0x15; B(0xfffda3)=5;
}
int update_rx(void)
{
    (void)update_ms();
    uint8_t s=B(0xfffda7);
    if (s&0x78) { (void)B(0xfffda9); B(0xfffda3)=0x15; B(0xfffda3)=5; return -2; }
    return (s&2)?B(0xfffda9):-1;
}
void update_tx(uint8_t c)
{
    while (!(B(0xfffda7)&1u)) (void)update_ms();
    B(0xfffda9)=c;
}
void update_drain(void) { while (!(B(0xfffda7)&4u)) (void)update_ms(); }
uint8_t *update_buffer(void) { return (uint8_t *)0x10000; }
const uint8_t *update_flash(unsigned sector) { return (const uint8_t *)(FLASH_BASE+sector*SECTOR_SIZE); }
/* Exact word-wide command offsets from Sony EPROM tables 205110/205140/
   205160/205180 and the assembler at 2052B4. Two x8 devices in parallel. */
static void unlock(uint32_t base,uint16_t command)
{
    W(base+0xaaaa)=0xaaaa; W(base+0x5554)=0x5555; W(base+0xaaaa)=command;
}
int update_device_ok(void)
{
    unlock(FLASH_BASE,0x9090);
    uint16_t manufacturer=W(FLASH_BASE),device=W(FLASH_BASE+2);
    unlock(FLASH_BASE,0xf0f0);
    return manufacturer==0x0101 && device==0xa4a4;
}
static int wait_word(uint32_t address,uint16_t value,uint32_t timeout)
{
    uint32_t start=update_ms();
    while (W(address)!=value) {
        if ((uint32_t)(update_ms()-start)>=timeout) return -1;
    }
    return 0;
}
int update_flash_program(unsigned sector,const uint8_t *bytes)
{
    if (sector>6) return -1;
    uint32_t base=FLASH_BASE+sector*SECTOR_SIZE;
    update_display("ERASING",sector,101);
    unlock(base,0x8080);
    W(base+0xaaaa)=0xaaaa; W(base+0x5554)=0x5555; W(base)=0x3030;
    /* Sony 20536E polls FFFF for 31 seconds. Same criterion, own RAM clock. */
    if (wait_word(base,0xffff,31000)) { unlock(base,0xf0f0); return -1; }
    update_display("PROGRAMMING",sector,0);
    for (uint32_t i=0;i<SECTOR_SIZE;i+=2) {
        if (!(i&0x1fffu)) update_display("PROGRAMMING",sector,(i*100u)>>17);
        uint16_t value=((uint16_t)bytes[i]<<8)|bytes[i+1];
        if (W(base+i)!=0xffff) return -1;
        if (value==0xffff) continue;
        unlock(base,0xa0a0); W(base+i)=value;
        /* Sony 20547A compares the whole word with a 2-second timeout. */
        if (wait_word(base+i,value,2000)) { unlock(base,0xf0f0); return -1; }
    }
    return 0;
}
