#include "transitions.hpp"
#include <cstdio>
#include "menu.hpp"
#include "keypad.hpp"
#include "lcd_graphics.hpp"
namespace bkds::link {
unsigned stepped_multi(unsigned current, int delta, bool wrap) {
    static const unsigned steps[]={1,2,4,9,16};
    unsigned index=0;
    for(unsigned i=0;i<5;i++) if(steps[i]==current) index=i;
    int next=int(index)+delta;
    if(wrap) next=(next%5+5)%5;
    else { if(next<0) next=0; if(next>4) next=4; }
    return steps[unsigned(next)];
}
unsigned TransitionControl::applied_tile_size() const {
    for(const auto& o:style_overrides)if(o.used&&o.code==code&&o.has_tiles)return o.tile_size;
    return 10;
}
void TransitionControl::store_geometry_value(unsigned value){
    if(mosaic_target()){if(auto* o=style_slot(code)){o->tile_size=value;o->has_tiles=true;}}
    else store_geometry(code==49?value:applied_vertices(),code==49?applied_rounding():value);
}
void TransitionControl::reset_geometry(){store_geometry_value(geometry_default());}
unsigned TransitionControl::applied_vertices() const {
    for(const auto& o:style_overrides)if(o.used&&o.code==code&&o.has_geometry)return o.vertices;
    const auto state=adapter.state();return state.wipe_geometry_known?state.wipe_vertices:5;
}
unsigned TransitionControl::applied_rounding() const {
    for(const auto& o:style_overrides)if(o.used&&o.code==code&&o.has_geometry)return o.rounding;
    return 15;
}
void TransitionControl::store_geometry(unsigned vertices,unsigned rounding){if(auto* o=style_slot(code)){o->vertices=vertices;o->rounding=rounding;o->has_geometry=true;}}
unsigned TransitionControl::applied_soft() const {
    for(const auto& o:soft_overrides) if(o.used&&o.code==code) return o.soft;
    return softness;
}
unsigned TransitionControl::edit_soft() const {
    return soft_all?softness:applied_soft();
}
void TransitionControl::set_soft(uint32_t wipe,uint32_t soft) {
    soft_full=false;
    for(auto& o:soft_overrides) if(o.used&&o.code==wipe) { o.soft=soft; return; }
    for(auto& o:soft_overrides) if(!o.used) { o={wipe,soft,true}; return; }
    soft_full=invalid=true;panel.beep(); // Never silently evict another wipe's configuration.
}
void TransitionControl::clear_soft_overrides() {
    soft_full=false;
    for(auto& o:soft_overrides) o.used=false;
}
void TransitionControl::reset_soft() {
    softness=0;clear_soft_overrides();soft_all=true;
    if(soft_keypad) {value=0;typing=false;invalid=false;}
}
void TransitionControl::close_style_menus() {
    const bool cursor=pos_menu;
    bord_menu=multi_menu=aspect_menu=pos_menu=style_keypad=geometry_menu=mix_params_menu=false;
    if(cursor) push_style(true);
}
TransitionControl::StyleOverride* TransitionControl::style_slot(uint32_t wipe) {
    for(auto& o:style_overrides) if(o.used&&o.code==wipe) return &o;
    for(auto& o:style_overrides) if(!o.used) { o=StyleOverride{}; o.code=wipe; o.used=true; return &o; }
    style_full=invalid=true;panel.beep(); return nullptr;
}
unsigned TransitionControl::applied_border() const {
    if(!bord_all) for(const auto& o:style_overrides) if(o.used&&o.code==code&&o.has_border) return o.border;
    return global_border;
}
unsigned TransitionControl::applied_multi() const {
    if(!multi_all) for(const auto& o:style_overrides) if(o.used&&o.code==code&&o.has_multi) return o.multi;
    return global_multi;
}
unsigned TransitionControl::applied_aspect() const {
    if(!aspect_all) for(const auto& o:style_overrides) if(o.used&&o.code==code&&o.has_aspect) return o.aspect;
    return global_aspect;
}
int TransitionControl::applied_px() const {
    if(!pos_all) for(const auto& o:style_overrides) if(o.used&&o.code==code&&o.has_pos) return o.px;
    return global_px;
}
int TransitionControl::applied_py() const {
    if(!pos_all) for(const auto& o:style_overrides) if(o.used&&o.code==code&&o.has_pos) return o.py;
    return global_py;
}
void TransitionControl::store_border(uint32_t amount) {
    if(bord_all) { global_border=amount; for(auto& o:style_overrides) o.has_border=false; return; }
    if(auto* o=style_slot(code)) { o->border=amount; o->has_border=true; }
}
void TransitionControl::store_multi(uint32_t count) {
    if(multi_all) { global_multi=count; for(auto& o:style_overrides) o.has_multi=false; return; }
    if(auto* o=style_slot(code)) { o->multi=count; o->has_multi=true; }
}
void TransitionControl::store_aspect(uint32_t index) {
    if(aspect_all) { global_aspect=index; for(auto& o:style_overrides) o.has_aspect=false; return; }
    if(auto* o=style_slot(code)) { o->aspect=index; o->has_aspect=true; }
}
void TransitionControl::store_pos(int x,int y) {
    if(pos_all) { global_px=x; global_py=y; for(auto& o:style_overrides) o.has_pos=false; return; }
    if(auto* o=style_slot(code)) { o->px=x; o->py=y; o->has_pos=true; }
}
void TransitionControl::push_style(bool persist) {
    if(adapter.wipe_tiles_supported(code))adapter.wipe_tiles(code,applied_tile_size(),persist);
    if(adapter.wipe_geometry_supported(code))adapter.wipe_geometry(code,applied_vertices(),applied_rounding(),persist);
    if(!adapter.wipe_modifiers()) return;
    if(adapter.native_wipe_modifiers()){
        if(bord_menu)native_styles|=1;
        if(aspect_menu)native_styles|=2;
        if(pos_menu)native_styles|=4;
        if(native_styles&1)adapter.prepare_wipe_modifier(168,applied_border());
        if(native_styles&2)adapter.prepare_wipe_modifier(170,applied_aspect());
        if(native_styles&4)adapter.prepare_wipe_modifier(174,uint32_t(applied_px()),uint32_t(applied_py()));
        return;
    }
    if(adapter.wipe_aspect_percentage()) {
        adapter.wipe_style(applied_multi(),applied_border(),applied_aspect(),100,uint32_t(applied_px()),uint32_t(applied_py()),pos_menu,persist);
        return;
    }
    static const uint32_t aw[]={1,4,16,3,9}, ah[]={1,3,9,4,16};
    const unsigned aspect=applied_aspect()<5?applied_aspect():0;
    adapter.wipe_style(applied_multi(),applied_border(),aw[aspect],ah[aspect],uint32_t(applied_px()),uint32_t(applied_py()),pos_menu,persist);
}
void TransitionControl::reset_style(unsigned key) {
    if(key==168) { if(adapter.state().border_profile_known&&!adapter.prepare_wipe_border_profile(0,-1,-1))panel.beep();global_border=0; bord_all=true; for(auto& o:style_overrides) o.has_border=false; bord_menu=false; }
    else if(key==170) { global_aspect=adapter.wipe_aspect_percentage()?adapter.wipe_aspect_default():0; aspect_all=true; for(auto& o:style_overrides) o.has_aspect=false; aspect_menu=false; }
    else if(key==171) { global_multi=1; multi_all=true; for(auto& o:style_overrides) o.has_multi=false; multi_menu=false; }
    else if(key==174) { global_px=global_py=500; pos_all=true; for(auto& o:style_overrides) o.has_pos=false; pos_menu=false; }
    if(adapter.native_wipe_modifiers())native_styles|=key==168?1u:key==170?2u:key==174?4u:0u;
    style_keypad=false; typing=false; invalid=false;
    push_style(true);
}
void TransitionControl::joystick(int x,int y) {
    joystick_x=x<-32767?-32767:x>32767?32767:x;
    joystick_y=y<-32767?32767:y>32767?-32767:-y;
}
void TransitionControl::position_tick(uint32_t now) {
    if(!pos_menu||!adapter.wipe_modifiers()||!adapter.state().connected){position_clock=now;position_clock_valid=false;position_remainder={};return;}
    if(!position_clock_valid){position_clock=now;position_clock_valid=true;return;}
    uint32_t elapsed=uint32_t(now-position_clock);if(elapsed<20)return;
    position_clock=now;if(elapsed>50)elapsed=50;
    int point[]={applied_px(),applied_py()};const int velocity[]={joystick_x,joystick_y};
    for(unsigned axis=0;axis<2;++axis){
        if(velocity[axis]>-2500&&velocity[axis]<2500){position_remainder[axis]=0;continue;}
        position_remainder[axis]+=int64_t(velocity[axis])*elapsed*500;
        const int delta=int(position_remainder[axis]/32767000);position_remainder[axis]%=32767000;
        point[axis]+=delta;if(point[axis]<0){point[axis]=0;position_remainder[axis]=0;}if(point[axis]>1000){point[axis]=1000;position_remainder[axis]=0;}
    }
    if(point[0]!=applied_px()||point[1]!=applied_py()){store_pos(point[0],point[1]);push_style(false);}
}
void TransitionControl::position_rotary(int32_t dx,int32_t dy) {
    if(!pos_menu||!adapter.wipe_modifiers()||(!dx&&!dy))return;
    auto bounded=[](int64_t value){return int(value<0?0:value>1000?1000:value);};
    store_pos(bounded(int64_t(applied_px())+dx),bounded(int64_t(applied_py())+dy));push_style(false);
}

void TransitionControl::dsk_shift(Changes& change,uint32_t now) {
    if(adapter.dsk_channels()<2){dsk_shifted=dsk_locked=dsk_grace=false;change.pressed.reset(119);return;}
    if(change.pressed[119]) {
        if(dsk_locked) {dsk_locked=dsk_shifted=dsk_grace=false;}
        else {dsk_shifted=true;dsk_pressed_at=now;dsk_grace=true;if(change.double_click[119])dsk_locked=true;}
    }
    if(change.released[119])dsk_shifted=dsk_locked;
    if(dsk_grace&&uint32_t(now-dsk_pressed_at)>300)dsk_grace=false;
    change.pressed.reset(119); // modifier may arrive with the DSK action in the same scan
}
void TransitionControl::modifiers(Changes& change,uint32_t now,bool allow_menu) {
    cancel_edit_for(change.pressed);
    auto others=change.pressed; others.reset(169);others.reset(173);
    if(adapter.wipe_modifiers()) { others.reset(168); others.reset(170); others.reset(171); others.reset(174); }
    if(others.any()) cancel_modifiers();
    if(change.pressed[173]) {
        soft_pending=false;soft_focus_saved=false;
        if(change.double_click[173]) {
            modify_pending=false;
            if(modify_focus_saved&&allow_menu) {
                modify_menu=modify_before.menu;soft_menu=modify_before.soft;soft_keypad=modify_before.keypad;
                typing=modify_before.typing;invalid=modify_before.invalid;value=modify_before.value;geometry_menu=modify_before.geometry;mix_params_menu=modify_before.mix_params;
            }
            modify_focus_saved=false;
            if(mix_params_target()){if(mix_choice==8){if(!adapter.prepare_dust({50,2,0}))panel.beep();}else {const bool super=mix_choice==7;if(!adapter.prepare_mix(super,super?100:0,super?100:0))panel.beep();}}
            else if(geometry_target()){reset_geometry();push_style(true);}
            else if(adapter.supports_dme_background(dme_choice)){background_arm=false;if(!adapter.set_dme_background(dme_choice,-1))panel.beep();}
            else if(has_dme_parameters()) parameters()={};
        } else {
            modify_before={modify_menu,soft_menu,soft_keypad,typing,invalid,value,geometry_menu,mix_params_menu};
            modify_focus_saved=allow_menu;modify_pending=allow_menu;modify_clicked_at=now;
        }
        change.pressed.reset(173);
    }
    if(change.pressed[169]) {
    modify_pending=false;modify_focus_saved=false;
    if(change.double_click[169]) {
        soft_pending=false;
        // Serial delivery can lag the hardware timestamp used for double-click detection.
        // Undo a single-click focus change that already ran before the second edge arrived.
        if(soft_focus_saved&&allow_menu) {
            modify_menu=soft_before_click.modify;soft_menu=soft_before_click.menu;soft_keypad=soft_before_click.keypad;
            typing=soft_before_click.typing;invalid=soft_before_click.invalid;value=soft_before_click.value;
        }
        soft_focus_saved=false;
        reset_soft();
    } else {
        soft_before_click={soft_menu,soft_keypad,typing,invalid,modify_menu,value};soft_focus_saved=allow_menu;
        soft_pending=allow_menu; soft_clicked_at=now;
    }
    change.pressed.reset(169);
    }
    if(!adapter.wipe_modifiers()) return;
    const unsigned style_ids[]={168u,170u,171u,174u};
    for(unsigned id:style_ids) if(change.pressed[id]&&adapter.wipe_modifier_supported(id)) {
        if(change.double_click[id]) {
            style_pending=false;
            if(style_focus_saved&&style_key==id) {
                soft_menu=style_before.soft; modify_menu=style_before.modify;
                bord_menu=style_before.bord; multi_menu=style_before.multi; aspect_menu=style_before.aspect; pos_menu=style_before.pos;
                style_keypad=style_before.keypad; typing=style_before.typing; invalid=style_before.invalid; value=style_before.value;
            }
            style_focus_saved=false;
            reset_style(id);
        } else {
            style_before={soft_menu,modify_menu,bord_menu,multi_menu,aspect_menu,pos_menu,style_keypad,typing,invalid,value};
            style_focus_saved=allow_menu; style_pending=allow_menu; style_key=id; style_clicked_at=now;
        }
        change.pressed.reset(id);
    }
}
void TransitionControl::advance_modifiers(uint32_t now,bool allow_menu) {
    tbar_clock=now;
    if(!allow_menu){cancel_modifiers();if(background_arm){background_arm=false;for(unsigned i=0;i<12;++i)panel.led(48+i,0);}}
    if(modify_pending&&uint32_t(now-modify_clicked_at)>300) {
        modify_pending=false;std::bitset<KeyCount> keys;keys.set(173);press(keys);
    }
    if(soft_pending&&uint32_t(now-soft_clicked_at)>300) {
        soft_pending=false;
        std::bitset<KeyCount> keys; keys.set(169); press(keys);
    }
    if(style_pending&&uint32_t(now-style_clicked_at)>300) {
        style_pending=false;
        std::bitset<KeyCount> keys; keys.set(style_key); press(keys);
    }
}
void TransitionControl::close_soft_keypad() {
    if(!soft_keypad) return;
    soft_keypad=false; typing=false; invalid=false; value=saved_value;
}
void TransitionControl::cancel_edit_for(const std::bitset<KeyCount>& keys) {
    if(keys.none()||keypad_edit_input(keys))return;
    typing=false;invalid=false;
    if(soft_keypad&&!(keys.count()==1&&((keys[160]&&soft_menu)||keys[176])))close_soft_keypad();
    if(style_keypad){const bool mixField=mix_params_menu&&(keys[160]||keys[161]||keys[162]);const bool otherField=keys[160]&&(geometry_menu||bord_menu||multi_menu||(aspect_menu&&adapter.wipe_aspect_percentage()));if(!(keys.count()==1&&(mixField||otherField||keys[176])))style_keypad=false;}
    if(soft_keypad)value=edit_soft();
    else if(rate_slot>=0)value=rates[unsigned(rate_slot)];
    else value=code;
    saved_typing=saved_invalid=false;
}
void TransitionControl::toggle_user() {
    if(user_wipe) {
        user_wipe=false;direct=false;code=preset_code;
        value=code;typing=false;invalid=false;
    } else {
        if(!user_stingers_available()){panel.beep();return;}
        user_wipe=true;direct=false;typing=false;invalid=false;
        if(adapter.fixed_stinger())stinger_choice=1;
        else if(!adapter.keypad_transition_available(TransitionType::stinger,stinger_choice))for(unsigned i=adapter.first_stinger_slot();i<10&&i<=adapter.stinger_slots();++i)if(adapter.keypad_transition_available(TransitionType::stinger,i)){stinger_choice=i;break;}
    }
}
bool TransitionControl::press(const std::bitset<KeyCount>& keys,const std::bitset<KeyCount>& doubles) {
    update_completion();
    cancel_edit_for(keys);
    if(keys.count()!=1) {
        if(adapter.supports_ftb()&&keys[115]) {
            return true;
        }
        for(unsigned id=120;id<KeyCount;id++)
            if(keys[id]&&(id<=124||id==126||(id>=128&&id<=156)||id==169||id==173||(id>=177&&id<=179)
                          ||(adapter.wipe_modifier_supported(id)&&(id==168||id==170||id==171||id==174)))) return true;
        return false;
    }
    unsigned id=0; while(!keys[id]) ++id;
    if(mix_params_menu&&((id>=160&&id<=162)||id==165||id==176)){
        if(id==176){if(style_keypad)style_keypad=false;else mix_params_menu=modify_menu=false;typing=invalid=false;return true;}
        if(id==165){mix_values=mix_dust?std::array<uint32_t,3>{50,2,0}:mix_super?std::array<uint32_t,3>{100,100,0}:std::array<uint32_t,3>{0,0,0};style_keypad=typing=invalid=false;if(!push_mix_values())panel.beep();return true;}
        unsigned index=id-160;if(index>=(mix_super?2u:3u)){panel.beep();return true;}
        bool same=style_keypad&&style_kind==240+index;style_keypad=!same;style_kind=240+index;value=mix_values[index];typing=invalid=false;return true;
    }
    if(id==173&&mix_params_target()){
        bool open=!mix_params_menu;close_soft_keypad();close_style_menus();soft_menu=false;mix_super=mix_choice==7;mix_dust=mix_choice==8;mix_params_menu=modify_menu=open;background_arm=false;read_mix_values();typing=invalid=false;return true;
    }
    if(geometry_menu&&(!geometry_target()))geometry_menu=false;
    if(geometry_menu&&(id==160||id==161||id==176)){
        typing=invalid=false;
        if(id==160){style_keypad=!style_keypad;style_kind=173;value=geometry_value();}
        else if(id==161){reset_geometry();push_style(true);}
        else if(style_keypad)style_keypad=false;
        else geometry_menu=false;
        return true;
    }
    if(id==173&&geometry_target()){
        const bool open=geometry_menu;close_soft_keypad();close_style_menus();soft_menu=modify_menu=false;geometry_menu=!open;typing=invalid=false;return true;
    }
    if(id==173) {
        // Types without a template do not open a menu or take the LCD.
        if(!has_dme_parameters()) return true;
        if(doubles[id]) {background_arm=false;if(adapter.supports_dme_background(dme_choice)){if(!adapter.set_dme_background(dme_choice,-1))panel.beep();}else parameters()={};return true;}
        close_soft_keypad();soft_menu=false;modify_menu=!modify_menu;background_arm=false;for(unsigned i=0;i<12;++i)panel.led(48+i,0);return true;
    }
    if(modify_menu&&adapter.supports_dme_background(dme_choice)&&id>=160&&id<=165){
        if(id==160||id==162){bool color=id==162;if(color&&adapter.state().color_sources.none()){panel.beep();return true;}background_arm=!background_arm||background_color!=color;background_color=color;if(!background_arm)for(unsigned i=0;i<12;++i)panel.led(48+i,0);}
        else if(id>=163&&adapter.state().dme_background_scopes_supported){background_arm=false;for(unsigned i=0;i<12;++i)panel.led(48+i,0);if(!adapter.set_dme_background_scope(dme_choice,id!=163,id==165))panel.beep();}
        else if(id==161){background_arm=false;for(unsigned i=0;i<12;++i)panel.led(48+i,0);if(!adapter.set_dme_background(dme_choice,-1))panel.beep();}
        return true;
    }
    if(modify_menu&&id>=160&&id<=165) {
        if(has_dme_parameters()) {
            if(id<=163)parameters().entry=id-160;
            if(id==165&&dme_choice==2)parameters().inward=!parameters().inward;
        }
        return true;
    }
    if(id==176&&modify_menu) {modify_menu=background_arm=false;for(unsigned i=0;i<12;++i)panel.led(48+i,0);return true;}
    if(id>=177&&id<=179) {
        if(id==178) {
            if(direction==1) direction=next_reverse?2:0;
            else { direction=1; next_reverse=false; }
        } else if(direction==1) next_reverse=id==177;
        else direction=id==177?2:0;
        // An explicit choice also overrides the next sense during an active take.
        pending_alternate=false; return true;
    }
    if(id==115) {
        if(adapter.supports_ftb()) adapter.fade_to_black(rates[2]);
        return adapter.supports_ftb();
    }
    if(id>=120&&id<=122) {
        if(id==122&&!adapter.supports_dme()) return true;
        if(adapter.supports_transition_preview()) {
            const auto live=adapter.state();
            if(live.manual_transition&&live.busy) return true;
            if(!adapter.set_transition_preview(doubles[id])&&doubles[id])panel.beep();
        } else if(doubles[id]) {panel.beep();return true;}
        close_soft_keypad();
        close_style_menus();
        selected=TransitionType(id-120);
        if(id==121&&!code&&!user_wipe)code=preset_code=presets[0];
        dme_menu=id==122;mix_menu=id==120&&mix_slots();
        if(id==120&&!mix_available(mix_choice))mix_choice=1;
        if(id==122&&!dme_direct){bool valid=false;for(unsigned i=0;i<10;++i)valid|=dme_shortcut_available(i)&&dme_shortcut(i)==dme_choice;if(!valid)for(unsigned i=0;i<10;++i)if(dme_shortcut_available(i)){dme_choice=dme_shortcut_choice=dme_shortcut(i);break;}}
        // Type selection takes precedence over the current keypad editor.
        rate_slot=-1; soft_menu=false; modify_menu=false; wipe_menu=id!=120;
        typing=false; invalid=false; return true;
    }
    if(mix_menu&&id==154){panel.beep();return true;}
    if(id==123||id==124) { adapter.dsk(id==123,rates[1],dsk_shifted?1:0); return true; }
    if(id==126) {
        const auto active=type();
        if(adapter.live_transition_controls()&&adapter.state().transitioning){adapter.automatic(active,rates[0]);return true;}
        const bool wipe=(active==TransitionType::dme&&adapter.keypad_transition_slots(TransitionType::dme))||active==TransitionType::wipe||active==TransitionType::stinger||(active==TransitionType::dme&&adapter.dme_keypad_grid());
        if(active==TransitionType::slide||active==TransitionType::swipe)adapter.dme_parameters(parameters());
        if(active==TransitionType::wipe&&adapter.wipe_modifiers())push_style(false);
        const auto before=adapter.state().completed_auto;
        if(adapter.automatic(active,rates[0],active==TransitionType::mix&&adapter.keypad_transition_slots(TransitionType::mix)?mix_choice:active==TransitionType::dme?dme_choice:active==TransitionType::wipe?code:active==TransitionType::stinger?stinger_choice:0,wipe&&(direction==2||(direction==1&&next_reverse)),applied_soft())) {
            completion=before; pending_alternate=wipe&&direction==1;
        } else panel.beep();
        return true;
    }
    if(id==169) {
        if(doubles[id]) {
            reset_soft();
            return true;
        }
        close_soft_keypad(); modify_menu=false; close_style_menus(); typing=false; invalid=false; soft_menu=!soft_menu; return true;
    }
    if(soft_menu&&id==160) {
        if(soft_keypad) { close_soft_keypad(); return true; }
        if(!soft_keypad) { saved_typing=typing; saved_invalid=invalid; saved_value=value; }
        soft_keypad=true; value=edit_soft(); typing=false; invalid=false; return true;
    }
    if(soft_menu&&id==161) {
        typing=false; invalid=false; soft_all=!soft_all;
        if(soft_keypad) value=edit_soft();
        return true;
    }
    if(soft_menu&&id==162) {
        typing=false; invalid=false;
        set_soft(code,softness);
        soft_all=false;
        if(soft_keypad) value=edit_soft();
        return true;
    }
    if(id==128) {
        geometry_menu=false;
        close_soft_keypad();
        soft_menu=false;modify_menu=false;
        rate_slot=rate_slot<0?last_rate_slot:(rate_slot+1)%3;
        last_rate_slot=rate_slot;
        wipe_menu=mix_menu=false; typing=false; invalid=false; return true;
    }
    if(id==136) {
        // Keypad prepares a wipe or DME. It must not arm Transition Type:
        // that key is what can reach program.
        close_soft_keypad();soft_menu=false;modify_menu=false;close_style_menus();mix_menu=false;wipe_menu=true;dme_menu=doubles[id]&&adapter.supports_dme();rate_slot=-1;
        typing=false;invalid=false;return true;
    }
    if(bord_menu&&id==163&&adapter.state().border_profile_known){
        auto s=adapter.state();int next=s.border_side==0?-1:s.border_side==-1?1:0;
        style_keypad=typing=invalid=false;if(!adapter.prepare_wipe_border_profile(next,s.border_inner_soft,s.border_outer_soft))panel.beep();return true;
    }
    const bool style_open=bord_menu||multi_menu||aspect_menu||pos_menu||(mix_params_menu&&style_keypad);
    if(!soft_keypad&&!style_open&&wipe_menu&&!dme_menu&&id==155) {
        if(direct) {
            direct=false; user_wipe=false; code=preset_code;
        } else {
            user_wipe=false; direct=true; code=direct_code;
        }
        typing=false; invalid=false; value=code; return true;
    }
    if(!soft_keypad&&!style_open&&wipe_menu&&!dme_menu&&id==154) { toggle_user(); return true; }
    if(!soft_keypad&&!style_open&&wipe_menu&&!dme_menu&&id==160) { code=preset_code=presets[0];user_wipe=false;direct=false;invalid=false;typing=false;return true; }
    if(!soft_keypad&&!style_open&&wipe_menu&&!dme_menu&&id==165) { if(!user_wipe)toggle_user();return true; }
    if(adapter.wipe_modifier_supported(id)&&(id==168||id==170||id==171||id==174)) {
        const bool open=(id==168&&bord_menu)||(id==170&&aspect_menu)||(id==171&&multi_menu)||(id==174&&pos_menu);
        close_soft_keypad(); soft_menu=modify_menu=false; style_keypad=false; typing=false; invalid=false;
        if(open) { bord_menu=multi_menu=aspect_menu=pos_menu=false; push_style(true); return true; }
        bord_menu=id==168; aspect_menu=id==170; multi_menu=id==171; pos_menu=id==174;
        push_style(false); return true;
    }
    if((bord_menu||multi_menu||aspect_menu||pos_menu)&&id==160) {
        typing=false; invalid=false;
        if(multi_menu) {
            store_multi(doubles[id]?1u:stepped_multi(applied_multi(),1,true));
            push_style(false); return true;
        }
        if(bord_menu) {
            if(style_keypad) { style_keypad=false; return true; }
            style_keypad=true; style_kind=168u;
            value=applied_border(); return true;
        }
        if(aspect_menu&&adapter.wipe_aspect_percentage()){if(style_keypad){style_keypad=false;return true;}style_keypad=true;style_kind=170;value=applied_aspect();return true;}
        if(aspect_menu) { store_aspect((applied_aspect()+1)%5); push_style(false); return true; }
        store_pos(500,500); push_style(false); return true;
    }
    if((bord_menu||multi_menu||aspect_menu||pos_menu)&&id==161) {
        if(bord_menu) bord_all=!bord_all;
        else if(multi_menu) multi_all=!multi_all;
        else if(aspect_menu) aspect_all=!aspect_all;
        else pos_all=!pos_all;
        if(style_keypad) value=bord_menu?applied_border():aspect_menu?applied_aspect():applied_multi();
        push_style(false); return true;
    }
    if((bord_menu||multi_menu||aspect_menu||pos_menu)&&id==162) {
        if(bord_menu) { bord_all=false; store_border(global_border); }
        else if(multi_menu) { multi_all=false; store_multi(global_multi); }
        else if(aspect_menu) { aspect_all=false; store_aspect(global_aspect); }
        else { pos_all=false; store_pos(global_px,global_py); }
        push_style(true); return true;
    }
    if(id==176&&style_keypad) { style_keypad=false; typing=false; invalid=false; return true; }
    if(id==176&&(bord_menu||multi_menu||aspect_menu||pos_menu)) {
        bord_menu=multi_menu=aspect_menu=pos_menu=false; push_style(true); return true;
    }
    if(id==176&&soft_keypad) { close_soft_keypad(); return true; }
    if(id==176&&soft_menu) { soft_menu=false; return true; }
    if(id==176&&(rate_slot>=0||wipe_menu||mix_menu||soft_menu||invalid)) {
        rate_slot=-1; wipe_menu=mix_menu=false; soft_menu=false; typing=false; invalid=false; return true;
    }
    constexpr unsigned digit_ids[]={153,145,146,147,137,138,139,129,130,131};
    if(!soft_keypad&&!style_open&&mix_menu&&mix_slots()) {
        for(unsigned n=0;n<10;n++)if(id==digit_ids[n]) {if(mix_available(n))mix_choice=n;else panel.beep();typing=invalid=false;return true;}
    }
    if(!soft_keypad&&!style_open&&wipe_menu&&dme_menu&&id==155) {
        if(!adapter.valid_dme_code(dme_direct_code)){auto fallback=adapter.first_direct_dme_code();if(!adapter.valid_dme_code(fallback)){panel.beep();return true;}dme_direct_code=fallback;}
        dme_direct=!dme_direct;
        if(dme_direct){dme_shortcut_choice=dme_choice;dme_choice=dme_direct_code;}
        else dme_choice=dme_shortcut_choice;
        typing=invalid=false;value=dme_choice;return true;
    }
    if(!soft_keypad&&!style_open&&wipe_menu&&dme_menu&&id==154){panel.beep();return true;}
    if(!soft_keypad&&!style_open&&wipe_menu&&dme_menu&&!dme_direct){
        for(unsigned n=0;n<10;++n)if(id==digit_ids[n]){if(!dme_shortcut_available(n)){panel.beep();return true;}dme_choice=dme_shortcut_choice=dme_shortcut(n);if(!has_dme_parameters())modify_menu=false;typing=invalid=false;return true;}
        if(id>=128&&id<=156)return true;
    }
    if(!soft_keypad&&!style_open&&wipe_menu&&user_wipe) {
        for(unsigned n=0;n<10;n++)if(id==digit_ids[n]) {if(adapter.fixed_stinger())return true;if(n<adapter.first_stinger_slot()||n>adapter.stinger_slots()||!adapter.keypad_transition_available(TransitionType::stinger,n)){panel.beep();return true;}stinger_choice=n;typing=false;invalid=false;return true;}
    }
    if(!soft_keypad&&!style_open&&wipe_menu&&!dme_menu&&!direct&&!user_wipe) for(unsigned n=0;n<10;n++) if(id==digit_ids[n]) {
        if(!adapter.valid_wipe_code(presets[n])){panel.beep();return true;}
        code=preset_code=presets[n];
        user_wipe=false; typing=false; invalid=false; push_style(false); return true;
    }
    if(!soft_keypad&&!style_keypad&&rate_slot<0&&!(wipe_menu&&(dme_menu?dme_direct:direct))) return false;
    if(id==140) { value=0; typing=true; invalid=false; return true; }
    if(id==156) {
        if(typing) {
            if(style_keypad) {
                if(mix_params_menu&&style_kind>=240&&style_kind<=242){if(value>((mix_super||mix_dust)?100u:255u)||(mix_dust&&style_kind==241&&value<1)){invalid=true;panel.beep();return true;}auto old=mix_values;mix_values[style_kind-240]=value;if(!push_mix_values()){mix_values=old;invalid=true;panel.beep();return true;}style_keypad=typing=invalid=false;return true;}
                if(style_kind==173){if(value<geometry_minimum()||value>geometry_maximum()){invalid=true;panel.beep();return true;}store_geometry_value(value);style_keypad=false;typing=invalid=false;push_style(true);return true;}
                if(style_kind==168) {
                    if(value>adapter.wipe_border_maximum()) { invalid=true; panel.beep(); return true; }
                    store_border(value);
                } else if(style_kind==170&&adapter.wipe_aspect_percentage()){
                    if(value<adapter.wipe_aspect_minimum()||value>adapter.wipe_aspect_maximum()){invalid=true;panel.beep();return true;}store_aspect(value);
                } else if(value!=1&&value!=2&&value!=4&&value!=9&&value!=16) { invalid=true; panel.beep(); return true; }
                else store_multi(value);
                style_keypad=false; typing=invalid=false; push_style(true); return true;
            } else if(soft_keypad) {
                if(value>100) { invalid=true; panel.beep(); return true; }
                if(soft_all) { softness=value; if(soft_all) clear_soft_overrides(); }
                else set_soft(code,value);
            } else if(rate_slot>=0) {
                const bool frames=adapter.frame_rates();
                if(value<(frames?1u:50u)||value>adapter.maximum_rate()) { invalid=true; panel.beep(); return true; }
                if(frames&&rate_slot==1&&adapter.state().dsk_frames[dsk_slot()]&&!adapter.set_dsk_rate(value,dsk_slot())) { invalid=true; panel.beep(); return true; }
                if(frames&&rate_slot==2&&adapter.state().ftb_frames&&!adapter.set_ftb_rate(value)) { invalid=true; panel.beep(); return true; }
                if(frames&&rate_slot==0&&!adapter.set_transition_rate(type(),value)) {invalid=true;panel.beep();return true;}
                rates[unsigned(rate_slot)]=value;
            } else if(dme_menu&&dme_direct) { if(!adapter.valid_dme_code(value)){invalid=true;panel.beep();return true;}dme_choice=dme_direct_code=value;
            } else { if(!value||!adapter.valid_wipe_code(value)) { invalid=true; panel.beep(); return true; } code=direct_code=value;push_style(false); }
        }
        if(soft_full&&invalid)return true;
        if(soft_keypad) { close_soft_keypad(); return true; }
        typing=false; invalid=false; return true;
    }
    constexpr unsigned digits[]={153,145,146,147,137,138,139,129,130,131};
    for(unsigned n=0;n<10;n++) if(id==digits[n]) {
        if(!typing||invalid) value=0;
        if(value>99999) { invalid=true; panel.beep(); return true; }
        value=value*10+n; typing=true; invalid=false; return true;
    }
    return false;
}
void TransitionControl::rotary(int32_t delta) {
    if(mix_params_menu)return;
    if(!delta) return;
    if(geometry_menu){unsigned low=geometry_minimum(),high=geometry_maximum();int64_t next=int64_t(style_keypad?value:geometry_value())+delta;unsigned applied=unsigned(next<int64_t(low)?low:next>int64_t(high)?high:next);
        if(style_keypad){value=applied;typing=true;invalid=false;return;}
        store_geometry_value(applied);push_style(false);return;}

    if(adapter.wipe_modifiers()&&(bord_menu||multi_menu||aspect_menu)) {
        if(bord_menu) {
            int64_t next=int64_t(applied_border())+delta;
            store_border(unsigned(next<0?0:next>adapter.wipe_border_maximum()?adapter.wipe_border_maximum():next));
        } else if(multi_menu) {
            store_multi(stepped_multi(applied_multi(),delta>0?1:-1,false));
        } else {
            int64_t next=int64_t(applied_aspect())+(adapter.wipe_aspect_percentage()?delta:(delta>0?1:-1));
            const int minimum=adapter.wipe_aspect_percentage()?int(adapter.wipe_aspect_minimum()):0;
            if(next<minimum) next=minimum;
            const int maximum=adapter.wipe_aspect_percentage()?int(adapter.wipe_aspect_maximum()):4;
            if(next>maximum) next=maximum;
            store_aspect(unsigned(next));
        }
        if(style_keypad) { value=bord_menu?applied_border():aspect_menu?applied_aspect():applied_multi(); typing=false; invalid=false; }
        push_style(false); return;
    }
    if(!soft_menu) return;
    const unsigned current=edit_soft();
    int64_t next=int64_t(current)+delta;
    const unsigned applied=unsigned(next<0?0:next>100?100:next);
    if(soft_all) { softness=applied; if(soft_all) clear_soft_overrides(); }
    else set_soft(code,applied);
    if(soft_keypad) { value=applied; typing=false; invalid=false; }
}
void TransitionControl::tbar(uint16_t position) {
    if(position>4095) return;
    // Finish within 0.5% of either calibrated endpoint. Keep the middle unchanged.
    constexpr uint16_t endpoint_margin=20,release_margin=32;
    if(position<=endpoint_margin)position=0;
    else if(position>=4095-endpoint_margin)position=4095;
    const auto state=adapter.state();
    if(!state.connected||!state.studio) {reset_tbar();return;}
    if(adapter.manual_me_pickup()) recall_me(state.me);
    const int previous=tbar_physical;
    tbar_physical=position;
    if(tbar_pickup) {
        const bool crossed=tbar_frozen&&tbar_target>=0&&previous>=0
            &&((previous<=tbar_target&&position>=tbar_target)||(previous>=tbar_target&&position<=tbar_target));
        if(crossed) {
            tbar_pickup=tbar_frozen=false;tbar_active=true;
        } else if(position==0||position==4095) {
            if(tbar_frozen) {
                if(adapter.manual(tbar_home==int(position)?0:4095,type(),code,false,applied_soft())) {
                    tbar_active=tbar_finishing=true;tbar_endpoint=position;
                    tbar_pickup=tbar_frozen=false;
                }
            } else {tbar_home=position;tbar_pickup=false;}
            return;
        } else return;
    }
    if(tbar_active) {
        if(!state.busy) {
            // The next sample can already be on the return stroke. Keep the
            // endpoint we actually sent, rather than requiring another sample
            // at that endpoint after OBS finishes its asynchronous completion.
            tbar_home=tbar_finishing?tbar_endpoint:((position==0||position==4095)?int(position):-1);
            tbar_active=tbar_finishing=false;
        } else {
        if(tbar_finishing) return;
        const uint16_t progress=tbar_home==0?position:4095-position;
        adapter.manual(progress,type(),type()==TransitionType::mix&&adapter.keypad_transition_slots(TransitionType::mix)?mix_choice:type()==TransitionType::dme?dme_choice:code,false,applied_soft());
        if(progress==0||progress==4095) {tbar_finishing=true;tbar_endpoint=position;}
        return;
        }
    }
    if(state.busy||state.transitioning) {tbar_home=-1;return;}
    if(tbar_home<0) {if(position==0||position==4095)tbar_home=position;return;}
    // Require 0.8% departure after completion/acquisition so end-stop jitter
    // cannot immediately start a second transition on the return stroke.
    if((tbar_home==0&&position<=release_margin)||(tbar_home==4095&&position>=4095-release_margin))return;
    const auto active=type();
    const bool wipe=(active==TransitionType::dme&&adapter.keypad_transition_slots(TransitionType::dme))||active==TransitionType::wipe||(active==TransitionType::dme&&adapter.dme_keypad_grid());
    if(active==TransitionType::wipe&&adapter.wipe_modifiers())push_style(false);
    if(adapter.manual(tbar_home==0?position:4095-position,active,active==TransitionType::mix&&adapter.keypad_transition_slots(TransitionType::mix)?mix_choice:active==TransitionType::dme?dme_choice:wipe?code:0,
                      wipe&&(direction==2||(direction==1&&next_reverse)),applied_soft())) {
        tbar_active=true;tbar_finishing=position==0||position==4095;
        if(tbar_finishing)tbar_endpoint=position;
        completion=state.completed_auto;pending_alternate=wipe&&direction==1;
    } else tbar_home=-1;
}

void TransitionControl::update_completion() {
    const auto state=adapter.state();
    if(pending_alternate) {
        if(state.completed_auto!=completion) { next_reverse=!next_reverse; pending_alternate=false; }
        else if(!state.busy) pending_alternate=false;
    }
}
void TransitionControl::refresh(bool display,bool upper_bank) {
    if(geometry_menu&&!geometry_target())geometry_menu=false;
    if(mix_params_menu&&!mix_params_target())mix_params_menu=modify_menu=style_keypad=false;
    if(mix_params_menu&&!typing&&adapter.state().mix_preparation_revision!=mix_revision)read_mix_values();
    if(adapter.me_delegation()) recall_me(int(adapter.state().me));
    update_completion();
    if(adapter.frame_rates()) {
        const auto live=adapter.state();
        if(live.ftb_frames&&ftb_rate_seen!=live.ftb_rate_revision&&!(typing&&rate_slot==2)) {
            rates[2]=live.ftb_frames;ftb_rate_seen=live.ftb_rate_revision;
        }
        const auto slot=dsk_slot();
        if(live.dsk_frames[slot]&&(dsk_rate_slot_seen!=slot||dsk_rate_seen!=live.dsk_rate_revision[slot])&&!(typing&&rate_slot==1)) {
            rates[1]=live.dsk_frames[slot];dsk_rate_slot_seen=slot;dsk_rate_seen=live.dsk_rate_revision[slot];
        }
        const unsigned kind=type()==TransitionType::wipe&&live.wipe_rate_known?1u:type()==TransitionType::dme&&live.dme_rate_known?2u:type()==TransitionType::dip&&live.dip_rate_known?3u:0u;
        const bool known=kind==1?live.wipe_rate_known:kind==2?live.dme_rate_known:kind==3?live.dip_rate_known:live.rate_known;
        const auto revision=kind==1?live.wipe_rate_revision:kind==2?live.dme_rate_revision:kind==3?live.dip_rate_revision:live.rate_revision;
        if(known&&(revision!=rate_seen||kind!=rate_kind)&&!(typing&&rate_slot==0)) {
            rates[0]=kind==1?live.wipe_frames:kind==2?live.dme_frames:kind==3?live.dip_frames:live.auto_frames;rate_seen=revision;rate_kind=kind;
        }
    }
    for(unsigned i=0;i<presets.size();++i) {
        const uint32_t next=adapter.wipe_preset(i);
        if(next==presets[i]) continue;
        if(!direct&&!user_wipe&&code==presets[i]) code=preset_code=next;
        presets[i]=next;
    }
    if(adapter.state().transition_preview && type()==TransitionType::wipe)
        adapter.prepare_wipe_modifier(169,applied_soft());
    const bool reverse=direction==2||(direction==1&&next_reverse);
    panel.led(179,reverse?0:2); panel.led(178,direction==1?2:0); panel.led(177,reverse?2:0);
    const auto preview_state=adapter.state();
    const unsigned preview_blink=preview_state.connected&&preview_state.transition_preview_known&&preview_state.transition_preview?500:0;
    panel.led(120,selected==TransitionType::mix?2:0,preview_blink);
    panel.led(121,selected==TransitionType::wipe||selected==TransitionType::stinger?2:0,preview_blink);
    if(!adapter.supports_dme()) dme_menu=false;
    panel.led(122,adapter.supports_dme()&&is_dme(selected)?2:0,preview_blink);
    const auto state=adapter.state();
    if(adapter.manual_me_pickup()) {
        const bool waiting=tbar_pickup||tbar_home<0;
        panel.set_tbar_indicators(waiting?((tbar_clock/250)%2?0x30:0):tbar_home==0?0x10:0x20);
    }
    panel.led(126,state.transitioning&&!state.manual_transition&&!state.ftb?2:0);
    panel.led(115,adapter.supports_ftb()&&state.connected&&state.ftb?2:0);
    const bool known=dsk_shifted?state.dsk2_known:state.dsk_known;
    const bool on=dsk_shifted?state.dsk2_on:state.dsk_on;
    const bool mixing=dsk_shifted?state.dsk2_mixing:state.dsk_mixing;
    panel.led(119,dsk_shifted||dsk_grace?2:state.connected&&state.dsk2_known&&state.dsk2_on?1:0);
    panel.led(123,state.connected&&mixing?2:0);
    panel.led(124,state.connected&&known&&on?2:0);
    // WIPE modifiers: HIGH while editing, LOW while a non-neutral value remains.
    if(modify_menu&&!mix_params_menu&&!has_dme_parameters()) modify_menu=false;
    panel.led(169,soft_menu&&display?2:applied_soft()?1:0);
    if(adapter.wipe_modifiers()) {
        const bool moved=applied_px()!=500||applied_py()!=500;
        const auto borderState=adapter.state();const bool borderModified=applied_border()||(borderState.border_profile_known&&(borderState.border_side!=0||borderState.border_inner_soft!=-1||borderState.border_outer_soft!=-1));
        panel.led(168,bord_menu&&display?2:borderModified?1:0);
        panel.led(170,adapter.wipe_modifier_supported(170)?(aspect_menu&&display?2:applied_aspect()!=(adapter.wipe_aspect_percentage()?adapter.wipe_aspect_default():0u)?1:0):0);
        panel.led(171,adapter.wipe_modifier_supported(171)?(multi_menu&&display?2:applied_multi()!=1u?1:0):0);
        panel.led(174,pos_menu&&display?2:moved?1:0);
    } else { panel.led(168,0); panel.led(170,0); panel.led(171,0); panel.led(174,0); }
    panel.led(173,geometry_menu&&display?2:geometry_target()&&(geometry_value()!=geometry_default())?1:modify_menu&&display?2:(mix_params_target()?mix_params_modified():has_dme_parameters()&&dme_modified())?1:0);
    if(state.command_failures>background_failures&&modify_menu){if(mix_params_menu)read_mix_values();panel.beep();}
    background_failures=state.command_failures;
    if(modify_menu&&background_arm&&adapter.supports_dme_background(dme_choice)){int chosen=adapter.dme_background(dme_choice);for(unsigned i=0;i<12;++i)panel.led(48+i,0);for(unsigned i=0;i<12;++i){unsigned source=i+12*(upper_bank?1:0);bool available=i==11?!background_color&&state.me+1<int(state.me_count):state.available[source]&&(!background_color||state.color_sources[source]);bool selected=chosen>=1000?i==11&&chosen==1000+state.me+1:chosen>=0&&unsigned(chosen)%12==i;panel.led(48+i,available||selected?1:0,selected?500:0);}}

    panel.led(128,rate_slot>=0?2:0); panel.led(136,wipe_menu?2:0,wipe_menu&&dme_menu?500:0);
    constexpr unsigned digit_ids[]={153,145,146,147,137,138,139,129,130,131};
    const bool style_open=bord_menu||multi_menu||aspect_menu||pos_menu||(mix_params_menu&&style_keypad);
    for(unsigned i=0;i<10;i++) {
        bool available=false,chosen=false;
        if(!style_open&&mix_menu&&mix_slots()) {
            available=mix_available(i);
            chosen=i==mix_choice;
        } else if(!style_open&&wipe_menu) {
            if(dme_menu&&!dme_direct) {
                available=dme_shortcut_available(i);chosen=i==dme_shortcut_digit();
            } else if(!dme_menu&&user_wipe) {
                available=i>=adapter.first_stinger_slot()&&i<=adapter.stinger_slots()&&adapter.keypad_transition_available(TransitionType::stinger,i);
                chosen=i==stinger_choice;
            } else if(!dme_menu&&!direct) {
                const auto preset=presets[i];available=adapter.valid_wipe_code(preset);chosen=code==preset;
            }
        }
        panel.led(digit_ids[i],available?(chosen?2:1):0);
    }
    panel.led(155,!style_open&&wipe_menu?(dme_menu?(adapter.valid_dme_code(adapter.first_direct_dme_code())?(dme_direct?2:1):0):direct?2:1):0); panel.led(154,!style_open&&wipe_menu&&!dme_menu&&user_stingers_available()?(user_wipe?2:1):0);
    if(soft_keypad||style_keypad) panel.clear_keypad_lamps();
    const bool draft=typing&&(soft_keypad||style_keypad||rate_slot>=0||(wipe_menu&&(dme_menu?dme_direct:direct)));
    panel.led(156,draft?1:0,draft?500:0);
    uint8_t digits[7]={15,15,15,15,15,15,0};
    if(soft_keypad||style_keypad||style_open||rate_slot>=0||wipe_menu||(mix_menu&&mix_slots())) {
        unsigned number=style_keypad||soft_keypad?value:style_open?(bord_menu?applied_border():multi_menu?applied_multi():aspect_menu?applied_aspect():0u):typing?value:rate_slot>=0?rates[unsigned(rate_slot)]:mix_menu?mix_choice:dme_menu?(dme_direct?dme_choice:dme_shortcut_digit()):user_wipe?stinger_choice:code;
        // Physical panel: native digit 0 is the rightmost position (units).
        for(unsigned i=0;i<6;i++) { digits[i]=uint8_t(number%10); number/=10; if(!number) break; }
        if(!soft_keypad&&rate_slot>=0) digits[6]=uint8_t(1u<<unsigned(rate_slot));
    }
    panel.digits(digits);
    if(!display) return;
    for(unsigned i=160;i<=165;i++)panel.led(i,0);
    if(mix_params_menu){SoftMenu menu;char title[41];if(mix_dust)std::snprintf(title,sizeof title,"DUST MIX PARAMETERS");else if(mix_super)std::snprintf(title,sizeof title,"SUPER MIX GAINS");else std::snprintf(title,sizeof title,"DIP COLOR #%02X%02X%02X",mix_values[0],mix_values[1],mix_values[2]);menu.title(title);
        for(unsigned i=0;i<(mix_super?2u:3u);++i){char text[8];std::snprintf(text,sizeof text,"%c:%u",mix_dust?(i==0?'R':i==1?'S':'F'):mix_super?(i?'B':'A'):i==0?'R':i==1?'G':'B',mix_values[i]);menu.field(i,text);}
        menu.field(5,"RESET");menu.show(panel,style_keypad?int(style_kind-240):-1);SoftMenu::navigation(panel,true);
    } else if(geometry_menu){SoftMenu menu;char title[41],text[12];std::snprintf(title,sizeof title,mosaic_target()?"WIPE BLOCK SIZE %u":code==49?"WIPE POLYGON %u":"WIPE CORNERS %u",code);std::snprintf(text,sizeof text,mosaic_target()?"%u%%":"%u",geometry_value());menu.title(title);menu.field(0,text);menu.field(1,"RESET");menu.show(panel,style_keypad?0:-1);SoftMenu::navigation(panel,true);
    } else if(modify_menu&&adapter.supports_dme_background(dme_choice)){SoftMenu menu;auto state=adapter.state();const int current=adapter.dme_background(dme_choice);char title[41];auto name=adapter.keypad_transition_label(TransitionType::dme,dme_choice);if(current==-3)std::snprintf(title,sizeof title,"%s BKGD STATIC IMAGE",name.c_str());else if(current<0)std::snprintf(title,sizeof title,"%s BKGD BLACK",name.c_str());else if(current>=1000)std::snprintf(title,sizeof title,"%s BKGD M/E %d",name.c_str(),current-999);else std::snprintf(title,sizeof title,"%s BKGD INPUT %d",name.c_str(),current+1);menu.title(title);menu.field(0,"AUX");menu.field(1,"BLACK");menu.field(2,"COLOR");if(state.dme_background_scopes_supported){const bool custom=adapter.dme_background_custom(dme_choice);menu.field(3,"GLOBAL");menu.field(4,"CUSTOM");menu.field(5,"RSTGL");menu.title((std::string("DME BKGD ")+(custom?"CUSTOM ":"GLOBAL ")+(current==-3?"IMAGE":current==-1?"BLACK":current>=1000?"M/E "+std::to_string(current-999):"INPUT "+std::to_string(current+1))).c_str());}menu.show(panel);if(state.dme_background_scopes_supported){const bool custom=adapter.dme_background_custom(dme_choice);panel.led(163,custom?1:2);panel.led(164,custom?2:1);panel.led(165,1);}panel.led(160,background_arm&&!background_color?2:1);panel.led(161,current==-1?2:1);panel.led(162,state.color_sources.any()?(background_arm&&background_color?2:1):0);SoftMenu::navigation(panel,true);
    } else if(modify_menu) {
        SoftMenu menu;
        menu.title(dme_choice==1?"SLIDE ENTRY POINT":"SWIPE ENTRY POINT");
        menu.field(0,"LEFT");menu.field(1,"RIGHT");menu.field(2,"TOP");menu.field(3,"BOTTOM");
        if(dme_choice==2)menu.field(5,parameters().inward?"IN:ON":"IN:OFF");
        menu.show(panel,int(parameters().entry));
        if(dme_choice==2)panel.led(165,parameters().inward?2:0);
    } else if(bord_menu||multi_menu||aspect_menu||pos_menu) {
        SoftMenu menu;
        char title[41],identity[16];wipe_identity(identity,sizeof identity);
        static const char* aspects[]={"1:1","4:3","16:9","3:4","9:16"};
        const bool custom=bord_menu?!bord_all:multi_menu?!multi_all:aspect_menu?!aspect_all:!pos_all;
        if(style_full) std::snprintf(title,sizeof title,"WIPE: 32 LOCAL SETTINGS MAX");
        else if(style_keypad&&invalid&&aspect_menu&&adapter.wipe_aspect_percentage())
            std::snprintf(title,sizeof title,"RANGE %u..%u / TYPE TO REPLACE",adapter.wipe_aspect_minimum(),adapter.wipe_aspect_maximum());
        else if(style_keypad&&invalid) std::snprintf(title,sizeof title,bord_menu?(adapter.native_wipe_modifiers()?"RANGE 0..100 / TYPE TO REPLACE":"RANGE 0..40 / TYPE TO REPLACE"):aspect_menu&&adapter.wipe_aspect_percentage()?"RANGE 0..100 / TYPE TO REPLACE":"MULTI 1 2 4 9 16");
        else if(pos_menu) std::snprintf(title,sizeof title,"WIPE POS %s  X%+d Y%+d",custom?"CUSTOM":"GLOBAL",(applied_px()-500)/10,(applied_py()-500)/10);
        else if(bord_menu) std::snprintf(title,sizeof title,custom?"WIPE BORD %s CUSTOM":"WIPE BORD GLOBAL",identity);
        else if(multi_menu) std::snprintf(title,sizeof title,custom?"WIPE MULTI %u CUSTOM":"WIPE MULTI GLOBAL",code);
        else if(adapter.wipe_aspect_percentage())std::snprintf(title,sizeof title,custom?"WIPE ASPECT %s CUSTOM":"WIPE ASPECT GLOBAL",identity);
        else std::snprintf(title,sizeof title,custom?"WIPE ASPCT %u CUSTOM":"WIPE ASPCT GLOBAL",code);
        menu.title(title);
        char text[8];
        if(bord_menu) std::snprintf(text,sizeof text,"%u",applied_border());
        else if(multi_menu) std::snprintf(text,sizeof text,"%u",applied_multi());
        else if(aspect_menu&&adapter.wipe_aspect_percentage())std::snprintf(text,sizeof text,"%u%%",applied_aspect());
        else if(aspect_menu) std::snprintf(text,sizeof text,"%s",aspects[applied_aspect()<5?applied_aspect():0]);
        else std::snprintf(text,sizeof text,"CENTER");
        menu.field(0,text);
        menu.field(1,custom?"CUSTOM":"GLOBAL");
        menu.field(2,"RSTGL");
        if(bord_menu&&adapter.state().border_profile_known){int side=adapter.state().border_side;menu.field(3,side<0?"INNER":side>0?"OUTER":"CENTER");}
        menu.show(panel,style_keypad?0:-1);
        panel.led(161,custom?1:2);
        panel.led(162,1);if(bord_menu&&adapter.state().border_profile_known)panel.led(163,1);
    } else if(soft_menu) {
        SoftMenu menu;
        char title[41],identity[16];wipe_identity(identity,sizeof identity);
        if(soft_full) std::snprintf(title,sizeof title,"SOFT: 32 LOCAL SETTINGS MAX");
        else if(soft_keypad&&invalid) std::snprintf(title,sizeof title,"RANGE 0..100 / TYPE TO REPLACE");
        else if(soft_all) std::snprintf(title,sizeof title,"WIPE SOFT GLOBAL");
        else std::snprintf(title,sizeof title,"WIPE SOFT %s CUSTOM",identity);
        menu.title(title);
        char text[8]; std::snprintf(text,sizeof text,"%u",edit_soft()); menu.field(0,text);
        menu.field(1,soft_all?"GLOBAL":"CUSTOM");
        menu.field(2,"RSTGL");
        menu.show(panel,soft_keypad?0:-1);
        panel.led(161,soft_all?2:1);
        panel.led(162,1);
    } else if(mix_menu&&mix_slots()) {
        panel.lcd(0,"MIX SELECT");panel.lcd(1,(std::to_string(mix_choice)+" "+adapter.keypad_transition_label(TransitionType::mix,mix_choice)+" | KEYPAD 1-"+std::to_string(mix_slots())).c_str());
    } else if(wipe_menu&&dme_menu&&dme_direct) {
        panel.lcd(0,"DME DIRECT");panel.lcd(1,(std::to_string(dme_choice)+" | "+adapter.keypad_transition_label(TransitionType::dme,dme_choice)).c_str());
    } else if(wipe_menu&&dme_menu) {
        panel.lcd(0,"DME SELECT");
        auto label=adapter.keypad_transition_slots(TransitionType::dme)?adapter.keypad_transition_label(TransitionType::dme,dme_choice):adapter.dme_keypad_grid()?std::string("PUSH"):adapter.dme_label(dme_choice);
        panel.lcd(1,(std::to_string(dme_shortcut_digit())+" "+label+" | KEYPAD 0-9").c_str());
        if(adapter.dme_keypad_grid())lcd_direction_arrow(dme_choice).draw(panel,36,0);
    }
    SoftMenu::navigation(panel,owns_lcd()||rate_slot>=0||wipe_menu);
}
}

