#pragma once
#include "menu.hpp"
#include "lcd_graphics.hpp"
#include <cstdio>
#include <limits>

namespace bkds::link {
// Local diagnostic owns all panel controls/outputs; mixer transport stays live.
class PanelTest {
    enum class Mode { menu,buttons,leds,encoders,analog,lcd,segments,indicators,demo } mode=Mode::menu;
    Panel& panel;Panel saved;
    bool running=false,second_page=false;
    unsigned pattern=0,last_key=0;bool last_down=false;
    std::bitset<KeyCount> held;
    std::array<int64_t,6> totals{};std::array<int32_t,6> deltas{};
    uint16_t tbar=0;uint8_t x=128,y=128;
    uint32_t painted=0;bool dirty=true;
    void blank() {
        for(const auto& key:Controls)panel.led(key.id,0);
        panel.set_indicators(0);panel.set_buzzer(false);
        const uint8_t digits[]={15,15,15,15,15,15,0};panel.digits(digits);
    }
    void choose(Mode next) {blank();mode=next;pattern=0;dirty=true;}
    static std::array<uint8_t,4> letter(char c) {
        switch(c) {
        case 'T':return {7,2,2,2};case 'E':return {7,6,4,7};
        case 'S':return {7,4,3,7};case 'D':return {6,5,5,6};
        case 'M':return {5,7,5,5};case 'O':return {7,5,5,7};
        default:return {0,0,0,0};
        }
    }
public:
    explicit PanelTest(Panel& p):panel(p){}
    bool active() const {return running;}
    void start() {saved=panel;running=true;second_page=false;held.reset();totals.fill(0);deltas.fill(0);choose(Mode::menu);}
    void stop() {if(!running)return;running=false;panel=saved;panel.resync();}
    void analog_sample(unsigned axis,uint16_t value) {
        if(!axis)tbar=value;else{x=value>>8;y=value&255;}dirty=true;
    }
    void input(const Snapshot& snapshot,const Changes& changes) {
        held=snapshot.held;
        // EXIT is always navigation; SYSTEM SETUP leaves diagnostic entirely.
        if(changes.pressed[47]) {stop();return;}
        if(changes.pressed[176]) {if(mode==Mode::menu)stop();else choose(Mode::menu);return;}
        if(mode==Mode::menu) {
            if(changes.pressed[180]||changes.pressed[181]||changes.pressed[165]){second_page=!second_page;dirty=true;}
            if(!second_page) {
                if(changes.pressed[160])choose(Mode::buttons);
                if(changes.pressed[161])choose(Mode::leds);
                if(changes.pressed[162])choose(Mode::encoders);
                if(changes.pressed[163])choose(Mode::analog);
                if(changes.pressed[164])choose(Mode::lcd);
            } else {
                if(changes.pressed[160])choose(Mode::segments);
                if(changes.pressed[161])choose(Mode::demo);
                if(changes.pressed[162])panel.beep();
                if(changes.pressed[163])choose(Mode::indicators);
            }
            return;
        }
        for(unsigned i=0;i<KeyCount;i++)if(changes.pressed[i]||changes.released[i]){last_key=i;last_down=changes.pressed[i];dirty=true;}
        for(unsigned i=0;i<6;i++)if(changes.rotary[i]) {
            const int64_t delta=changes.rotary[i];
            if((delta>0&&totals[i]>std::numeric_limits<int64_t>::max()-delta)||
               (delta<0&&totals[i]<std::numeric_limits<int64_t>::min()-delta))totals[i]=0;
            totals[i]+=delta;deltas[i]=changes.rotary[i];if(mode==Mode::encoders)pattern=i;dirty=true;
        }
        if(mode==Mode::leds||mode==Mode::segments||mode==Mode::lcd||mode==Mode::indicators) {
            for(unsigned i=0;i<6;i++)if(changes.pressed[160+i]){pattern=i;if(mode==Mode::leds&&i==5)panel.beep();dirty=true;}
        }
        if(mode==Mode::encoders)for(unsigned i=0;i<6;i++)if(changes.pressed[160+i]){totals[i]=0;deltas[i]=0;dirty=true;}
    }
    void tick(uint32_t now) {
        if(!running||uint32_t(now-painted)<100)return;
        const bool animated=mode==Mode::demo||(mode==Mode::leds&&pattern>=3)||mode==Mode::segments||mode==Mode::indicators;
        if(!dirty&&!animated)return;
        dirty=false;painted=now;
        char top[41]{},bottom[41]{};
        SoftMenu::navigation(panel,true,mode==Mode::menu&&second_page,mode==Mode::menu&&!second_page);
        if(mode==Mode::menu) {
            SoftMenu menu;menu.breadcrumb("TEST");
            const char* first[]={"KEYS","LEDS","ENC","ANLG","LCD","MORE"};
            const char* second[]={"7SEG","DEMO","BEEP","INDIC","","MORE"};
            for(unsigned i=0;i<6;i++)menu.field(i,second_page?second[i]:first[i]);
            menu.show(panel);return;
        }
        if(mode==Mode::buttons) {
            std::snprintf(top,sizeof top,"TEST KEYS: %03u %s / HELD %u",last_key,last_down?"DOWN":"UP",unsigned(held.count()));
            unsigned used=0;
            for(unsigned i=0;i<KeyCount;i++)if(held[i]&&used<35){used+=unsigned(std::snprintf(bottom+used,sizeof bottom-used,"%u ",i));}
            if(!used)std::snprintf(bottom,sizeof bottom,"PRESS KEYS / EXIT: BACK");
            for(const auto& key:Controls)panel.led(key.id,held[key.id]?2:0);
            panel.led(176,1);
        } else if(mode==Mode::encoders) {
            std::snprintf(top,sizeof top,"TEST ENCODERS / F1-F6: ZERO COUNTER");
            const unsigned axis=pattern;
            std::snprintf(top,sizeof top,"TEST ENC %u DELTA %+d",axis+1,int(deltas[axis]));
            SoftMenu menu;menu.title(top);
            for(unsigned i=0;i<6;i++) {char value[24];const auto v=std::max<int64_t>(-99999,std::min<int64_t>(999999,totals[i]));std::snprintf(value,sizeof value,"%lld",(long long)v);menu.field(i,value);}
            menu.show(panel,int(axis));return;
        } else if(mode==Mode::analog) {
            std::snprintf(top,sizeof top,"TEST ANALOG / RAW VALUES");
            std::snprintf(bottom,sizeof bottom,"TBAR %4u / X %3u / Y %3u",tbar,x,y);
        } else if(mode==Mode::leds) {
            const char* names[]={"OFF","LOW","HIGH","CHASE","BLINK","BEEP"};
            SoftMenu menu;std::snprintf(top,sizeof top,"TEST LEDS ~ %s",names[pattern]);menu.title(top);for(unsigned i=0;i<6;i++)menu.field(i,names[i]);menu.show(panel,int(pattern));
            const unsigned chase=(now/200)%Controls.size();
            // Clear each bank before selecting its next level (bank hardware constraint).
            for(const auto& key:Controls)panel.led(key.id,0);
            for(unsigned i=0;i<Controls.size();i++) {
                const auto& key=Controls[i];unsigned level=pattern==1?1:pattern==2?2:pattern==3&&i==chase?2:pattern==4?((now/500)%2?2:1):0;
                panel.led(key.id,level);
            }
            panel.led(176,1);return;
        } else if(mode==Mode::segments) {
            std::snprintf(top,sizeof top,"TEST 7SEG / F1 ALL F2 DIGITS F3 WALK");
            std::snprintf(bottom,sizeof bottom,"F4 BLANK / EXIT: BACK");
            uint8_t digits[7]={};
            for(unsigned i=0;i<6;i++)digits[i]=pattern==0?0x88:pattern==1?uint8_t((now/400+i)%10):pattern==2?(i==(now/300)%6?8:15):15;
            digits[6]=pattern==0?7:pattern>=3?0:uint8_t(1u<<((now/500)%3));panel.digits(digits);
        } else if(mode==Mode::indicators) {
            std::snprintf(top,sizeof top,"TEST INDICATORS / F1 WALK F2 ALL F3 OFF");
            std::snprintf(bottom,sizeof bottom,"EXIT: BACK");
            panel.set_indicators(pattern==0?uint8_t(0x10u<<((now/500)%4)):pattern==1?0xf0:0);
        } else if(mode==Mode::lcd) {
            std::snprintf(top,sizeof top,"TEST LCD / F1 TEXT F2 FILL F3 ARROWS");
            std::snprintf(bottom,sizeof bottom,"0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ");
            panel.lcd(0,top);panel.lcd(1,bottom);
            if(pattern==1){LcdGlyph fill;fill.fill(31);panel.lcd_glyph(0,fill);std::array<uint8_t,80> cells{};panel.lcd_cells(0,cells.data(),cells.size());}
            if(pattern==2){for(unsigned i=0;i<8;i++)panel.lcd_glyph(i,LcdArrows[i]);uint8_t cells[32];std::memset(cells,' ',32);for(unsigned i=0;i<8;i++)cells[i*4]=uint8_t(i);panel.lcd_cells(40,cells,32);}return;
        } else if(mode==Mode::demo) {
            std::snprintf(top,sizeof top,"faderOS PANEL DEMO / TEST ~ DEMO");
            std::snprintf(bottom,sizeof bottom,"BUS PIXEL BANNER / EXIT: BACK");
            const char* word=(now/10000)%2?"DEMO":"TEST";
            const unsigned scroll=(now/250)%20;
            for(unsigned row=0;row<4;row++)for(unsigned col=0;col<12;col++)panel.led(row*16+col,0);
            for(unsigned row=0;row<4;row++)for(unsigned col=0;col<12;col++) {
                const unsigned source=(col+scroll)%20;
                const bool pixel=source<16&&source%4<3&&(letter(word[source/4])[row]&(1u<<(2-source%4)));
                panel.led((3-row)*16+col,pixel?((now/5000)%2?1:2):0);
            }
        }
        panel.lcd(0,top);panel.lcd(1,bottom);
    }
};
}
