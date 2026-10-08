#pragma once
#include "link/core.hpp"
#ifndef FADEROS_PLATFORM_PICO
#error "Pico transport requires FADEROS_PLATFORM_PICO"
#endif
#include "hardware/uart.h"

namespace bkds::link {
class PicoUart final : public Transport {
    static constexpr unsigned RingSize=1024;
    static PicoUart* instance;
    uart_inst_t* uart;
    std::array<uint8_t,RingSize> rx{};
    volatile unsigned head=0,tail=0;
    volatile bool overflow=false;
    unsigned irq;
    static void receive_irq();
public:
    PicoUart(unsigned index,unsigned tx,unsigned receive);
    ~PicoUart() override;
    int read(uint8_t*,std::size_t) override;
    int write(const uint8_t*,std::size_t) override;
    int set_baud(uint32_t) override;
};
}
