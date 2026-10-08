#include "mixer.hpp"
#include <algorithm>
namespace bkds::link {
Mappings::Mappings() {
    for(unsigned i=0;i<12;i++) {
        buttons[i]={MixerAction::preview,uint8_t(i)};
        buttons[16+i]={MixerAction::program,uint8_t(i)};
        tallies[i]={TallyBus::preview,uint8_t(i),1};
        tallies[16+i]={TallyBus::program,uint8_t(i),2};
    }
    buttons[127]={MixerAction::cut,0};
}
bool valid_mappings(const Mappings& m) {
    for(unsigned i=0;i<KeyCount;i++) {
        const auto& b=m.buttons[i]; const auto& t=m.tallies[i]; const auto* c=control(i);
        if((b.action!=MixerAction::none||t.bus!=TallyBus::none)&&(!c||i==46||i==47||(i>=61&&i<=63)||i==80||i==119||(i>=120&&i<=124)||i==126||i>=128)) return false;
        if(unsigned(b.action)>3||unsigned(t.bus)>2||b.source>=12||t.source>=12||t.level<1||t.level>2) return false;
        if(t.bus!=TallyBus::none&&c&&c->bank>=0) for(unsigned j=0;j<i;j++) {
            const auto* other=control(j);
            if(other&&other->bank==c->bank&&m.tallies[j].bus!=TallyBus::none&&(m.tallies[j].level!=t.level||m.tallies[j].bus!=t.bus)) return false;
        }
    }
    return true;
}
void MixerControl::shift(const Changes& change,uint32_t now) {
    if(change.pressed[80]) {
        if(shift_locked) { shift_locked=false; shifted=false; shift_lamp_grace=false; }
        else {
            shifted=true; shift_pressed_at=now; shift_lamp_grace=true;
            if(change.double_click[80]) shift_locked=true;
        }
    }
    if(change.released[80]) shifted=shift_locked;
}
bool MixerControl::press_output(const std::bitset<KeyCount>& keys) {
    if(!keys[61]&&!keys[62]&&!keys[63]) return false;
    if(keys.count()==1) {
        const auto kind=keys[63]?OutputKind::stream:keys[62]?OutputKind::record:OutputKind::virtualcam;
        if(!adapter.output(kind))panel.beep();
    }
    return true;
}
bool MixerControl::press(const std::bitset<KeyCount>& keys) {
    unsigned found=KeyCount;
    for(unsigned i=0;i<KeyCount;i++) if(keys[i]&&mappings.buttons[i].action!=MixerAction::none) {
        if(found!=KeyCount) return false; // ambiguous simultaneous source/CUT chord
        found=i;
    }
    if(found==KeyCount) return false;
    const auto s=adapter.state(); const auto& b=mappings.buttons[found];
    const bool live_source=adapter.live_bus_changes()&&s.transitioning&&(b.action==MixerAction::program||b.action==MixerAction::preview);
    const bool live_cut=adapter.live_transition_controls()&&s.transitioning&&b.action==MixerAction::cut;
    if(!s.connected||!s.studio||(s.busy&&!live_source&&!live_cut)||!s.sources) return false;
    return adapter.request(b.action,b.source+(shifted?12:0));
}
void MixerControl::refresh_shift(uint32_t now) {
    adapter.set_multiview_bank(shifted);
    // Bridge the gap between clicks visually; bank selection still follows release.
    if(shift_lamp_grace&&uint32_t(now-shift_pressed_at)>300) shift_lamp_grace=false;
    const auto s=adapter.state();
    const auto upper=[](int source) { return source>=12&&source<24; };
    bool armed=s.connected&&(upper(s.program)||(s.studio&&upper(s.preview)));
    if(s.connected)for(const auto& o:s.overlays)if(o.known&&o.bus!=TallyBus::none&&upper(o.source))armed=true;
    panel.led(80,shifted||shift_lamp_grace?2:armed?1:0);
}
void MixerControl::refresh(uint32_t now) {
    const auto s=adapter.state();
    refresh_shift(now);
    for(unsigned i=0;i<3;i++) panel.led(63-i,s.connected&&s.output_known[i]&&s.output_active[i]?2:0);
    // Clear desired mapped lamps first: a KY bank shares its LOW/HIGH selector.
    // This is only staging; flush sends the final state, not intermediate blanks.
    for(unsigned i=0;i<KeyCount;i++) if(mappings.tallies[i].bus!=TallyBus::none) panel.led(i,0);
    for(unsigned i=0;i<KeyCount;i++) {
        const auto& t=mappings.tallies[i]; if(t.bus==TallyBus::none) continue;
        const int source=t.bus==TallyBus::preview?s.preview:s.program;
        // SHIFT chooses which bank a press selects. Tally stays visible on
        // both floors: lower bank steady, upper bank blinking.
        const bool upper=source==int(t.source+12);
        const bool lower=source==int(t.source);
        const bool active=s.connected&&(t.bus!=TallyBus::preview||s.studio)&&(upper||lower);
        // HIGH for what is on air. A dissolve keeps the preview source on air too.
        const bool on_air=t.bus==TallyBus::program||(s.preview>=0&&s.preview==s.program)||(t.bus==TallyBus::preview&&s.both_sources);
        unsigned level=active?(on_air?uint8_t(2):t.level):0;
        bool blink=active&&upper;
        if(s.connected)for(const auto& o:s.overlays) {
            if(!o.known||o.bus==TallyBus::none||(o.source!=int(t.source)&&o.source!=int(t.source+12)))continue;
            // Each KY-307 row shares a color. Prepared overlays belong to PST;
            // on-air overlays belong to PGM, not an orange lamp in a red row.
            if(o.bus!=t.bus)continue;
            level=std::max(level,on_air?2u:unsigned(t.level));
            if(o.source==int(t.source+12))blink=true;
        }
        panel.led(i,level,level&&blink?500:0);
    }
}
}