namespace bkds::link {
bool TransitionControl::background_press(const std::bitset<KeyCount>& keys,bool upper) {
    if(!modify_menu||!background_arm||!adapter.supports_dme_background(dme_choice))return false;
    const auto state=adapter.state();for(unsigned i=0;i<12;++i)if(keys[48+i]){int input=i==11?1000+state.me+1:int(i+12*(upper?1:0));bool available=i==11?!background_color&&state.me+1<int(state.me_count):state.available[unsigned(input)]&&(!background_color||state.color_sources[unsigned(input)]);if(keys.count()!=1||!available||!adapter.set_dme_background(dme_choice,input))panel.beep();return true;}
    auto other=keys;other.reset(80);for(unsigned i=160;i<=165;++i)other.reset(i);if(other.any()){background_arm=false;for(unsigned i=0;i<12;++i)panel.led(48+i,0);}return false;
}
}

namespace bkds::link {
void TransitionControl::mix_rotary(int a,int b,int c){
    if(!mix_params_menu)return;
    const int deltas[]={a,b,c};const unsigned max=(mix_super||mix_dust)?100u:255u;
    if(style_keypad){unsigned i=style_kind-240;if(i>=3||!deltas[i])return;int64_t next=int64_t(value)+deltas[i];const unsigned min=mix_dust&&i==1?1:0;value=unsigned(next<min?min:next>max?max:next);typing=true;invalid=false;return;}
    auto previous=mix_values;for(unsigned i=0;i<(mix_super?2u:3u);++i){int64_t next=int64_t(mix_values[i])+deltas[i];const unsigned min=mix_dust&&i==1?1:0;mix_values[i]=unsigned(next<min?min:next>max?max:next);}
    if(previous!=mix_values&&!push_mix_values()){mix_values=previous;panel.beep();}
}
}
