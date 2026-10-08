#include "lcd_graphics.hpp"
#include "panel.hpp"
#include <algorithm>
#include <cstring>
namespace bkds::link {
static uint8_t rom_icon(const LcdGlyph& rows) {
    for(unsigned i=0;i<LcdArrows.size();i++)if(rows==LcdArrows[i])return uint8_t(i+1);
    return 0;
}
Panel::Panel() {
    for(auto& l:lines) l.fill(' ');
    segments.fill(15); segments[6]=0;
    resync(); lcd(0,"faderOS BKDS-2010"); lcd(1,"SYSTEM SETUP: press button 47");
}
void Panel::resync() {
    glyph_known.fill(false);
    beep_pending=beep_active=false;
    sent.fill(255); line_known.fill(false); segments_known=false;
    sent_indicators=sent_buzzer=255; writing={}; blocked=false;
}
bool Panel::led(unsigned id,unsigned level,uint32_t period) {
    const auto* c=control(id);
    if(!c||level>2||(period&&period<500)) return false;
    if(c->bank>=0&&level) for(const auto& other:Controls)
        if(other.id!=id&&other.bank==c->bank&&leds[other.id].level&&leds[other.id].level!=level) return false;
    leds[id]={uint8_t(level),period}; blocked=false; return true;
}
bool Panel::lcd(unsigned row,const char* text) {
    if(row>1||std::strlen(text)>40) return false;
    Line l; l.fill(' ');
    for(unsigned i=0;text[i];i++) { if(text[i]<32||text[i]>126) return false; l[i]=text[i]; }
    lines[row]=l; blocked=false; return true;
}
bool Panel::lcd_glyph(unsigned slot,const LcdGlyph& rows) {
    if(slot>=8||std::any_of(rows.begin(),rows.end(),[](uint8_t r){return r>31;}))return false;
    glyphs[slot]=rows;glyph_requested[slot]=true;blocked=false;return true;
}
bool Panel::lcd_cells(unsigned offset,const uint8_t* data,std::size_t count) {
    if(!data||!count||offset>=80||count>80-offset)return false;
    for(std::size_t i=0;i<count;i++)if(data[i]>=8&&data[i]<32)return false;
    for(std::size_t i=0;i<count;i++)lines[(offset+i)/40][(offset+i)%40]=char(data[i]);
    blocked=false;return true;
}
uint8_t Panel::cell(unsigned offset,bool graphics) const {
    const uint8_t value=uint8_t(lines[offset/40][offset%40]);
    if(value<8&&(!graphics||!glyph_requested[value]))return '?';
    if(value>126&&!graphics)return '?';
    return value;
}
void Panel::clear_keypad_lamps() {
    // Numeric LCD edits temporarily replace the keypad's mode feedback.
    for(const auto& c:Controls) if(c.id>=128&&c.id<=156) led(c.id,0);
}
bool Panel::digits(const uint8_t* p) {
    for(unsigned i=0;i<6;i++) if(p[i]&0x70) return false;
    if(p[6]>7) return false;
    std::copy_n(p,7,segments.begin()); blocked=false; return true;
}
void Panel::flush(Session& session,uint32_t now) {
    if(beep_active&&uint32_t(now-beep_started)>=80)beep_active=false;
    const uint8_t sound=uint8_t(buzzer||beep_pending||beep_active);
    // An audible rejection must not wait behind LCD/LED updates. Start the
    // pulse only once ON can be sent; OFF has the same priority.
    if(!session.ready()||(blocked&&sound==sent_buzzer)) return;
    Frame f;
    if(sound!=sent_buzzer&&(sound||sent_buzzer==1)){f.type=0x14;f.length=1;f.payload[0]=sound;}
    for(const auto& c:Controls) {
        const auto& l=leds[c.id];
        uint8_t value=l.period ? (((uint64_t(now)*2/l.period)&1)?l.level:0):l.level;
        if((!f.type||f.type==0x10)&&value!=sent[c.id]&&(sent[c.id]!=255||value!=0)&&f.length<32) {
            f.type=0x10; f.payload[f.length++]=c.id; f.payload[f.length++]=value;
        }
    }
    const bool graphics=session.lcd_graphics_supported();
    if(!f.type&&graphics)for(unsigned slot=0;slot<8;slot++) {
        if(glyph_requested[slot]&&(!glyph_known[slot]||glyphs[slot]!=sent_glyphs[slot])) {
            f.type=session.lcd_icons_supported()&&rom_icon(glyphs[slot])?0x18:0x16;
            f.length=1;f.payload[0]=uint8_t(slot);
            for(unsigned next=slot;next<8&&glyph_requested[next]&&
                (!glyph_known[next]||glyphs[next]!=sent_glyphs[next]);next++) {
                if(f.type==0x18) {
                    const uint8_t id=rom_icon(glyphs[next]);if(!id)break;
                    f.payload[f.length++]=id;
                } else {
                    std::copy(glyphs[next].begin(),glyphs[next].end(),f.payload.begin()+f.length);
                    f.length+=8;
                }
            }
            break;
        }
    }
    // Confirmed tallies take priority over LCD text.
    if(!f.type&&session.lcd_patch_supported()) {
        unsigned first=80,last=0;
        for(unsigned i=0;i<80;i++) if(!line_known[i/40]||cell(i,graphics)!=uint8_t(shown[i/40][i%40])) {
            if(first==80) first=i;
            last=i;
        }
        if(first<80) {
            f.type=0x15; f.payload[0]=uint8_t(first); f.length=uint16_t(last-first+2);
            for(unsigned i=first;i<=last;i++) f.payload[i-first+1]=cell(i,graphics);
            if(std::any_of(f.payload.begin()+1,f.payload.begin()+f.length,[](uint8_t c){return c<32||c>126;}))f.type=0x17;
        }
    } else if(!f.type) for(unsigned row=0;row<2;row++) {
        bool changed=!line_known[row];
        for(unsigned i=0;i<40;i++)if(cell(row*40+i,false)!=uint8_t(shown[row][i]))changed=true;
        if(!changed)continue;
        f.type=0x11; f.length=41; f.payload[0]=uint8_t(row);
        for(unsigned i=0;i<40;i++)f.payload[i+1]=cell(row*40+i,false);
        break;
    }
    if(!f.type) for(const auto& c:Controls) {
        const auto& l=leds[c.id];
        uint8_t value=l.period ? (((uint64_t(now)*2/l.period)&1)?l.level:0):l.level;
        if(value!=sent[c.id]&&f.length<32) {
            f.type=0x10; f.payload[f.length++]=c.id; f.payload[f.length++]=value;
        }
    }
    if(!f.type&&(!segments_known||segments!=sent_segments)) {
        f.type=0x12; f.length=7; std::copy(segments.begin(),segments.end(),f.payload.begin());
    }
    if(!f.type&&indicators!=sent_indicators) { f.type=0x13; f.length=1; f.payload[0]=indicators; }
    if(!f.type&&sound!=sent_buzzer){f.type=0x14;f.length=1;f.payload[0]=sound;}
    if(f.type&&session.send(f.type,f.payload.data(),f.length,now)) {
        writing=f;
        if(f.type==0x14&&f.payload[0]&&beep_pending){beep_pending=false;beep_active=true;beep_started=now;}
    }
}
void Panel::ack(uint8_t type,uint8_t status) {
    if(type!=writing.type) return;
    if(status) { blocked=true; writing={}; return; }
    const auto& p=writing.payload;
    if(type==0x10) for(unsigned i=0;i<writing.length;i+=2) sent[p[i]]=p[i+1];
    if(type==0x11) { std::copy_n(p.begin()+1,40,shown[p[0]].begin()); line_known[p[0]]=true; }
    if(type==0x18) {
        for(unsigned i=1;i<writing.length;i++) {
            const unsigned slot=p[0]+i-1;
            sent_glyphs[slot]=LcdArrows[p[i]-1];glyph_known[slot]=true;
        }
    }
    if(type==0x16) {
        for(unsigned i=0;i<(writing.length-1u)/8;i++) {
            const unsigned slot=p[0]+i;
            std::copy_n(p.begin()+1+i*8,8,sent_glyphs[slot].begin());glyph_known[slot]=true;
        }
    }
    if(type==0x15||type==0x17) {
        unsigned first=p[0],end=first+writing.length-1;
        for(unsigned i=first;i<end;i++) shown[i/40][i%40]=char(p[i-first+1]);
        for(unsigned row=0;row<2;row++) if(first<=row*40&&end>=(row+1)*40) line_known[row]=true;
    }
    if(type==0x12) { std::copy_n(p.begin(),7,sent_segments.begin()); segments_known=true; }
    if(type==0x13) sent_indicators=p[0];
    if(type==0x14) sent_buzzer=p[0];
    writing={};
}
}
