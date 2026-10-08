#include "link/lcd_graphics.hpp"
#include <cassert>
#include <cstring>
#include <deque>
#include <vector>
using namespace bkds::link;
struct WirePeer final : Transport {
    std::deque<uint8_t> incoming,outgoing;
    int read(uint8_t* data,std::size_t size) override {
        std::size_t n=0;while(n<size&&!incoming.empty()){data[n++]=incoming.front();incoming.pop_front();}return int(n);
    }
    int write(const uint8_t* data,std::size_t size) override {
        outgoing.insert(outgoing.end(),data,data+size);return int(size);
    }
    void inject(const Frame& f) {Wire w;assert(encode(f,w));incoming.insert(incoming.end(),w.bytes.begin(),w.bytes.begin()+w.size);}
    std::vector<Frame> drain() {
        Decoder decoder;Frame f;std::vector<Frame> result;
        while(!outgoing.empty()){if(decoder.feed(outgoing.front(),f))result.push_back(f);outgoing.pop_front();}return result;
    }
};
struct PanelSink final : Listener {
    Panel& panel;
    explicit PanelSink(Panel& p):panel(p){}
    void state(const Snapshot&,bool) override {}
    void ack(uint8_t type,uint8_t status) override {panel.ack(type,status);}
    void lost(const char*) override {assert(false);}
};
struct Fixture {
    Panel panel;WirePeer peer;PanelSink sink{panel};Session session{peer,sink};uint32_t now=0;
    explicit Fixture(uint16_t capabilities) {
        session.start(99,now);advance();peer.drain();
        Frame hello;hello.type=0x81;hello.session=99;hello.length=59;hello.payload[1]=1;
        hello.payload[2]=uint8_t(capabilities>>8);hello.payload[3]=uint8_t(capabilities);
        peer.inject(hello);advance();peer.drain();assert(session.ready());
    }
    void advance(){for(unsigned i=0;i<10;i++)session.tick(now++);}
    std::vector<Frame> flush(){panel.flush(session,now);advance();return peer.drain();}
    void ack(const Frame& sent){Frame reply;reply.type=0x80;reply.seq=sent.seq;reply.session=99;reply.length=1;peer.inject(reply);advance();peer.drain();}
};
int main() {
    Fixture rom(0x61f);
    for(unsigned i=0;i<8;i++)assert(rom.panel.lcd_glyph(i,LcdArrows[i]));
    auto compact=rom.flush();assert(compact.size()==1&&compact[0].type==0x18&&compact[0].length==9);
    for(unsigned i=1;i<9;i++)assert(compact[0].payload[i]==i);
    auto replacement=LcdArrows[0];replacement[0]=31;
    assert(rom.panel.lcd_glyph(0,replacement));rom.ack(compact[0]);
    compact=rom.flush();assert(compact.size()==1&&compact[0].type==0x16&&compact[0].payload[1]==31);
    rom.ack(compact[0]);rom.panel.resync();
    compact=rom.flush();assert(compact[0].type==0x16);rom.ack(compact[0]);
    // A custom upload may include neighboring ROM glyphs; no desired edit is lost.
    Fixture f(0x21f);assert(f.session.lcd_graphics_supported());
    const auto arrow=LcdArrows[0];assert(f.panel.lcd_glyph(0,arrow));
    const uint8_t cells[]={0,32,255};assert(f.panel.lcd_cells(39,cells,3));
    auto out=f.flush();assert(out.size()==1&&out[0].type==0x16&&out[0].length==9&&out[0].payload[0]==0);
    assert(!memcmp(out[0].payload.data()+1,arrow.data(),8));
    // Desired glyph changes while upload is in flight; the old ACK must not lose it.
    auto changed=arrow;changed[0]=31;assert(f.panel.lcd_glyph(0,changed));f.ack(out[0]);
    out=f.flush();assert(out.size()==1&&out[0].type==0x16&&out[0].payload[1]==31);f.ack(out[0]);
    out=f.flush();assert(out.size()==1&&out[0].type==0x17&&out[0].length==81);
    assert(out[0].payload[40]==0&&out[0].payload[41]==32&&out[0].payload[42]==255);f.ack(out[0]);
    auto bad=arrow;bad[7]=32;assert(!f.panel.lcd_glyph(0,bad)&&!f.panel.lcd_glyph(8,arrow));
    const uint8_t invalid[]={1,8};assert(!f.panel.lcd_cells(39,invalid,2)&&f.panel.line(0)[39]==0);
    assert(!f.panel.lcd_cells(79,cells,3)&&!f.panel.lcd_cells(0,nullptr,1));
    f.panel.resync();out=f.flush();assert(out.size()==1&&out[0].type==0x16);f.ack(out[0]);
    // Undefined slots never display uninitialized CGRAM.
    const uint8_t undefined[]={7};assert(f.panel.lcd_cells(0,undefined,1));
    out=f.flush();assert(out.size()==1&&out[0].type==0x17&&out[0].payload[1]=='?');f.ack(out[0]);
    // Older firmware sees ASCII replacements without rejected commands or repaint loops.
    for(uint16_t caps:{uint16_t(0x1f),uint16_t(0xf)}) {
        Fixture old(caps);assert(!old.session.lcd_graphics_supported());
        assert(old.panel.lcd_glyph(0,arrow)&&old.panel.lcd_cells(0,cells,3));
        out=old.flush();assert(out.size()==1&&out[0].type==(caps==0x1f?0x15:0x11));
        assert(out[0].payload[1]=='?'&&out[0].payload[2]==32&&out[0].payload[3]=='?');old.ack(out[0]);
        for(unsigned i=0;i<20;i++){out=old.flush();for(const auto& frame:out){assert(frame.type!=0x16&&frame.type!=0x17);old.ack(frame);}}
        out=old.flush();assert(out.empty());
    }
    assert(!lcd_direction_arrow(5).valid());
    for(unsigned key:{1u,2u,3u,4u,6u,7u,8u,9u}) {
        const auto large=lcd_direction_arrow(key);
        assert(large.valid()&&large.width()==15&&large.height()==16);
        Fixture direction(0x21f);assert(large.draw(direction.panel,36,0));
        auto frames=direction.flush();assert(frames[0].type==0x16&&frames[0].length==49);
        unsigned lit=0;for(unsigned i=1;i<49;i++)for(unsigned bit=0;bit<5;bit++)lit+=(frames[0].payload[i]>>bit)&1;
        assert(lit>=45&&lit<=150);
    }
    // Tile conversion, bit orientation and whole-draw bounds checking.
    Fixture bitmap(0x21f);LcdBitmap image(3,2);
    assert(image.width()==15&&image.height()==16&&image.pixel(0,0)&&image.pixel(14,15));
    assert(!image.pixel(15,0)&&!image.draw(bitmap.panel,38,0)&&!image.draw(bitmap.panel,0,1));
    assert(image.draw(bitmap.panel,36,0));out=bitmap.flush();assert(out[0].type==0x16&&out[0].length==49&&out[0].payload[1]==16);
    assert(!LcdBitmap(5,2).valid()&&!image.draw(bitmap.panel,0,0,3));
}
