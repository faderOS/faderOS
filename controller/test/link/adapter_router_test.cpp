// Exercise adapter changes through the same Application used by the serial host.
#define main picohost_main_not_run
#include "../../src/link/main_posix.cpp"
#undef main
#include <cassert>
struct MemoryStore final:ConfigStore { bool save(const Config&) override { return true; } };
struct ProbeAdapter final:MixerAdapter {
    MixerState view;
    Config last;
    unsigned dust_calls=0,border_calls=0;
    bool prepare_dust(const std::array<uint32_t,3>&)override{++dust_calls;return true;}
    bool prepare_wipe_border_profile(int,int,int)override{++border_calls;return true;}
    unsigned configured=0,disabled=0,commands=0;
    bool frames=false,usks=true,overlays=false;
    unsigned overlay_calls=0,overlay_layer=99,overlay_source=99;bool overlay_preview=false;
    bool overlay_delegation()const override{return overlays;}
    bool toggle_overlay(unsigned layer,unsigned source,bool preview)override{++overlay_calls;overlay_layer=layer;overlay_source=source;overlay_preview=preview;return true;}
    void configure(const Config& c) override {last=c;++configured;view={};}
    void deactivate() override {++disabled;view={};}
    MixerState state() override {return view;}
    bool request(MixerAction,unsigned) override {++commands;return true;}
    bool frame_rates() const override {return frames;}
    bool keyers() const override {return frames&&usks;}
    bool me_delegation() const override {return frames;}
    bool set_me(unsigned me) override {if(!view.connected||me>=view.me_count)return false;view.me=int(me);return true;}
    unsigned ss_calls=0,ss_side=99,ss_window=99,ss_input=99;
    bool set_supersource_input(unsigned side,unsigned window,unsigned input) override {++ss_calls;ss_side=side;ss_window=window;ss_input=input;return true;}
    unsigned keypad_transition_slots(TransitionType t) const override {return t==TransitionType::mix?3:7;}
    unsigned first_keypad_transition_slot(TransitionType t) const override {return t==TransitionType::dme?0:1;}
    unsigned first_stinger_slot() const override {return 1;}
    bool keypad_transition_available(TransitionType,unsigned s) const override {return s!=6;}
    std::string keypad_transition_label(TransitionType,unsigned) const override {return "NATIVE EFFECT";}
    bool supports_dme_parameters() const override {return false;}
    uint32_t default_softness() const override {return frames?0:3;}
};
int main() {
    ProbeAdapter obs,kavtor;kavtor.frames=true;
    AdapterRouter router;router.attach(Backend::obs,obs);router.attach(Backend::kavtor,kavtor);
    Config c;router.configure(c);
    assert(router.prepare_dust({50,2,0})&&obs.dust_calls==1&&kavtor.dust_calls==0);
    assert(router.prepare_wipe_border_profile(-1,15,0)&&obs.border_calls==1&&kavtor.border_calls==0);
    assert(router.keypad_transition_slots(TransitionType::mix)==3&&router.keypad_transition_slots(TransitionType::dme)==7);
    assert(router.first_keypad_transition_slot(TransitionType::dme)==0&&router.first_stinger_slot()==1);
    assert(!router.keypad_transition_available(TransitionType::dme,6)&&router.keypad_transition_available(TransitionType::dme,7));
    assert(router.keypad_transition_label(TransitionType::mix,1)=="NATIVE EFFECT"&&!router.supports_dme_parameters());
    MemoryStore store;Mappings profile;profile.transitions.rates[0]=730;profile.buttons[0].source=7;
    Mappings live=profile;
    Application app(store,c,router,live,profile,true);
    assert(router.supported()&&obs.configured==1&&app.transitions().duration(0)==730);
    obs.view.auth_required=true;app.tick();
    const auto& lcd=app.panel.line(1);
    assert(std::string(lcd.begin(),lcd.end()).find("AUTH REQUIRED - CONFIGURE IN WEB")!=std::string::npos);
    obs.view.auth_required=false;
    auto choose=[&](Backend b){auto next=app.configuration.saved();next.backend=b;assert(app.configuration.save(next,app.configuration.revision())==SaveResult::saved);};
    // A menu save during a take must not interrupt the active adapter.
    obs.view.connected=true;obs.view.transitioning=true;
    choose(Backend::kavtor);app.apply_adapter();
    assert(obs.disabled==0&&kavtor.configured==0&&router.backend()==Backend::obs);
    obs.view.transitioning=false;app.apply_adapter();
    assert(obs.disabled==1&&kavtor.configured==1&&router.frame_rates());
    assert(app.transitions().duration(0)==25&&app.transitions().soft_value()==0);
    assert(live.buttons[0].source!=7);
    assert(router.request(MixerAction::cut,0)&&kavtor.commands==1&&obs.commands==0);
    // Exercise the real application/router path, not only KeyControl with a direct adapter.
    kavtor.view.connected=true;kavtor.view.supersources_supported=true;kavtor.view.available.set(1);
    auto& ss=kavtor.view.supersources[0];ss.source=9;ss.windows[0].mapped=ss.windows[1].mapped=true;ss.windows[0].input=0;
    auto ss_press=[&](unsigned id){std::bitset<KeyCount> bits;bits.set(id);assert(app.key_control.press(bits,0));app.key_control.refresh(0);};
    ss_press(104);ss_press(32);ss_press(49);
    assert(kavtor.ss_calls==1&&kavtor.ss_side==0&&kavtor.ss_window==0&&kavtor.ss_input==1&&obs.ss_calls==0);
    assert(app.panel.desired_led(32)==1&&app.panel.desired_period(32)==500&&app.panel.desired_led(33)==1);
    ss_press(33);assert(app.panel.desired_led(32)==1&&app.panel.desired_period(32)==0&&app.panel.desired_led(33)==1&&app.panel.desired_period(33)==500);
    ss_press(104);
    auto inactive=app.configuration.saved();inactive.endpoints[unsigned(Backend::obs)].port=4456;
    assert(app.configuration.save(inactive,app.configuration.revision())==SaveResult::saved);
    app.apply_adapter();assert(kavtor.configured==1); // Editing inactive endpoint does not reconnect.
    // Same-adapter endpoint update, with independent OBS address preserved.
    auto next=app.configuration.saved();next.active().port=9200;
    assert(app.configuration.save(next,app.configuration.revision())==SaveResult::saved);
    app.apply_adapter();assert(kavtor.configured==2&&kavtor.disabled==0&&kavtor.last.active().port==9200);
    choose(Backend::obs);app.apply_adapter();
    assert(kavtor.disabled==1&&obs.configured==2&&!router.frame_rates());
    assert(live.buttons[0].source==7&&app.transitions().duration(0)==730);
    assert(app.configuration.saved().endpoints[unsigned(Backend::kavtor)].port==9200);
    // Not implemented never falls through to the last working mixer.
    choose(Backend::vmix);app.apply_adapter();
    assert(!router.supported()&&!router.state().connected&&!router.request(MixerAction::cut,0));
    assert(obs.disabled==2&&obs.commands==0&&kavtor.commands==1);
    choose(Backend::kavtor);app.apply_adapter();assert(kavtor.configured==3);
    // UTILITY delegation is independent of keyer support (ATEM basic adapter).
    ProbeAdapter atem;atem.frames=true;atem.usks=false;router.attach(Backend::atem,atem);
    choose(Backend::atem);app.apply_adapter();atem.view.connected=true;atem.view.me_count=2;
    Snapshot snap;app.state(snap,true);snap.ms=100;snap.held.set(109);app.state(snap,false);app.tick();
    assert(atem.view.me==1&&atem.commands==0&&app.panel.desired_led(109)==2&&app.panel.desired_led(108)==1&&app.panel.desired_led(110)==0);
    snap.ms=200;snap.held.reset();app.state(snap,false);snap.ms=300;snap.held.set(111);app.state(snap,false);assert(atem.view.me==1);
    ProbeAdapter second,third;router.attach(Backend::atem,second,1);router.attach(Backend::atem,third,2);
    auto select=[&](unsigned slot){auto next=app.configuration.saved();next.select_server(slot);next.backend=Backend::atem;assert(app.configuration.save(next,app.configuration.revision())==SaveResult::saved);app.apply_adapter();};
    const auto first_configs=atem.configured;select(1);second.view.connected=true;second.view.program=4;assert(atem.disabled==0&&second.configured==1&&router.request(MixerAction::cut,0)&&second.commands==1&&atem.commands==0);
    select(2);third.view.connected=true;third.view.program=8;second.view.program=9;assert(second.disabled==0&&third.configured==1);select(1);assert(second.configured==1&&router.state().program==9);select(0);assert(atem.configured==first_configs&&second.disabled==0&&third.disabled==0);
    app.tick();assert(app.panel.desired_led(31)==2&&app.panel.desired_led(30)==0);snap.ms=400;snap.held.reset();app.state(snap,false);snap.ms=500;snap.held.set(29);app.state(snap,false);assert(app.requested_server==2&&third.commands==0);
    ProbeAdapter vmix;vmix.overlays=true;router.attach(Backend::vmix,vmix);
    app.requested_server=-1;choose(Backend::vmix);app.apply_adapter();
    vmix.view.connected=vmix.view.studio=true;vmix.view.sources=2;vmix.view.overlay_channels=8;
    for(auto& o:vmix.view.overlays)o.known=true;
    Snapshot overlay;overlay.ms=600;app.state(overlay,true);
    overlay.ms=700;overlay.held[32]=true;app.state(overlay,false);
    overlay.ms=800;overlay.held[0]=true;app.state(overlay,false);
    assert(vmix.overlay_calls==1&&vmix.overlay_layer==0&&vmix.overlay_source==0&&vmix.overlay_preview&&vmix.commands==0);
    overlay.ms=900;overlay.held[0]=false;app.state(overlay,false);
    overlay.ms=1000;overlay.held[17]=true;app.state(overlay,false);
    assert(vmix.overlay_calls==2&&vmix.overlay_source==1&&!vmix.overlay_preview&&vmix.commands==0);
    overlay.ms=1100;overlay.held.reset();app.state(overlay,false);
    overlay.ms=1200;overlay.held[0]=true;app.state(overlay,false);assert(vmix.commands==1); // no held layer: normal PST
    std::cout<<"ADAPTER ROUTER PASS: deferred switch, endpoints, capabilities, mappings, unsupported isolation\n";
}
