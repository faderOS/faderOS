#include "uart_transport.hpp"
#include "link/home.hpp"
#include "link/lcd_graphics.hpp"
#include "pico/stdlib.h"
#include <cstdio>
using namespace bkds::link;

// UART bring-up only: no network mixer, persistence, web or USB host yet.
struct Probe final : Listener {
    Panel panel;
    Inputs inputs;
    std::array<uint16_t,2> values{};
    std::array<unsigned,2> samples{};
    bool connected=false;
    void state(const Snapshot& snapshot,bool baseline) override {
        connected=true;
        const auto changes=inputs.update(snapshot,baseline);
        for(unsigned i=0;i<KeyCount;i++) {
            if(changes.pressed[i])std::printf("DOWN %u\n",i);
            if(changes.released[i])std::printf("UP %u\n",i);
        }
        for(unsigned i=0;i<6;i++)if(changes.rotary[i])
            std::printf("ENCODER %u %ld\n",i,long(changes.rotary[i]));
    }
    void analog(unsigned axis,uint16_t,uint16_t value) override {
        if(axis<2){values[axis]=value;++samples[axis];}
    }
    void firmware_info(const char* version,const char* date) override {
        std::printf("PANEL FIRMWARE %s %s\n",version,date);
    }
    void ack(uint8_t type,uint8_t status) override {
        panel.ack(type,status);
        if(status)std::printf("ACK ERROR %u TYPE %u\n",status,type);
    }
    void lost(const char* why) override {
        connected=false;panel.resync();std::printf("LINK LOST %s\n",why);
    }
};
int main() {
    stdio_init_all();
    // Static storage keeps session/output buffers off the small default stack.
    static PicoUart transport(FADEROS_UART_INDEX,FADEROS_UART_TX,FADEROS_UART_RX);
    static Probe probe;
    static Session session(transport,probe);
    probe.panel.lcd(0,"faderOS UART BRING-UP");
    probe.panel.lcd(1,"NO MIXER / USB DIAGNOSTICS");
    session.prefer_baud(FADEROS_PANEL_BAUD);
    uint32_t now=to_ms_since_boot(get_absolute_time());
    session.start(time_us_32()^0xfade2350u,now);
    uint32_t report=now,observed=0;
    while(!session.is_closed()) {
        now=to_ms_since_boot(get_absolute_time());
        session.tick(now);
        if(session.baud()!=observed){observed=session.baud();std::printf("SCI2 BAUD %lu\n",(unsigned long)observed);}
        const int key=getchar_timeout_us(0);
        if(key=='g'||key=='b') {
            if(!session.ready()||!session.lcd_graphics_supported())
                std::printf("LCD GRAPHICS UNAVAILABLE: requires panel firmware 0.23 / capability bit 9\n");
            else if(key=='g') {
                probe.panel.lcd(0,"faderOS CGRAM / EIGHT ARROWS");
                probe.panel.lcd(1,"");
                for(unsigned slot=0;slot<8;slot++)probe.panel.lcd_glyph(slot,LcdArrows[slot]);
                const uint8_t cells[]={0,32,1,32,2,32,3,32,4,32,5,32,6,32,7};
                probe.panel.lcd_cells(40,cells,sizeof cells);
            } else {
                probe.panel.lcd(0,"faderOS BITMAP");probe.panel.lcd(1,"");
                LcdBitmap bitmap(3,2);
                for(unsigned y=1;y<15;y++)bitmap.pixel(7,y);
                for(unsigned i=0;i<5;i++){bitmap.pixel(7-i,1+i);bitmap.pixel(7+i,1+i);}
                bitmap.draw(probe.panel,36,0);
            }
        }
        if(key=='t') {
            probe.panel.lcd(0,"faderOS UART BRING-UP");
            probe.panel.lcd(1,"NO MIXER / USB DIAGNOSTICS");
        }
        if(session.ready())probe.panel.flush(session,now);
        if(uint32_t(now-report)>=1000) {
            const uint32_t elapsed=now-report;report=now;
            std::printf("%s %s / %s %s / SCI2 %lu / TBAR %u JOY %u %u RX Hz %lu %lu\n",
                ProductName,HostVersion,PlatformName,session.ready()?"READY":"WAITING",
                (unsigned long)session.baud(),probe.values[0],unsigned(probe.values[1]>>8),unsigned(probe.values[1]&255),
                (unsigned long)(probe.samples[0]*1000/elapsed),(unsigned long)(probe.samples[1]*1000/elapsed));
            probe.samples.fill(0);
        }
        sleep_us(100);
    }
    // A closed/overflowed transport requires reset; never pretend it is healthy.
    for(;;){std::printf("UART CLOSED: reset Pico to retry\n");sleep_ms(1000);}
}
