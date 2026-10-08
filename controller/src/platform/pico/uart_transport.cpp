#include "uart_transport.hpp"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/regs/uart.h"

namespace bkds::link {
PicoUart* PicoUart::instance=nullptr;
PicoUart::PicoUart(unsigned index,unsigned tx,unsigned receive)
    :uart(index?uart1:uart0),irq(index?UART1_IRQ:UART0_IRQ) {
    hard_assert(!instance);
    uart_init(uart,9600);
    gpio_set_function(tx,GPIO_FUNC_UART);
    gpio_set_function(receive,GPIO_FUNC_UART);
    uart_set_hw_flow(uart,false,false);
    uart_set_format(uart,8,1,UART_PARITY_NONE);
    uart_set_fifo_enabled(uart,true);
    instance=this;
    irq_set_exclusive_handler(irq,receive_irq);
    irq_set_enabled(irq,true);
    uart_set_irq_enables(uart,true,false);
}
PicoUart::~PicoUart() {
    uart_set_irq_enables(uart,false,false);
    irq_set_enabled(irq,false);
    irq_remove_handler(irq,receive_irq);
    instance=nullptr;
    uart_deinit(uart);
}
void PicoUart::receive_irq() {
    auto& self=*instance;
    while(uart_is_readable(self.uart)) {
        const auto byte=uint8_t(uart_getc(self.uart));
        const unsigned next=(self.head+1)%RingSize;
        if(next==self.tail)self.overflow=true;
        else {self.rx[self.head]=byte;self.head=next;}
    }
}
int PicoUart::read(uint8_t* data,std::size_t size) {
    const auto saved=save_and_disable_interrupts();
    if(overflow) {restore_interrupts(saved);return -1;}
    std::size_t n=0;
    while(n<size&&tail!=head){data[n++]=rx[tail];tail=(tail+1)%RingSize;}
    restore_interrupts(saved);
    return int(n);
}
int PicoUart::write(const uint8_t* data,std::size_t size) {
    std::size_t n=0;
    while(n<size&&uart_is_writable(uart))uart_putc_raw(uart,char(data[n++]));
    return int(n);
}
int PicoUart::set_baud(uint32_t rate) {
    if(rate!=9600&&rate!=38400)return -1;
    // Wait asynchronously for the final byte to leave the shift register.
    if(uart_get_hw(uart)->fr&UART_UARTFR_BUSY_BITS)return 0;
    uart_set_baudrate(uart,rate);
    return 1;
}
}
