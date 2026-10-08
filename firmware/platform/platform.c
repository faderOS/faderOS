#include "board.h"
extern char fault_table[], fault_table_end[];
/* Above __bss_end and far below the stack, so a reset that keeps the RAM
   powered leaves the record intact. A cold start finds no magic. */
#define BOOT_RECORD 0x30000u
#define BOOT_MAGIC 0x424b4232u
#define NO_FAULT 0xffffffffu
/* The EPROM clears 0x000000-0x00ffff before it does anything else (its entry
   at 0x400 jumps to 0x200408, programs the address decoder, then runs the
   clear loop at 0x440). Nothing of ours lives below 0x10000, so a mark left
   there survives exactly as long as the EPROM does not run. */
#define EPROM_MARK 0x8000u
#define MARK_MAGIC 0x4d41524bu
static volatile uint32_t *const record=(volatile uint32_t *)BOOT_RECORD;
uint32_t platform_boots, platform_last_fault, platform_last_fault_pc, platform_entry;
uint32_t platform_resetvec, platform_eprom_ran;
void platform_init(void)
{
    /* Vectors 0 and 1 are the reset stack pointer and entry point, and we
       never write them. On this board the EPROM only sits at zero until the
       decode is remapped, so after handover a reset takes these from RAM and
       lands wherever Sony left them: 200400 is the EPROM, 040000 our loader,
       060020 our application. */
    /* Read in assembly: the C front end refuses to believe an absolute load
       this close to zero is deliberate. */
    __asm__ volatile("move.l 4,%0" : "=d"(platform_resetvec) :: "memory");
    /* Peripheral state exactly as the restart left it, sampled before we own
       anything. A TMP68301 that really took its reset reads back 07FF/00/00;
       our own running values here mean the controller never reset and someone
       merely re-entered the loader. */
    uint32_t entry=((uint32_t)*(volatile uint16_t *)0xfffc94<<16)
                  |((uint32_t)(*(volatile uint16_t *)0xfffe00&0xffu)<<8)
                  |*(volatile uint8_t *)0xfffda1;
    uint32_t marked=*(volatile uint32_t *)EPROM_MARK==MARK_MAGIC;
    *(volatile uint32_t *)EPROM_MARK=MARK_MAGIC;
    if (record[0]==BOOT_MAGIC) record[1]++;
    else {
        record[0]=BOOT_MAGIC; record[1]=1; record[2]=NO_FAULT; record[3]=0;
        record[4]=0; record[5]=0; record[6]=0; record[7]=0;
        record[8]=0; record[9]=0;
    }
    /* The loader runs this first, so shifting each sample one slot down leaves
       the application printing what the loader saw, not what the loader set. */
    record[7]=record[6]; record[6]=entry;
    record[9]=record[8]; record[8]=marked;
    platform_entry=record[7];
    platform_eprom_ran=!record[9];
    platform_boots=record[1];
    /* Carry a fault across the restart that follows it, then take it off the
       record so the next boot does not repeat a stale report. */
    platform_last_fault=record[2]; platform_last_fault_pc=record[3];
    record[2]=NO_FAULT; record[3]=0;
    /* Own the vector table and stop inherited interrupts/timers. EPROM has
       already configured RAM and the bus; no Sony application helpers used. */
    volatile uintptr_t *vectors=(volatile uintptr_t *)0;
    uintptr_t stride=(uintptr_t)(fault_table_end-fault_table)/256u;
    for (unsigned i=2; i<256; ++i) vectors[i]=(uintptr_t)fault_table+i*stride;
    *(volatile uint16_t *)0xfffc94=0x7ff;
    *(volatile uint16_t *)0xfffe00=2;
    *(volatile uint16_t *)0xfffe20=2;
    *(volatile uint16_t *)0xfffe40=2;
    uart_init();
}
/* Ladder bookkeeping. The loader also runs platform_init, so only the
   application may advance the step; claiming it before the step runs means a
   restart lands on the next rung whether or not we get to say anything. */
uint32_t platform_survived;
uint32_t platform_step_claim(unsigned steps)
{
    uint32_t step=record[4];
    if (step>steps) step=steps;
    record[4]=step+1;
    platform_survived=record[5];
    return step;
}
void platform_step_passed(uint32_t step)
{
    record[5]|=1u<<step;
    platform_survived=record[5];
}
_Noreturn void fault_main(uint32_t vector, const volatile uint16_t *frame)
{
    /* Bus and address errors push the wider group-1 frame: function code,
       access address, instruction register, then the usual status and PC. */
    int wide=vector==2 || vector==3;
    uint32_t pc=((uint32_t)frame[wide ? 5 : 1]<<16)|frame[wide ? 6 : 2];
    record[2]=vector; record[3]=pc;
    uart_init();
    uart_puts("BKDS FAULT v=");
    uart_hex(vector,2);
    uart_puts(" pc=");
    uart_hex(pc,6);
    uart_puts(" sr=");
    uart_hex(frame[wide ? 4 : 0],4);
    if (wide) {
        uart_puts(" access=");
        uart_hex(((uint32_t)frame[1]<<16)|frame[2],6);
    }
    uart_puts(" boot=");
    uart_hex(platform_boots,4);
    uart_puts("\r\nBKDS FAULT; RESET REQUIRED\r\n");
    for (;;) {}
}
