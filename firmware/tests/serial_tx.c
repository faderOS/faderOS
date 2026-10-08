#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define SERIAL_LINK_TEST
static volatile uint8_t regs[512];
static volatile uint16_t words[256];
#define B(a) regs[((a)-0xfffc00)&511]
#define W(a) words[(((a)-0xfffc00)&511)/2]
static uint16_t irq_lock(void) {return 0;}
static void irq_unlock(uint16_t saved) {(void)saved;}
#include "../drivers/serial_link.c"
int main(void) {
    /* Busy hardware: queue must be bounded and preserve FIFO on wrap. */
    for(unsigned i=0;i<63;i++)assert(serial_write(i));
    assert(!serial_write(99)&&B(0xfffda1)==0x0c&&!serial_tx_empty());
    for(unsigned i=0;i<63;i++) {
        B(0xfffda7)=1;serial_rx_interrupt();assert(B(0xfffda9)==i);
    }
    assert(tx_head==tx_tail&&B(0xfffda1)==0x0e&&!serial_tx_empty());
    B(0xfffda7)=5;assert(serial_tx_empty());
    /* No interrupt edge is needed to prime a previously idle transmitter. */
    assert(serial_write(0xaa)&&B(0xfffda9)==0xaa&&tx_head==tx_tail);
    B(0xfffda7)=0;assert(serial_write(0xbb)&&B(0xfffda1)==0x0c);
    /* RX and TX simultaneously ready: neither direction loses its byte. */
    B(0xfffda9)=0x37;B(0xfffda7)=3;serial_rx_interrupt();
    assert(serial_read()==0x37&&B(0xfffda9)==0xbb&&tx_head==tx_tail);
    assert(serial_read()==-1);
    puts("TX IRQ PASS: bounded FIFO, wrap, drain, idle restart, simultaneous RX/TX");
}
