#include "board.h"
#define REG8(a) (*(volatile uint8_t *)(a))
void uart_init(void)
{
    /* TMP68301 SCI2, internal 16 MHz clock, 8N1, polling, nominal 9600.
       SPR is shared: this bring-up driver takes ownership of all UART clocks. */
    /* EPROM may hand over with its final diagnostic still transmitting. */
    while (!(REG8(0xfffda7)&4u)) {}
    REG8(0xfffd8f)=0xa1;
    REG8(0xfffd8f)=0x81;
    REG8(0xfffd8d)=26;
    REG8(0xfffda5)=8;
    REG8(0xfffda1)=0xce;
    REG8(0xfffda3)=0x15;
    REG8(0xfffda3)=0x05;
}
void uart_puts(const char *s)
{
    while (*s) {
        while (!(REG8(0xfffda7)&1u)) {}
        REG8(0xfffda9)=(uint8_t)*s++;
    }
    /* Drain the shift register before a handover can reset the UART. */
    while (!(REG8(0xfffda7)&4u)) {}
}
void uart_hex(uint32_t value, unsigned digits)
{
    while (digits--) {
        unsigned nibble=(value>>(digits*4))&0xfu;
        while (!(REG8(0xfffda7)&1u)) {}
        REG8(0xfffda9)=(uint8_t)(nibble<10 ? '0'+nibble : 'A'+nibble-10);
    }
    while (!(REG8(0xfffda7)&4u)) {}
}
int uart_getc(void)
{
    uint8_t status=REG8(0xfffda7);
    if (status&0x78u) { REG8(0xfffda3)=0x15; REG8(0xfffda3)=5; }
    return (status&2u) ? REG8(0xfffda9) : -1;
}
