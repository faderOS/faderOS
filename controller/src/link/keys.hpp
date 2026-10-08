#pragma once
#include "mixer.hpp"
namespace bkds::link {
// KEY CONTROL delegations for mixers that expose upstream keyers.
// Frame-memory banks select MP1 stills independently of keyer routing. Double-click KEY1/KEY2 delegation selects KEY3/KEY4. DSK delegation can latch DSK2.
class KeyControl {
    Panel& panel;
    MixerAdapter& adapter;
    unsigned delegation=0; // 0..3 upstream, 4 downstream
    bool dsk_upper=false;
    int supersource_side=-1, supersource_window=-1;
    bool supersource_menu=false,supersource_aux_owned=false;
    uint32_t supersource_failures=0;
    int aux_role=-1;
    std::bitset<4> aux_held;
    std::bitset<12> overlay_held;
    uint32_t overlay_failures=0;
    std::array<bool,6> split_selected{};
    bool source_arm=false,source_cut=false;
    int media_bank=-1;
    bool capture_waiting=false,media_menu=false;
    bool delete_mode=false,clear_waiting=false,show_clear=false;
    int delete_target=-1;
public:
    KeyControl(Panel& p,MixerAdapter& a):panel(p),adapter(a) {}
    void overlay_sync(const std::bitset<KeyCount>& held) {for(unsigned i=0;i<12;i++)overlay_held[i]=held[32+i];for(unsigned i=0;i<4;i++)aux_held[i]=held[84+i];}
    bool aux_chord(const std::bitset<KeyCount>& keys);
    bool overlay_press(const std::bitset<KeyCount>& keys,bool second_layer);
    void reset() { aux_held.reset();overlay_held.reset();overlay_failures=0; split_selected.fill(false);delegation=0;dsk_upper=false;aux_role=-1; source_arm=source_cut=false;media_bank=-1;capture_waiting=false;supersource_side=supersource_window=-1;supersource_menu=false;media_menu=delete_mode=clear_waiting=show_clear=false;delete_target=-1; }
    bool press(const std::bitset<KeyCount>& keys,unsigned dsk_slot,const std::bitset<KeyCount>& doubles={},bool second_layer=false);
    bool owns_lcd()const{return (media_menu&&media_bank>=0)||(supersource_side>=0&&supersource_menu);}
    void cancel_menu_for(const std::bitset<KeyCount>& keys);
    void render();
    void refresh(unsigned dsk_slot,bool second_layer=false);
};
}
