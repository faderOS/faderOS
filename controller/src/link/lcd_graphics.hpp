#pragma once
#include "panel.hpp"

namespace bkds::link {
// CGRAM slots are global to the entire display. Callers own/reserve their slots.
inline constexpr std::array<LcdGlyph,8> LcdArrows={{
    {{4,14,21,4,4,4,4,0}},       // up
    {{4,4,4,4,21,14,4,0}},       // down
    {{0,4,8,31,8,4,0,0}},       // left
    {{0,4,2,31,2,4,0,0}},       // right
    {{31,24,20,18,1,0,0,0}},    // top-left
    {{31,3,5,9,16,0,0,0}},      // top-right
    {{0,0,1,18,20,24,31,0}},    // bottom-left
    {{0,0,16,9,5,3,31,0}}      // bottom-right
}};
// Small tiled monochrome bitmap: each 5x8 cell consumes one of eight slots.
// Physical gaps between LCD character cells remain visible.
class LcdBitmap {
    unsigned columns,rows;
    std::array<LcdGlyph,8> tiles{};
public:
    LcdBitmap(unsigned width_cells,unsigned height_cells):columns(width_cells),rows(height_cells) {}
    bool valid() const {return columns>0&&columns<=8&&rows>0&&rows<=2&&columns*rows<=8;}
    unsigned width() const {return valid()?columns*5:0;}
    unsigned height() const {return valid()?rows*8:0;}
    bool pixel(unsigned x,unsigned y,bool on=true) {
        if(!valid()||x>=width()||y>=height())return false;
        auto& line=tiles[(y/8)*columns+x/5][y%8];
        const auto mask=uint8_t(1u<<(4-x%5));
        if(on)line|=mask;else line&=uint8_t(~mask);
        return true;
    }
    bool draw(Panel& panel,unsigned column,unsigned row,unsigned first_slot=0) const {
        if(!valid()||column>=40||row>=2||columns>40-column||rows>2-row||
           first_slot>=8||columns*rows>8-first_slot)return false;
        for(unsigned i=0;i<columns*rows;i++)panel.lcd_glyph(first_slot+i,tiles[i]);
        for(unsigned r=0;r<rows;r++) {
            std::array<uint8_t,8> cells{};
            for(unsigned c=0;c<columns;c++)cells[c]=uint8_t(first_slot+r*columns+c);
            panel.lcd_cells((row+r)*40+column,cells.data(),columns);
        }
        return true;
    }
};
// Direction follows the panel keypad compass: 7/8/9 above, 1/2/3 below.
inline LcdBitmap lcd_direction_arrow(unsigned key) {
    int dx=0,dy=0;
    switch(key) {
    case 7:dx=-1;dy=-1;break;case 8:dy=-1;break;case 9:dx=1;dy=-1;break;
    case 4:dx=-1;break;case 6:dx=1;break;
    case 1:dx=-1;dy=1;break;case 2:dy=1;break;case 3:dx=1;dy=1;break;
    default:return LcdBitmap(0,0);
    }
    LcdBitmap image(3,2);
    for(unsigned y=0;y<16;y++)for(unsigned x=0;x<15;x++) {
        const int xx=int(x)*2-14,yy=int(y)*2-15;
        int along=xx*dx+yy*dy,across=-xx*dy+yy*dx;
        if(dx&&dy){along=along*7/10;across=across*7/10;}
        if(across<0)across=-across;
        const bool shaft=along>=-13&&along<=2&&across<=3;
        const bool head=along>=0&&along<=14&&across<=14-along;
        if(shaft||head)image.pixel(x,y);
    }
    return image;
}
}
