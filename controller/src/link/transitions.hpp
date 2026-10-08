#pragma once
#include "mixer.hpp"
#include <array>
namespace bkds::link {
// Portable panel logic; no OBS names, sockets or JSON in the control path.
class TransitionControl {
    Panel& panel;
    MixerAdapter& adapter;
    TransitionType selected=TransitionType::mix;
    struct MeArm {TransitionType selected=TransitionType::mix;bool user_wipe=false;uint32_t code=0,rate=25;unsigned mix_choice=1,stinger_choice=1,preset_code=0,dme_choice=0;bool dme_direct=false;unsigned dme_direct_code=1001,dme_shortcut_choice=0;};
    std::array<MeArm,4> me_arm{};
    int me_slot=0;
    void recall_me(int slot) {
        if(slot<0||slot>3||slot==me_slot) return;
        me_arm[size_t(me_slot)]={selected,user_wipe,code,rates[0],mix_choice,stinger_choice,preset_code,dme_choice,dme_direct,dme_direct_code,dme_shortcut_choice};
        if(adapter.manual_me_pickup()) {
            tbar_banks[size_t(me_slot)]={tbar_home,tbar_physical,tbar_active&&!tbar_finishing};
            const auto& target=tbar_banks[size_t(slot)];
            tbar_home=target.home;tbar_target=target.position;tbar_frozen=target.active;
            const auto live=adapter.state();
            if(live.manual_transition&&live.manual_position>=0&&tbar_home>=0)
                tbar_target=tbar_home==0?live.manual_position:4095-live.manual_position;
            tbar_previous=tbar_physical;tbar_pickup=true;tbar_active=tbar_finishing=false;
        }
        me_slot=slot;
        const MeArm& arm=me_arm[size_t(slot)];
        selected=arm.selected;user_wipe=arm.user_wipe;code=arm.code;rates[0]=arm.rate;mix_choice=arm.mix_choice;stinger_choice=arm.stinger_choice;preset_code=arm.preset_code;dme_choice=arm.dme_choice;dme_direct=arm.dme_direct;dme_direct_code=arm.dme_direct_code;dme_shortcut_choice=arm.dme_shortcut_choice;
        modify_menu=background_arm=mix_params_menu=false;for(unsigned i=0;i<12;++i)panel.led(48+i,0);
        wipe_menu=mix_menu=false; direct=false; typing=false; invalid=false; rate_slot=-1;
    }
    std::array<uint32_t,3> rates{300,300,300};
    unsigned rate_kind=0;
    unsigned ftb_rate_seen=0,rate_seen=0,dsk_rate_seen=0,dsk_rate_slot_seen=2;
    int rate_slot=-1,last_rate_slot=0;
    bool mix_menu=false;
    bool soft_menu=false,user_wipe=false,wipe_menu=false,direct=false,typing=false,invalid=false;
    bool geometry_menu=false,mix_params_menu=false,mix_super=false;
    std::array<uint32_t,3> mix_values{};uint32_t mix_revision=0;
    bool modify_menu=false,modify_pending=false,modify_focus_saved=false;
    uint32_t modify_clicked_at=0;
    struct ModifyFocus { bool menu=false,soft=false,keypad=false,typing=false,invalid=false;unsigned value=0;bool geometry=false,mix_params=false; } modify_before;
    std::array<DmeParameters,2> dme_params{};
    bool has_dme_parameters() const {return (dme_menu||is_dme(selected))&&(adapter.supports_dme_background(dme_choice)||(adapter.supports_dme_parameters()&&!adapter.dme_keypad_grid()&&dme_choice!=0));}
    TransitionType armed_dme() const {return dme_choice==1?TransitionType::slide:dme_choice==2?TransitionType::swipe:TransitionType::dme;}
    DmeParameters& parameters() {return dme_params[dme_choice==2?1:0];}
    bool dme_modified() const {if(adapter.supports_dme_background(dme_choice))return adapter.dme_background(dme_choice)!=-1;for(const auto& p:dme_params)if(p.entry!=1||p.inward)return true;return false;}
    bool dme_menu=false;
    bool background_arm=false,background_color=false;uint32_t background_failures=0;
    bool dme_direct=false;unsigned dme_direct_code=1001,dme_shortcut_choice=0;
    unsigned mix_choice=1,dme_choice=0,stinger_choice=1;
    bool dsk_shifted=false,dsk_locked=false,dsk_grace=false;
    uint32_t dsk_pressed_at=0;
    bool mix_params_target()const {return !wipe_menu&&selected==TransitionType::mix&&(mix_choice==5||mix_choice==7)&&adapter.supports_mix_preparation(mix_choice==7);}
    bool mix_params_modified()const {if(!mix_params_target())return false;auto s=adapter.state();return mix_choice==5?s.dip_rgb!=0:s.super_gain_a!=100||s.super_gain_b!=100;}
    void read_mix_values(){auto s=adapter.state();mix_revision=s.mix_preparation_revision;mix_values=mix_super?std::array<uint32_t,3>{s.super_gain_a,s.super_gain_b,0}:std::array<uint32_t,3>{s.dip_rgb>>16,(s.dip_rgb>>8)&255,s.dip_rgb&255};}
    bool push_mix_values(){return adapter.prepare_mix(mix_super,mix_super?mix_values[0]:(mix_values[0]<<16)|(mix_values[1]<<8)|mix_values[2],mix_super?mix_values[1]:0);}
    void toggle_user();
    unsigned preset_code=0;
    std::array<uint32_t,10> presets{};
    void wipe_identity(char* out,unsigned size) const {std::snprintf(out,size,"%u",code);}
    unsigned dme_shortcut(unsigned digit)const {return digit;}
    bool dme_shortcut_available(unsigned digit)const {
        if(digit>9||!adapter.supports_dme())return false;
        if(adapter.dme_keypad_grid())return digit!=0&&digit!=5&&adapter.keypad_transition_available(TransitionType::dme,digit);
        auto last=adapter.keypad_transition_slots(TransitionType::dme);
        if(!last)return digit<3&&adapter.keypad_transition_available(TransitionType::dme,dme_shortcut(digit));
        return digit>=adapter.first_keypad_transition_slot(TransitionType::dme)&&digit<=last&&adapter.keypad_transition_available(TransitionType::dme,digit);
    }
    unsigned dme_shortcut_digit()const {for(unsigned i=0;i<10;++i)if(dme_shortcut(i)==dme_choice)return i;return 10;}
    unsigned mix_slots()const {auto slots=adapter.keypad_transition_slots(TransitionType::mix);return slots?slots:adapter.mix_dip()?5:0;}
    bool mix_available(unsigned digit)const {if(adapter.keypad_transition_slots(TransitionType::mix))return digit>=1&&digit<=mix_slots()&&adapter.keypad_transition_available(TransitionType::mix,digit);return adapter.mix_dip()&&(digit==1||digit==5);}
    bool user_stingers_available()const {if(!adapter.stinger_slots())return false;for(unsigned i=adapter.first_stinger_slot();i<10&&i<=adapter.stinger_slots();++i)if(adapter.keypad_transition_available(TransitionType::stinger,i))return true;return false;}
    bool soft_keypad=false,saved_typing=false,saved_invalid=false;
    unsigned saved_value=0;
    void close_soft_keypad();
    unsigned value=0,code=0,direction=0,softness=3;
    unsigned direct_code=0;
    bool soft_all=true,soft_full=false;
    struct SoftOverride { uint32_t code=0,soft=0; bool used=false; };
    std::array<SoftOverride,32> soft_overrides{};
    bool geometry_target() const {return (adapter.wipe_geometry_supported(code)||adapter.wipe_tiles_supported(code))&&(wipe_menu?!dme_menu:selected==TransitionType::wipe);}
    unsigned applied_vertices() const;
    unsigned applied_rounding() const;
    void store_geometry(unsigned vertices,unsigned rounding);
    unsigned applied_tile_size() const;
    bool mosaic_target() const {return adapter.wipe_tiles_supported(code);}
    unsigned geometry_value() const {return mosaic_target()?applied_tile_size():code==49?applied_vertices():applied_rounding();}
    unsigned geometry_minimum() const {return mosaic_target()?2:code==49?3:0;}
    unsigned geometry_maximum() const {return code==49?64:50;}
    unsigned geometry_default() const {return mosaic_target()?10:code==49?5:15;}
    void store_geometry_value(unsigned value);
    void reset_geometry();
    unsigned applied_soft() const;
    void set_soft(uint32_t wipe,uint32_t soft);
    void clear_soft_overrides();
    void reset_soft();
    unsigned edit_soft() const;
    bool bord_menu=false,multi_menu=false,aspect_menu=false,pos_menu=false;
    bool bord_all=true,multi_all=true,aspect_all=true,pos_all=false;
    int joystick_x=0,joystick_y=0;uint32_t position_clock=0;bool position_clock_valid=false;
    std::array<int64_t,2> position_remainder{};
    unsigned native_styles=0;
    uint32_t global_border=0,global_multi=1,global_aspect=0;
    int global_px=500,global_py=500;
    struct StyleOverride { uint32_t code=0,multi=1,border=0,aspect=0; int px=500,py=500; uint32_t vertices=5,rounding=15,tile_size=10;bool has_tiles=false,has_geometry=false;bool used=false,has_border=false,has_multi=false,has_aspect=false,has_pos=false; };
    std::array<StyleOverride,512> style_overrides{};
    bool style_full=false,style_keypad=false,style_pending=false,style_focus_saved=false;
    unsigned style_key=0,style_kind=0;
    uint32_t style_clicked_at=0;
    struct StyleFocus { bool soft=false,modify=false,bord=false,multi=false,aspect=false,pos=false,keypad=false,typing=false,invalid=false; unsigned value=0; } style_before;
    void close_style_menus();
    void reset_style(unsigned key);
    void push_style(bool persist);
    StyleOverride* style_slot(uint32_t wipe);
    unsigned applied_border() const;
    unsigned applied_multi() const;
    unsigned applied_aspect() const;
    int applied_px() const;
    int applied_py() const;
    void store_border(uint32_t amount);
    void store_multi(uint32_t count);
    void store_aspect(uint32_t index);
    void store_pos(int x,int y);
    bool next_reverse=false,pending_alternate=false;
    struct TbarBank {int home=-1,position=-1;bool active=false;};
    std::array<TbarBank,4> tbar_banks{};
    int tbar_physical=-1,tbar_previous=-1,tbar_target=-1;
    bool tbar_pickup=false,tbar_frozen=false;
    uint32_t tbar_clock=0;
    int tbar_home=-1,tbar_endpoint=-1;
    bool tbar_active=false,tbar_finishing=false;
    uint32_t completion=0;
    bool soft_pending=false;
    struct SoftFocus { bool menu=false,keypad=false,typing=false,invalid=false,modify=false; unsigned value=0; } soft_before_click;
    bool soft_focus_saved=false;
    uint32_t soft_clicked_at=0;
    void update_completion();
public:
    TransitionControl(Panel& p,MixerAdapter& a):panel(p),adapter(a) {
        for(unsigned i=0;i<presets.size();++i) presets[i]=adapter.wipe_preset(i);
        softness=adapter.default_softness();if(adapter.wipe_aspect_percentage())global_aspect=adapter.wipe_aspect_default();
    }
    void defaults(const TransitionSettings& s) { cancel_modifiers(); close_soft_keypad(); rates=adapter.frame_rates()?std::array<uint32_t,3>{25,25,25}:s.rates; rate_seen=0; ftb_rate_seen=0; dsk_rate_seen=0; dsk_rate_slot_seen=2; softness=s.softness; clear_soft_overrides(); soft_all=true; soft_menu=false; modify_menu=false; close_style_menus(); pending_alternate=false; rate_slot=-1; typing=false; invalid=false; for(auto& arm:me_arm) arm={selected,user_wipe,code,rates[0],mix_choice,stinger_choice,preset_code,dme_choice,dme_direct,dme_direct_code,dme_shortcut_choice}; me_slot=0; }
    bool press(const std::bitset<KeyCount>&,const std::bitset<KeyCount>& doubles={});
    void dsk_shift(Changes&,uint32_t now);
    unsigned dsk_slot() const { return dsk_shifted?1u:0u; }
    void sync_dsk_shift(bool held) {dsk_shifted=held&&adapter.dsk_channels()>1;dsk_locked=dsk_grace=false;}
    void modifiers(Changes&,uint32_t now,bool allow_menu);
    void advance_modifiers(uint32_t now,bool allow_menu);
    void cancel_modifiers() { soft_pending=false; soft_focus_saved=false; modify_pending=false;modify_focus_saved=false; style_pending=false; style_focus_saved=false; }
    void joystick(int x,int y);
    void position_tick(uint32_t now);
    void position_rotary(int32_t dx,int32_t dy);
    void refresh(bool display=true,bool upper_bank=false);
    void cancel_edit_for(const std::bitset<KeyCount>&);
    void rotary(int32_t delta);
    bool background_press(const std::bitset<KeyCount>& keys,bool upper);
    void tbar(uint16_t position);
    void mix_rotary(int a,int b,int c);
    void reset_tbar() {adapter.cancel_manual();tbar_home=tbar_endpoint=-1;tbar_active=tbar_finishing=false;tbar_pickup=tbar_frozen=false;tbar_banks={};tbar_physical=tbar_previous=tbar_target=-1;}
    unsigned soft_value() const { return applied_soft(); }
    void close_lcd_menu(){cancel_modifiers();close_soft_keypad();close_style_menus();soft_menu=modify_menu=dme_menu=geometry_menu=mix_params_menu=false;}
    bool owns_lcd() const { return geometry_menu||(mix_menu&&mix_slots())||(wipe_menu&&dme_menu)||soft_menu||modify_menu||bord_menu||multi_menu||aspect_menu||pos_menu; }
    TransitionType type() const { return selected==TransitionType::mix&&mix_choice==5&&adapter.mix_dip()&&!adapter.keypad_transition_slots(TransitionType::mix)?TransitionType::dip:selected==TransitionType::wipe?(user_wipe?TransitionType::stinger:TransitionType::wipe):selected==TransitionType::dme?((adapter.dme_keypad_grid()||adapter.keypad_transition_slots(TransitionType::dme))?TransitionType::dme:armed_dme()):selected; }
    uint32_t duration(unsigned slot) const { return rates[slot]; }
};
}
