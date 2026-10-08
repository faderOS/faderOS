#include "keys.hpp"
#include <algorithm>
#include <cstdio>
#include "menu.hpp"
namespace bkds::link {
bool KeyControl::aux_chord(const std::bitset<KeyCount>& keys) {
    unsigned count=0,me=0;
    for(unsigned i=0;i<4;++i)if(keys[108+i]){++count;me=i;}
    if(supersource_side>=0)return false;
    if(!count||aux_held.none())return false;
    const auto state=adapter.state();
    unsigned role=0;while(role<4&&!aux_held[role])++role;
    auto other=keys;
    for(unsigned i=0;i<4;++i){other.reset(108+i);other.reset(84+i);}
    if(count!=1||aux_held.count()!=1||other.any()||!state.connected||
       me>=state.me_count||state.aux_sources[role]<0||
       !adapter.set_aux_source(role+1,1000+me))panel.beep();
    return true; // A rejected AUX chord must never change the active M/E.
}
bool KeyControl::overlay_press(const std::bitset<KeyCount>& keys,bool second_layer) {
    if(!adapter.overlay_delegation())return false;
    bool layer_pressed=false;
    for(unsigned i=0;i<12;i++)layer_pressed=layer_pressed||keys[32+i];
    unsigned sources=0,source=0;bool preview=false;
    for(unsigned i=0;i<12;i++) {
        if(keys[i]){++sources;source=i;preview=true;}
        if(keys[16+i]){++sources;source=i;preview=false;}
    }
    if(!sources)return layer_pressed;
    if(overlay_held.none())return false;
    bool extra=false;
    for(unsigned i=0;i<KeyCount;i++)if(keys[i]&&i!=80&&!(i<12||(i>=16&&i<28)||(i>=32&&i<44)))extra=true;
    unsigned layer=0;while(layer<12&&!overlay_held[layer])++layer;
    const auto state=adapter.state();
    if(sources!=1||extra||overlay_held.count()!=1||layer>=state.overlay_channels||layer>=8||!state.connected||!state.overlays[layer].known||!adapter.toggle_overlay(layer,source+(second_layer?12:0),preview))panel.beep();
    return true; // never fall through to a main-bus take after a rejected chord
}
bool KeyControl::press(const std::bitset<KeyCount>& keys,unsigned dsk_slot,const std::bitset<KeyCount>& doubles,bool second_layer) {
    if(!adapter.keyers()||keys.none()) return false;
    if(keys.count()!=1&&(keys[113]||keys[114]))return true;
    bool handled=false;
    const auto state=adapter.state();
    if(state.supersources_supported&&(keys[104]||keys[105])) {
        int side=keys[105]?1:0;
        if(keys.count()!=1||state.supersources[side].source<0){supersource_side=-1;supersource_menu=false;panel.beep();}
        else {supersource_side=supersource_side==side?-1:side;supersource_menu=supersource_side>=0;aux_role=-1;source_arm=false;media_bank=-1;media_menu=false;}
        return true;
    }
    if(supersource_side>=0) {
        if(keys[93]||keys[94]||keys[95]||keys[81]||keys[82]||keys[83]||keys[84]||keys[85]||keys[86]||keys[87]) {supersource_side=-1;supersource_menu=false;}
        else {
            const auto& ss=state.supersources[unsigned(supersource_side)];
            for(unsigned i=0;i<12;++i)if(keys[32+i]) {unsigned window=i+(second_layer?12:0);if(!ss.windows[window].mapped)panel.beep();else supersource_window=int(window);supersource_menu=true;return true;}
            for(unsigned i=0;i<12;++i)if(keys[48+i]) {unsigned source=i==11?unsigned(1000+state.me+1):i+(second_layer?12:0);bool available=i==11?state.me+1<int(state.me_count):state.available[source];if(keys.count()!=1||supersource_window<0||!ss.windows[unsigned(supersource_window)].mapped||!available||!adapter.set_supersource_input(unsigned(supersource_side),unsigned(supersource_window),source))panel.beep();supersource_menu=true;return true;}
            if(keys[176]){supersource_menu=false;return true;}
            for(unsigned id:{67u,68u,69u,70u,88u,89u,90u,91u,92u,96u,97u,98u,99u,100u,101u,102u,106u})if(keys[id]){panel.beep();return true;}
        }
    }
    for(unsigned role=0;role<4;++role) if(keys[84+role]&&state.aux_sources[role]>=0) {aux_role=aux_role==int(role)?-1:int(role);return true;}
    if(aux_role>=0) {
        for(unsigned i=0;i<12;++i) if(keys[48+i]) {
            const unsigned source=i==11?unsigned(1000+state.me+1):i+(second_layer?12:0);
            if((i==11&&state.me>=int(state.me_count)-1)||!adapter.set_aux_source(unsigned(aux_role+1),source))panel.beep();
            return true;
        }
        if(keys[93]||keys[94]||keys[95]||keys[81])aux_role=-1;
    }
const bool native=adapter.native_key_controls();
    const unsigned selected_dsk=dsk_upper?1u:(dsk_slot>0?1u:0u);
    const unsigned source_owner=delegation==4?4+selected_dsk:delegation;
    if(native&&keys.count()!=1){
        if(owns_lcd()&&(keys[160]||keys[161]))return true;
        for(unsigned id:{67u,68u,69u,70u,81u,82u,83u,88u,89u,90u,91u,92u,93u,94u,95u,96u,97u,98u,99u,100u,101u,102u,106u})if(keys[id])return true;
        if(source_arm||media_bank>=0)for(unsigned i=0;i<12;i++)if(keys[32+i])return true;
    }
    if(native&&owns_lcd()&&keys.count()==1){
        if(keys[176]){if(delete_mode){delete_mode=false;delete_target=-1;}else media_menu=false;return true;}
        if(keys[160]){
            if(delete_mode){delete_mode=false;delete_target=-1;return true;}
            show_clear=false;if(adapter.capture_media_still())capture_waiting=true;else panel.beep();return true;
        }
        if(keys[161]){
            if(delete_mode){
                if(delete_target<0||delete_target!=state.media_still){delete_mode=false;delete_target=-1;panel.beep();return true;}
                if(!state.media_program_known||state.media_on_program){panel.beep();return true;}
                if(adapter.clear_media_still(unsigned(delete_target))){clear_waiting=show_clear=true;delete_mode=false;delete_target=-1;}
                else panel.beep();
            }else if(state.connected&&state.media_known&&state.media_capture_status!=1&&state.media_clear_status!=1&&state.media_still>=0){delete_mode=true;delete_target=state.media_still;show_clear=false;}
            else panel.beep();
            return true;
        }

    }
    if(native){
        for(unsigned id:{82u,83u})if(keys[id]){handled=true;const int bank=int(id-82);if(!state.media_known||state.media_still_slots<=unsigned(bank*10))panel.beep();else{media_bank=media_bank==bank&&media_menu?-1:bank;media_menu=media_bank>=0;source_arm=false;}}
        for(unsigned id:{67u,70u})if(keys[id]){source_cut=id==70;if(source_cut)split_selected[source_owner]=true;source_arm=true;media_bank=-1;handled=true;}
        for(unsigned id:{68u,69u,88u,89u,90u,91u,92u,96u,97u,98u,99u,100u,101u,102u,106u})if(keys[id]){handled=true;if(!adapter.key_setting(delegation,selected_dsk,id))panel.beep();else if(id==68||id==69)split_selected[source_owner]=false;}
        if(media_bank>=0)for(unsigned i=0;i<12;i++)if(keys[32+i]){handled=true;if(i>=10||!adapter.select_media_still(unsigned(media_bank)*10+i))panel.beep();}
    }
    if(adapter.key_source_selection()&&keys[81]) { source_arm=!source_arm;source_cut=false;media_bank=-1; handled=true; }
    for(unsigned id:{93u,94u}) if(adapter.key_source_selection()&&keys[id]) {
        const unsigned slot=id-93+(doubles[id]?2u:0u);
        if(state.connected&&state.key_available[slot]){delegation=slot;media_bank=-1;}
        else panel.beep();
        handled=true;
    }
    if(adapter.key_source_selection()&&keys[95]) {
        const unsigned slot=doubles[95]?1u:(dsk_slot>0?1u:0u);
        if(state.connected&&(slot?state.dsk2_known:state.dsk_known)) {
            delegation=4;dsk_upper=doubles[95];media_bank=-1;
        } else panel.beep();
        handled=true;
    }
    if(keys[113]) { adapter.toggle_key(0); handled=true; }
    if(keys[114]) { adapter.toggle_key(1); handled=true; }
    if(keys[116]||keys[117]||keys[118]) {
        handled=true;
        const auto live=adapter.state();
        if(keys[116]&&doubles[116]) adapter.set_next_transition(true,{});
        else if(live.next_known) {
            bool background=live.next_background;auto next=live.next_key;
            if(keys[116]) background=!background;
            if(keys[117]&&live.key_available[0]) next[0]=!next[0];
            if(keys[118]&&live.key_available[1]) next[1]=!next[1];
            if(!background&&!std::any_of(next.begin(),next.end(),[](bool on){return on;})) background=true;
            adapter.set_next_transition(background,next);
        }
    }
    if(adapter.key_source_selection()&&source_arm) {
        for(unsigned i=0;i<12;++i) {
            if(!keys[32+i]) continue;
            handled=true;
            if(native){if(!adapter.key_setting(delegation,selected_dsk,source_cut?70:67,int(i+(second_layer?12:0))))panel.beep();continue;}
            if(i==11) { panel.beep(); continue; }
            const int selected=int(i+(second_layer?12:0));
            if(delegation==4) { if(!adapter.set_dsk_source(selected_dsk,selected)) panel.beep(); }
            else if(!adapter.set_key_source(delegation,selected)) panel.beep();
        }
    }
    return handled;
}
void KeyControl::refresh(unsigned dsk_slot,bool second_layer) {
    const auto state=adapter.state();
    for(unsigned role=0;role<4;++role)panel.led(84+role,state.connected&&state.aux_sources[role]>=0?(aux_role==int(role)?2:1):0);
    if(aux_role>=0&&state.aux_sources[unsigned(aux_role)]<0)aux_role=-1;
    if(aux_role<0&&std::any_of(state.aux_sources.begin(),state.aux_sources.end(),[](int source){return source>=0;}))for(unsigned i=0;i<12;++i)panel.led(48+i,0);
    if(aux_role>=0) {
        const int source=state.aux_sources[unsigned(aux_role)];
        for(unsigned i=0;i<12;++i) {
            const bool upper=source==int(i+12), active=source==int(i)||upper||(i==11&&source==1000+state.me+1);
            panel.led(48+i,state.connected&&active?2:0,upper?500:0);
        }
    }

    if(clear_waiting&&(!state.connected||state.media_clear_status!=1)){if(!state.connected||state.media_clear_status!=2)panel.beep();clear_waiting=false;}
    if(delete_mode&&(!state.connected||!state.media_known||state.media_still!=delete_target)){delete_mode=false;delete_target=-1;}
    if(capture_waiting&&(!state.connected||state.media_capture_status!=1)){if(!state.connected||state.media_capture_status!=2)panel.beep();capture_waiting=false;}
    if(!state.supersources_supported){supersource_side=-1;supersource_menu=false;}
    if(supersource_side>=0&&state.command_failures>supersource_failures)panel.beep();
    supersource_failures=state.command_failures;
    for(unsigned side=0;side<2;++side)panel.led(104+side,state.connected&&state.supersources_supported&&state.supersources[side].source>=0?(supersource_side==int(side)?2:1):0);
    if(adapter.me_delegation()) for(unsigned s=0;s<4;++s) panel.led(108+s,state.connected&&s<state.me_count?(state.me==int(s)?2:1):0);
    if(adapter.overlay_delegation()) {
        if(state.command_failures>overlay_failures)panel.beep();
        overlay_failures=state.command_failures;
        for(unsigned i=0;i<12;i++)panel.led(32+i,0);
        for(unsigned i=0;i<12;i++) {
            const bool available=state.connected&&i<state.overlay_channels&&i<8&&state.overlays[i].known;
            // KY-307 shares one color per row: use blinking for delegation.
            panel.led(32+i,available?1:0,available&&overlay_held[i]?500:0);
        }
    }
    if(!adapter.keyers()) {for(unsigned id:{81u,93u,94u,95u,113u,114u,116u,117u,118u})panel.led(id,0);return;}
    if(!adapter.key_source_selection())source_arm=false;
    const unsigned slot=dsk_upper?1u:(dsk_slot>0?1u:0u);
    if(adapter.native_key_controls()){
        for(unsigned id:{82u,83u})panel.led(id,state.connected&&state.media_known&&state.media_still_slots>(id-82)*10?(media_bank==int(id-82)?2:1):0);
        const bool dsk=delegation==4;const unsigned i=dsk?slot:delegation;
        const int kind=dsk?state.dsk_kind[i]:state.key_kind[i];
        for(unsigned id:{88u,89u,90u,91u,92u,106u})panel.led(id,state.connected?(kind==int(id)?2:id==88&&state.key_luma_supported?1:0):0);
        panel.led(90,state.connected&&kind==88&&(dsk?state.dsk_premult[i]:state.key_premult[i])==1?1:0);
        for(unsigned id:{96u,97u,98u,99u,100u,101u,102u}){int value=-1;if(id==96&&!dsk)value=state.key_border[i];if(id==98&&!dsk)value=state.key_shadow[i];if(id==101&&!dsk)value=state.key_submask[i];if(id==100)value=dsk?state.dsk_mask[i]:state.key_mask[i];if(id==102)value=dsk?state.dsk_invert[i]:state.key_invert[i];panel.led(id,state.connected&&value==1?1:0);}
        const bool source_known=state.connected&&kind>=0;
        panel.led(67,source_known?(source_arm&&!source_cut?2:1):0);
        const unsigned owner=dsk?4+slot:delegation;
        panel.led(70,source_known&&split_selected[owner]?(source_arm&&source_cut?2:1):0);
    }
    panel.led(81,source_arm?2:0);
    panel.led(93,adapter.key_source_selection()&&(delegation==0||delegation==2)?2:0,delegation==2?500:0);
    panel.led(94,adapter.key_source_selection()&&(delegation==1||delegation==3)?2:0,delegation==3?500:0);
    panel.led(95,adapter.key_source_selection()&&delegation==4?2:0,delegation==4&&slot?500:0);
    panel.led(113,state.connected&&state.key_available[0]&&state.keyers_known&&state.key_on[0]?2:0);
    panel.led(114,state.connected&&state.key_available[1]&&state.keyers_known&&state.key_on[1]?2:0);
    panel.led(116,state.connected&&state.next_known&&state.next_background?2:0);
    panel.led(117,state.connected&&state.key_available[0]&&state.next_known&&state.next_key[0]?2:0);
    panel.led(118,state.connected&&state.key_available[1]&&state.next_known&&state.next_key[1]?2:0);
    int selected=-1;
    if(source_arm) {
        if(delegation==4) selected=source_cut?state.dsk_cut_source[slot]:state.dsk_source[slot];
        else selected=source_cut?state.key_cut_source[delegation]:state.key_source[delegation];
    }
    bool on_air=false;
    if(source_arm&&selected>=0) {
        if(delegation==4) {
            on_air=slot?state.dsk2_on:state.dsk_on;
        } else if(delegation<4) {
            on_air=state.key_on[delegation];
        }
    }
    for(unsigned i=0;i<12;++i) {
        const bool chosen=source_arm&&(selected==int(i)||selected==int(i+12));
        if(adapter.native_key_controls()&&media_bank>=0){panel.led(32+i,i<10&&state.media_known?(state.media_still==media_bank*10+int(i)?1:0):0);continue;}
        panel.led(32+i,chosen?(on_air?2:1):0,chosen&&selected>=12?500:0);
    }
    if(supersource_side<0&&supersource_aux_owned){for(unsigned i=0;i<12;++i)panel.led(48+i,0);supersource_aux_owned=false;}
    if(supersource_side>=0) {
        supersource_aux_owned=true;
        const auto& ss=state.supersources[unsigned(supersource_side)];
        panel.led(93,0);panel.led(94,0);panel.led(95,0);
        for(unsigned id:{67u,68u,69u,70u,88u,89u,90u,91u,92u,96u,97u,98u,99u,100u,101u,102u,106u})panel.led(id,0);
        int selected=-1;if(supersource_window>=0&&ss.windows[unsigned(supersource_window)].mapped)selected=ss.windows[unsigned(supersource_window)].input;
        // KY-307 has one color selector per bus. Clear the previous source
        // feedback first, then use LOW throughout and blink the selected item.
        for(unsigned i=0;i<12;++i){panel.led(32+i,0);panel.led(48+i,0);}
        for(unsigned i=0;i<12;++i){bool current=state.connected&&ss.windows[i+(second_layer?12:0)].mapped;bool active=state.connected&&supersource_window>=0&&unsigned(supersource_window)%12==i&&ss.windows[unsigned(supersource_window)].mapped;panel.led(32+i,(active||current)?1:0,active?500:0);
            bool same=state.connected&&selected>=0&&(selected>=1000?i==11&&selected==1000+state.me+1:unsigned(selected)%12==i);bool available=state.connected&&(i==11?state.me+1<int(state.me_count):state.available[i+(second_layer?12:0)]&&int(i+(second_layer?12:0))!=ss.source);panel.led(48+i,(same||available)?1:0,same?500:0);}
    }

}
void KeyControl::cancel_menu_for(const std::bitset<KeyCount>& keys){
    for(unsigned id=0;id<KeyCount;++id)if(keys[id]&&id!=80&&id!=104&&id!=105&&id!=176&&!(id>=32&&id<44)&&!(id>=48&&id<60))supersource_menu=false;
    for(unsigned id=0;id<KeyCount;id++)if(keys[id]&&id!=160&&id!=161&&id!=176){delete_mode=false;delete_target=-1;break;}
    for(unsigned id=0;id<KeyCount;id++)if(keys[id]&&id!=80&&id!=82&&id!=83&&id!=160&&id!=161&&id!=176&&(id<32||id>43)){media_menu=false;return;}
}
void KeyControl::render(){
    if(!owns_lcd())return;
    if(supersource_side>=0&&supersource_menu){for(unsigned id=160;id<=165;++id)panel.led(id,0);SoftMenu::navigation(panel,true);auto state=adapter.state();const auto& ss=state.supersources[unsigned(supersource_side)];char title[81],line[81];std::snprintf(title,sizeof title,"SS %s  %s",supersource_side?"PROGRAM":"PREVIEW",ss.name.data());
        if(supersource_window<0||!ss.windows[unsigned(supersource_window)].mapped)std::snprintf(line,sizeof line,"SELECT WINDOW ON KEY BUS");else{const auto& box=ss.windows[unsigned(supersource_window)];if(box.input>=1000)std::snprintf(line,sizeof line,"%s  M/E %d",box.name.data(),box.input-999);else std::snprintf(line,sizeof line,"%s  INPUT %d",box.name.data(),box.input+1);}panel.lcd(0,title);panel.lcd(1,line);return;}

    const auto s=adapter.state();SoftMenu menu;char title[41],player[8];const char bank=media_bank?'2':'1';
    if(!s.connected)std::snprintf(title,sizeof title,"FRAME MEM%c ~ ATEM DISCONNECTED",bank);
    else if(delete_mode){
        if(!s.media_program_known)std::snprintf(title,sizeof title,"DELETE BLOCKED: TALLY UNKNOWN");
        else if(s.media_on_program)std::snprintf(title,sizeof title,"DELETE BLOCKED: MP1 ON PROGRAM");
        else std::snprintf(title,sizeof title,"DELETE SLOT %02u?",unsigned(delete_target)+1);
    }
    else if(show_clear&&s.media_clear_status==1)std::snprintf(title,sizeof title,"DELETING SLOT %02u",unsigned(std::max(0,s.media_clear_slot))+1);
    else if(show_clear&&s.media_clear_status==2)std::snprintf(title,sizeof title,"DELETED SLOT %02u",unsigned(std::max(0,s.media_clear_slot))+1);
    else if(show_clear&&s.media_clear_status==3)std::snprintf(title,sizeof title,"DELETE NOT CONFIRMED");
    else if(s.media_capture_status==1)std::snprintf(title,sizeof title,"FRAME MEM%c ~ CAPTURING PROGRAM",bank);
    else if(s.media_capture_status==2)std::snprintf(title,sizeof title,"FRAME MEM%c ~ CAPTURED SLOT %02u",bank,unsigned(std::max(0,s.media_capture_slot))%256+1);
    else if(s.media_capture_status==3)std::snprintf(title,sizeof title,"FRAME MEM%c ~ CAPTURE NOT CONFIRMED",bank);
    else std::snprintf(title,sizeof title,"FRAME MEM%c ~ MP1",bank);
    menu.title(title);const bool supported=adapter.supports_media_capture();if(supported)menu.field(0,"CAPT");
    if(s.media_still>=0)std::snprintf(player,sizeof player,"MP1:%02u",unsigned(s.media_still)%256+1);else std::snprintf(player,sizeof player,"MP1:--");
    menu.field(1,"DELETE");menu.field(2,player);menu.field(3,media_bank?"11-20":"01-10");
    if(delete_mode){menu.field(0,"CANCEL");menu.field(1,delete_target>=0&&delete_target==s.media_still&&s.media_program_known&&!s.media_on_program?"YES":"");}
    menu.show(panel);
    const bool can_clear=s.connected&&s.media_known&&s.media_capture_status!=1&&s.media_clear_status!=1;
    panel.led(161,delete_mode?(delete_target>=0&&delete_target==s.media_still&&s.media_program_known&&!s.media_on_program?2:0):(can_clear?1:0));
    panel.led(160,delete_mode?1:supported?(s.media_capture_status==1?2:1):0,s.media_capture_status==1?500:0);
    SoftMenu::navigation(panel,true);
}

}
