#pragma once
#include "core.hpp"
#include "catalog.hpp"
namespace bkds::link {
using LcdGlyph=std::array<uint8_t,8>;
using Line=std::array<char,40>;
class Panel {
    struct Led { uint8_t level=0; uint32_t period=0; };
    std::array<Led,KeyCount> leds{};
    std::array<uint8_t,KeyCount> sent{};
    std::array<Line,2> lines{},shown{};
    std::array<bool,2> line_known{};
    std::array<uint8_t,7> segments{},sent_segments{};
    uint8_t indicators=0,buzzer=0,sent_indicators=255,sent_buzzer=255;
    bool beep_pending=false,beep_active=false;uint32_t beep_started=0;
    bool segments_known=false,blocked=false;
    std::array<LcdGlyph,8> glyphs{},sent_glyphs{};
    std::array<bool,8> glyph_requested{},glyph_known{};
    uint8_t cell(unsigned offset,bool graphics) const;
    Frame writing{};
public:
    Panel();
    bool led(unsigned id,unsigned level,uint32_t period=0);
    void clear_keypad_lamps();
    unsigned desired_led(unsigned id) const { return id<KeyCount?leds[id].level:0; }
    uint32_t desired_period(unsigned id) const { return id<KeyCount?leds[id].period:0; }
    const std::array<uint8_t,7>& desired_digits() const { return segments; }
    bool lcd(unsigned row,const char* text);
    bool lcd_glyph(unsigned slot,const LcdGlyph& rows);
    bool lcd_cells(unsigned offset,const uint8_t* data,std::size_t count);
    void resync();
    void flush(Session&,uint32_t now);
    void ack(uint8_t type,uint8_t status);
    const Line& line(unsigned row) const { return lines[row]; }
    bool digits(const uint8_t* bytes);
    void set_tbar_indicators(uint8_t b) {indicators=(indicators&0xc0)|(b&0x30);blocked=false;}
    uint8_t desired_indicators() const {return indicators;}
    void set_indicators(uint8_t b) { indicators=b&0xf0; blocked=false; }
    void beep() { if(!beep_active)beep_pending=true; blocked=false; }
    void set_buzzer(bool on) { buzzer=on; blocked=false; }
};
}
