#pragma once
#include "panel.hpp"
#include <algorithm>
#include <cstring>
#include <string>
namespace bkds::link {
// Six physical soft keys beneath the 40-column LCD. Centres follow the
// panel layout; each field owns its columns so long labels cannot overlap.
class SoftMenu {
    std::array<Line,2> rows{};
public:
    inline static constexpr unsigned Begin[7]={0,6,13,20,27,34,40};
    SoftMenu() { for(auto& row:rows) row.fill(' '); }
    void title(const char* text) {
        rows[0].fill(' ');
        const auto n=std::min<std::size_t>(40,std::strlen(text));
        std::copy_n(text,n,rows[0].begin());
    }
    void breadcrumb(const char* section,const char* leaf=nullptr) {
        std::string text="SYSTEM SETUP";
        if(section&&*section) text+=" ~ "+std::string(section);
        if(leaf&&*leaf) text+=" ~ "+std::string(leaf);
        if(text.size()>40) text.replace(0,12,"SETUP");
        title(text.c_str()); // HD44780 A00: 0x7e is the right arrow.
    }
    static void navigation(Panel& panel,bool back,bool up=false,bool down=false) {
        panel.led(176,back?1:0);panel.led(180,up?1:0);panel.led(181,down?1:0);
    }
    void field(unsigned slot,const char* text) {
        if(slot>=6) return;
        unsigned width=Begin[slot+1]-Begin[slot];
        unsigned n=unsigned(std::min<std::size_t>(width,std::strlen(text)));
        std::fill(rows[1].begin()+Begin[slot],rows[1].begin()+Begin[slot+1],' ');
        std::copy_n(text,n,rows[1].begin()+Begin[slot]+(width-n)/2);
    }
    void show(Panel& panel,int selected=-1) const {
        for(unsigned r=0;r<2;r++) {
            char text[41]{}; std::copy(rows[r].begin(),rows[r].end(),text); panel.lcd(r,text);
        }
        for(unsigned i=0;i<6;i++) panel.led(160+i,int(i)==selected?2:0);
    }
};
}
