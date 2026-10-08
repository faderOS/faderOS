#include "link.h"
#ifndef SERIAL_LINK_TEST
#define B(a) (*(volatile uint8_t *)(a))
#define W(a) (*(volatile uint16_t *)(a))
static uint16_t irq_lock(void) {
    uint16_t saved;
    __asm__ volatile("move.w %%sr,%0\n\tmove.w #0x2700,%%sr" : "=d"(saved) :: "memory", "cc");
    return saved;
}
static void irq_unlock(uint16_t saved) {__asm__ volatile("move.w %0,%%sr" :: "d"(saved) : "memory", "cc");}
#endif
static volatile uint16_t tx_head,tx_tail;
static volatile uint8_t tx_ring[64];
/* Manual 301A-44/46: TX-ready is edge serviced; prime SDR explicitly when
   restarting an empty queue, and mask TX interrupts when no bytes remain. */
static void tx_service(void) {
    if(tx_head!=tx_tail && (B(0xfffda7)&1u)) {
        B(0xfffda9)=tx_ring[tx_tail];
        tx_tail=(tx_tail+1)&63;
    }
    B(0xfffda1)=tx_head==tx_tail ? 0x0e : 0x0c;
}
static volatile uint16_t head,tail;
static volatile uint8_t ring[512];
volatile uint32_t serial_errors;
extern void serial_entry(void);
#ifndef SERIAL_LINK_TEST
void serial_link_init(void)
{
    __asm__ volatile("move.w #0x2700,%%sr" ::: "memory", "cc");
    tx_head=tx_tail=0;
    /* All four SCI2 vectors, as Sony FUN_0008078c does. The controller also
       supplies 0x52/0x53 when the condition is gone by acknowledge time. */
    __asm__ volatile("move.l %0,0x140\n\tmove.l %0,0x144\n\t"
                     "move.l %0,0x148\n\tmove.l %0,0x14c"
                     :: "a"(serial_entry) : "memory", "cc");
    B(0xfffc8b)=6; /* SCI2, slot 6. Sony uses level 6 on all three channels. */
    B(0xfffda1)=0x0e; /* 8N1, RX/error interrupts; TX enabled when queued. */
    B(0xfffd8f)=0x80;
    W(0xfffc94)=W(0xfffc94)&(uint16_t)~0x0040u; /* unmask SCI2; keep timers/ext1 */
    __asm__ volatile("move.w #0x2000,%%sr" ::: "memory", "cc");
}

#endif
void serial_rx_interrupt(void)
{
    uint8_t s=B(0xfffda7);
    if (s&0x78) {
        /* Parity, overrun, framing and break latch until SCMR's reset bit is
           toggled. Leaving them set makes every later byte report the same
           stale error, so one line glitch would silence the link for good. */
        (void)B(0xfffda9);
        B(0xfffda3)=0x15; B(0xfffda3)=5;
        serial_errors++;
    } else if (s&2) {
        uint8_t b=B(0xfffda9);
        uint16_t next=(head+1)&511;
        if (next!=tail) { ring[head]=b; head=next; } else serial_errors++;
    }
    tx_service();
    W(0xfffc98)=0xffbf;
}
int serial_read(void)
{
    if (tail==head) return -1;
    uint8_t b=ring[tail]; tail=(tail+1)&511; return b;
}
int serial_write(uint8_t b)
{
    uint16_t saved=irq_lock();
    uint16_t next=(tx_head+1)&63;
    if(next==tx_tail) {irq_unlock(saved);return 0;}
    tx_ring[tx_head]=b;tx_head=next;
    tx_service();
    irq_unlock(saved);return 1;
}
int serial_tx_empty(void) { return tx_head==tx_tail && (B(0xfffda7)&4u)!=0; }

#ifndef SERIAL_LINK_TEST
int serial_set_baud(uint32_t baud)
{
    uint8_t divider;
    if (baud==9600) divider=8;
    else if (baud==19200) divider=4;
    else if (baud==38400) divider=2;
    else if (baud==76800) divider=1;
    else return 0;
    /* f/8/SPR/SBRR, SPR=26: +0.16% nominal error at all four rates.
       Only SCI2's divisor changes; never reset the shared UART controller. */
    if (B(0xfffd8d)!=26) return 0;
    __asm__ volatile("move.w #0x2700,%%sr" ::: "memory", "cc");
    B(0xfffda1)=0x0e;tx_head=tx_tail=0;
    B(0xfffda3)=0; /* stop SCI2 RX/TX while updating its divider */
    B(0xfffda5)=divider;
    if (B(0xfffda7)&2) (void)B(0xfffda9);
    head=tail=0;
    B(0xfffda3)=0x15; B(0xfffda3)=5;
    __asm__ volatile("move.w #0x2000,%%sr" ::: "memory", "cc");
    return 1;
}

#endif
