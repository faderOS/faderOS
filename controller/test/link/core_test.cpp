#include "link/analog.hpp"
#include "link/core.hpp"
#include "link/keys.hpp"
#include "link/panel.hpp"
#include "link/setup.hpp"
#include "link/home.hpp"
#include "link/menu.hpp"
#include "link/mixer.hpp"
#include "link/transitions.hpp"
#include <cassert>
#include <cstring>
#include <deque>
#include <iostream>
#include <sstream>
#include <vector>
using namespace bkds::link;
struct Fake:Transport {
    std::deque<uint8_t> rx,tx; bool dead=false;
    int read(uint8_t* p,std::size_t) override { if(dead) return -1; if(rx.empty()) return 0; *p=rx.front(); rx.pop_front(); return 1; }
    int write(const uint8_t* p,std::size_t) override { if(dead) return -1; tx.push_back(*p); return 1; }
    void inject(const Frame& f) { Wire w; assert(encode(f,w)); for(std::size_t i=0;i<w.size;i++) rx.push_back(w.bytes[i]); }
    std::vector<Frame> drain() { Decoder d; std::vector<Frame> result; Frame f; while(!tx.empty()) { if(d.feed(tx.front(),f)) result.push_back(f); tx.pop_front(); } return result; }
};
struct Sink:Listener {
    std::string version,date;
    void firmware_info(const char* v,const char* d) override {version=v;date=d;}
    unsigned baseline=0,events=0,acks=0,losses=0,diagnostics=0; Snapshot last{};
    std::array<unsigned,2> gaps{};
    void analog_gap(unsigned axis,uint16_t n) override {gaps[axis]+=n;}
    void diagnostic(const std::array<uint32_t,8>& d) override {assert(d[2]==200);diagnostics++;}
    std::array<unsigned,2> analogs{};std::array<uint16_t,2> values{};
    void analog(unsigned axis,uint16_t,uint16_t value) override { analogs[axis]++;values[axis]=value; }
    void state(const Snapshot& s,bool sync) override { last=s; if(sync) baseline++; else events++; }
    void ack(uint8_t,uint8_t) override { acks++; }
    void lost(const char*) override { losses++; }
};
struct FakeMixer:MixerAdapter {
    MixerState view;
    DmeParameters dme_params;
    void dme_parameters(DmeParameters p) override {dme_params=p;}
    TransitionType transition=TransitionType::mix; uint32_t rate=0,code=0; bool reversed=false; unsigned softness=3;
    bool automatic(TransitionType t,uint32_t r,uint32_t c=0,bool rev=false,uint32_t soft=3) override { transition=t; rate=r; code=c; reversed=rev; softness=soft; view.busy=true; requests++; return true; }
    MixerAction action=MixerAction::none; unsigned target=99,requests=0;
    MixerState state() override { return view; }
    bool request(MixerAction a,unsigned s) override { action=a; target=s; requests++; return true; }
    unsigned dsk_slot=0;
    bool dsk(bool mix,uint32_t ms,unsigned slot=0) override { dsk_slot=slot; action=mix?MixerAction::dsk_mix:MixerAction::dsk_cut;rate=ms;requests++;return true; }
    bool manual(uint16_t pos,TransitionType t,uint32_t c,bool rev,uint32_t soft) override {
        transition=t;code=c;reversed=rev;softness=soft;target=pos;view.busy=true;requests++;return true;
    }
    void cancel_manual() override {}
    bool output(OutputKind kind) override { action=MixerAction::output;target=unsigned(kind);requests++;return true; }
};
struct PreviewMixer:FakeMixer {
    bool desired=false;unsigned previews=0;
    bool supports_transition_preview() const override {return true;}
    bool set_transition_preview(bool on) override {desired=on;++previews;return true;}
};
struct MemoryStore:ConfigStore { Config saved{}; unsigned writes=0; bool fail=false; bool save(const Config& c) override { if(fail) return false; saved=c; writes++; return true; } };
static bool has(const Line& l,const char* text) { return std::string(l.begin(),l.end()).find(text)!=std::string::npos; }
static void press(Setup& s,unsigned id) { std::bitset<KeyCount> k; k.set(id); s.press(k); }
static void type_number(Setup& s,const char* text) {
    static const unsigned ids[]={153,145,146,147,137,138,139,129,130,131};
    while(*text) { press(s,ids[unsigned(*text-'0')]); text++; }
}
// Exercise negotiation under partial I/O, missing acknowledgements and clock wrap.
static void baud_test(unsigned mode,uint32_t start,uint32_t rate) {
    struct Serial:Fake {
        uint32_t rate=9600; bool fail=false; unsigned delay=0;
        int set_baud(uint32_t value) override {
            if(value!=9600) { if(fail) return -1; if(delay) { delay--; return 0; } }
            rate=value; return 1;
        }
    } transport;
    transport.fail=mode==4; transport.delay=mode==6?100:mode==7?10000:0;
    Sink sink; Session session(transport,sink); session.prefer_baud(rate); session.start(42,start);
    Decoder decoder; unsigned sets=0,confirms=0,hellos=0;
    bool ready=false;
    for(uint32_t t=0;t<20000;t++) {
        session.tick(start+t);
        while(!transport.tx.empty()) {
            Frame f; bool complete=decoder.feed(transport.tx.front(),f); transport.tx.pop_front();
            if(!complete) continue;
            Frame reply; reply.session=f.session; reply.seq=f.seq;
            if(f.type==1) {
                assert(t>=5500); // No bytes before the physical firmware timeout plus margin.
                hellos++; reply.type=0x81; reply.length=59; reply.payload[1]=1; reply.payload[3]=mode==5?31:63;
            } else if(f.type==5) continue;
            else {
                reply.type=0x80; reply.length=1;
                if(f.type==4) {
                    sets++; assert(read32(f.payload.data())==rate);
                    if(mode==1) reply.payload[0]=1;
                    if(mode==2) continue;
                }
                if(f.type==6) { confirms++; assert(transport.rate==rate); if(mode==3) continue; }
            }
            transport.inject(reply);
        }
        if(session.ready()) { ready=true; break; }
    }
    assert(ready&&!session.is_closed());
    if(mode==0||mode==6) { assert(session.baud()==rate&&sets==1&&confirms==1&&hellos==1); }
    else { assert(session.baud()==9600); }
    if(mode==2||mode==3||mode==4||mode==7) assert(hellos==2&&sink.losses==1);
    if(mode==5) assert(sets==0&&confirms==0);
}
int main() {
    {
        struct Retry:FakeMixer {bool valid_dme_code(uint32_t code)const override{return code==1051;}uint32_t first_direct_dme_code()const override{return 1051;}bool valid_wipe_code(uint32_t code)const override{return code==23;}} adapter;
        Panel panel;TransitionControl tr(panel,adapter);auto key=[&](unsigned id){std::bitset<KeyCount> b;b[id]=true;tr.press(b);tr.refresh();};
        const unsigned digits[]={153,145,146,147,137,138,139,129,130,131};auto number=[&](const char* n){while(*n)key(digits[unsigned(*n++-'0')]);};
        key(122);key(155);number("1050");key(156);const auto rejected=panel.desired_digits();tr.refresh();assert(panel.desired_digits()==rejected&&panel.desired_period(156)==500);
        number("1051");key(156);key(126);assert(adapter.code==1051&&panel.desired_period(156)==0);
        adapter.view.busy=false;key(121);key(155);number("999");key(156);number("23");key(156);key(126);assert(adapter.code==23);
        adapter.view.busy=false;key(128);number("1");key(156);number("300");key(156);key(126);assert(adapter.rate==300);
    }
    {
        Panel panel;MemoryStore store;Configuration config(store,Config{});Setup setup(panel,config);
        press(setup,47);press(setup,160);press(setup,160);press(setup,161);press(setup,160);
        type_number(setup,"999");setup.refresh_keypad();const auto rejected=panel.desired_digits();assert(rejected[0]==9&&rejected[1]==9&&rejected[2]==9);
        press(setup,156);setup.refresh_keypad();assert(setup.owns_keypad()&&panel.desired_digits()==rejected);
        type_number(setup,"192");setup.refresh_keypad();assert(panel.desired_digits()[0]==2&&panel.desired_digits()[1]==9&&panel.desired_digits()[2]==1);press(setup,156);assert(!setup.owns_keypad()&&has(panel.line(1),"192"));
    }
    {
        struct Mix:FakeMixer {
            unsigned writes=0;
            unsigned keypad_transition_slots(TransitionType t)const override{return t==TransitionType::mix?7:0;}
            bool supports_mix_preparation(bool)const override{return true;}
            bool prepare_mix(bool super,uint32_t a,uint32_t b)override{if(super){view.super_gain_a=a;view.super_gain_b=b;}else view.dip_rgb=a;++view.mix_preparation_revision;++writes;return true;}
        } a;Panel p;TransitionControl t(p,a);
        auto key=[&](unsigned id){std::bitset<KeyCount> k;k[id]=true;assert(t.press(k));t.refresh();};
        key(120);key(138);key(173);assert(has(p.line(0),"DIP COLOR")&&p.desired_led(173)==2);
        t.mix_rotary(10,20,30);t.refresh();assert(a.view.dip_rgb==0x0a141e);
        key(160);auto before=a.writes;key(145);key(146);assert(a.writes==before&&p.desired_period(156)==500);key(156);assert(a.view.dip_rgb==0x0c141e);
        key(161);key(146);key(161);assert(a.view.dip_rgb==0x0c141e&&p.desired_period(156)==0);
        key(176);assert(p.desired_led(173)==1);key(145);assert(p.desired_led(173)==0);key(138);assert(p.desired_led(173)==1);
        Changes reset;reset.pressed[173]=reset.double_click[173]=true;t.modifiers(reset,100,true);t.refresh();assert(a.view.dip_rgb==0&&!has(p.line(0),"DIP COLOR"));
        key(129);key(173);assert(has(p.line(0),"SUPER MIX"));key(161);before=a.writes;key(138);key(153);assert(a.writes==before);key(156);assert(a.view.super_gain_b==50);
        t.mix_rotary(-25,0,0);assert(a.view.super_gain_a==75&&a.view.super_gain_b==50);key(165);assert(a.view.super_gain_a==100&&a.view.super_gain_b==100);
    }
    {
        struct Bg:FakeMixer {
            bool supports_dme()const override{return true;}
            bool valid_dme_code(uint32_t code)const override{return code==1001||code==1041;}
            bool supports_dme_background(unsigned code)const override{return code==1041;}
            bool set_dme_background(unsigned code,int source)override{view.dme_preset_backgrounds[0]={code,source};return true;}
        } a;a.view.connected=true;a.view.available.set();Panel p;TransitionControl tr(p,a);
        auto key=[&](unsigned id){std::bitset<KeyCount> k;k[id]=true;assert(tr.background_press(k,false)||tr.press(k));tr.refresh();};
        key(122);key(155);key(145);key(153);key(137);key(145);key(156);key(173);
        assert(has(p.line(0),"BKGD BLACK"));key(160);key(48+2);tr.refresh();
        assert(a.dme_background(1041)==2);assert(has(p.line(0),"BKGD INPUT 3"));
        key(176);assert(p.desired_led(173)==1);key(173);key(161);assert(a.dme_background(1041)==-1);
    }

    {
        struct Tiles:FakeMixer {unsigned size=10,writes=0;
            bool wipe_modifiers()const override{return true;}
            bool wipe_tiles_supported(uint32_t code)const override{return code==200;}
            bool wipe_tiles(uint32_t,unsigned value,bool)override{size=value;++writes;return true;}
        } a;Panel p;TransitionControl tr(p,a);
        auto key=[&](unsigned id){std::bitset<KeyCount> bits;bits[id]=true;assert(tr.press(bits));tr.refresh();};
        key(121);key(155);key(146);key(153);key(153);key(156);key(173);
        assert(has(p.line(0),"BLOCK SIZE"));assert(has(p.line(1),"10%"));
        key(160);auto before=a.writes;key(146);key(153);assert(a.size==10&&a.writes==before);
        key(156);assert(a.size==20);tr.rotary(2);assert(a.size==22);
        key(160);key(153);key(156);assert(a.size==22); // invalid zero is not committed
        key(176);key(161);assert(a.size==10);
    }

    {
        struct Geometry:FakeMixer {unsigned vertices=5,rounding=15,writes=0;
            bool wipe_modifiers()const override{return true;}
            bool wipe_geometry_supported(uint32_t code)const override{return code==49||(code>=300&&code<=304);}
            bool wipe_geometry(uint32_t,unsigned v,unsigned r,bool)override{vertices=v;rounding=r;++writes;return true;}
        } a;Panel p;TransitionControl tr(p,a);
        auto key=[&](unsigned id){std::bitset<KeyCount> bits;bits[id]=true;assert(tr.press(bits));tr.refresh();};
        key(121);key(155);key(137);key(131);key(156);key(173);assert(has(p.line(0),"POLYGON"));
        key(160);auto before=a.writes;key(147);assert(a.writes==before&&a.vertices==5&&p.desired_period(156)==500);
        key(156);assert(a.vertices==3&&p.desired_period(156)==0);key(160);tr.rotary(2);assert(a.vertices==3);key(156);assert(a.vertices==5);
        tr.rotary(2);tr.refresh();assert(a.vertices==7);key(128);assert(p.desired_led(173)==1);
        key(173);key(161);assert(a.vertices==5);
    }

    {
        struct SS:FakeMixer{bool keyers()const override{return true;}} a;
        a.view.connected=true;a.view.supersources_supported=true;a.view.available.set();
        a.view.supersources[0].source=6;a.view.supersources[0].windows[0].mapped=true;
        Panel p;KeyControl keys(p,a);
        auto key=[&](unsigned id){std::bitset<KeyCount> b;b[id]=true;assert(keys.press(b,0));keys.refresh(0);};
        key(104);assert(p.desired_led(48)==1);key(104);
        for(unsigned i=0;i<12;++i){assert(p.desired_led(32+i)==0);assert(p.desired_led(48+i)==0);}
    }

    {
        struct DmeDirect:FakeMixer {
            bool supports_dme()const override{return true;}
            bool supports_dme_parameters()const override{return false;}
            unsigned keypad_transition_slots(TransitionType t)const override{return t==TransitionType::dme?9:0;}
            bool valid_dme_code(uint32_t c)const override{return c>=1001&&c<=1004;}
        } a;
        Panel p;TransitionControl tr(p,a);
        auto key=[&](unsigned id){std::bitset<KeyCount> bits;bits[id]=true;assert(tr.press(bits));tr.refresh();};
        key(122);assert(p.desired_led(155)==1);key(155);assert(p.desired_led(155)==2);
        key(145);key(153);key(153);key(146);assert(p.desired_period(156)==500);
        key(156);assert(p.desired_period(156)==0);key(126);assert(a.code==1002);a.view.busy=false;
        key(146);key(146);key(156);assert(a.code==1002&&p.desired_period(156)==500);
        key(128);assert(p.desired_led(156)==0);key(122);key(126);assert(a.code==1002);a.view.busy=false;
        key(155);key(126);assert(a.code==1);a.view.busy=false;
        key(155);key(126);assert(a.code==1002);
    }

    {
        struct Background:FakeMixer {
            unsigned writes=0;int input=-2;
            unsigned keypad_transition_slots(TransitionType)const override{return 9;}
            bool supports_dme_parameters()const override{return false;}
            bool supports_dme_background(unsigned code)const override{return code==9;}
            bool set_dme_background(unsigned,int source)override{input=source;++writes;view.dme_backgrounds[1]=source;return true;}
        } adapter;
        adapter.view.connected=adapter.view.studio=true;adapter.view.available.set();adapter.view.color_sources.set(2);
        Panel panel;TransitionControl controller(panel,adapter);
        auto press=[&](unsigned id){std::bitset<KeyCount> bits;bits.set(id);assert(controller.press(bits));controller.refresh();};
        press(122);press(131);press(173);assert(has(panel.line(1),"COLOR"));adapter.view.dme_backgrounds[1]=-3;controller.refresh();assert(has(panel.line(0),"STATIC IMAGE"));press(160);
        std::bitset<KeyCount> key;key.set(49);assert(controller.background_press(key,false));controller.refresh();assert(adapter.input==1&&adapter.writes==1&&panel.desired_period(49)==500);
        press(162);key.reset();key.set(49);unsigned before=adapter.writes;assert(controller.background_press(key,false));assert(adapter.writes==before);
        key.reset();key.set(50);assert(controller.background_press(key,false));assert(adapter.input==2);
        press(161);assert(adapter.input==-1);press(176);assert(panel.desired_led(160)==0);
    }
    {
        struct Scoped:FakeMixer {
            int global=2,custom=3;
            unsigned keypad_transition_slots(TransitionType)const override{return 9;}
            bool supports_dme_parameters()const override{return false;}
            bool supports_dme_background(unsigned code)const override{return code==9;}
            bool set_dme_background_scope(unsigned,bool useCustom,bool copy=false)override{if(copy)custom=global;view.dme_background_customs[1]=useCustom;view.dme_backgrounds[1]=useCustom?custom:global;return true;}
            bool set_dme_background(unsigned,int source)override{if(view.dme_background_customs[1])custom=source;else global=source;view.dme_backgrounds[1]=source;return true;}
        } adapter;adapter.view.connected=adapter.view.studio=adapter.view.dme_background_scopes_supported=true;adapter.view.dme_background_customs[1]=false;adapter.view.dme_backgrounds[1]=2;adapter.view.available.set();
        Panel panel;TransitionControl controller(panel,adapter);auto key=[&](unsigned id){std::bitset<KeyCount> b;b[id]=true;controller.press(b);controller.refresh();};
        key(122);key(131);key(173);assert(has(panel.line(0),"GLOBAL")&&panel.desired_led(163)==2);
        key(164);assert(has(panel.line(0),"CUSTOM")&&adapter.dme_background(9)==3);key(165);assert(adapter.dme_background(9)==2&&adapter.custom==2);
        key(163);key(160);std::bitset<KeyCount> input;input[51]=true;assert(controller.background_press(input,false));controller.refresh();assert(adapter.global==3&&adapter.custom==2);
        key(164);assert(adapter.dme_background(9)==2);key(176);assert(panel.desired_led(163)==0&&panel.desired_led(164)==0);
    }
    {
        struct Dme:FakeMixer {
            bool frame_rates()const override{return true;}
            bool me_delegation()const override{return true;}
            unsigned keypad_transition_slots(TransitionType t)const override{return t==TransitionType::dme?9:0;}
            unsigned first_keypad_transition_slot(TransitionType)const override{return 0;}
            bool keypad_transition_available(TransitionType,unsigned code)const override{return code<=14;}
            std::string keypad_transition_label(TransitionType,unsigned code)const override{return code==13?"PAGE TURN":"MOVE";}
            bool supports_dme_parameters()const override{return false;}
        } adapter;
        adapter.view.connected=adapter.view.studio=true;adapter.view.me_count=4;
        Panel panel;TransitionControl transitions(panel,adapter);
        auto press=[&](unsigned id){std::bitset<KeyCount> bits;bits.set(id);assert(transitions.press(bits));transitions.refresh();};
        press(122);press(153);assert(panel.desired_led(153)==2);press(154);assert(panel.desired_led(154)==0&&panel.desired_led(153)==2);
        press(147);assert(panel.desired_led(147)==2);press(126);assert(adapter.code==3);
        adapter.view.busy=false;adapter.view.me=1;transitions.refresh();press(122);assert(panel.desired_led(154)==0);
        adapter.view.me=0;transitions.refresh();press(122);assert(panel.desired_led(154)==0&&panel.desired_led(147)==2);
        assert(adapter.keypad_transition_available(TransitionType::stinger,0));
    }
    {
        struct SS:FakeMixer {unsigned calls=0,side=99,window=99,source=99;int instance=-1;
            bool keyers() const override{return true;}
            bool set_supersource_input(unsigned s,unsigned w,unsigned input)override{++calls;side=s;window=w;source=input;instance=view.supersources[s].source;return true;}
        } adapter;
        adapter.view.connected=true;adapter.view.supersources_supported=true;adapter.view.available.set();adapter.view.available.reset(11);adapter.view.available.reset(23);
        for(unsigned side=0;side<2;++side){auto& ss=adapter.view.supersources[side];ss.source=6+side;ss.windows[0].mapped=true;std::strcpy(ss.windows[0].id.data(),"window");std::strcpy(ss.windows[0].name.data(),"WINDOW 1");ss.windows[0].input=0;}
        adapter.view.supersources[0].windows[1].mapped=true;
        Panel panel;KeyControl keys(panel,adapter);
        auto press=[&](unsigned id,bool upper=false){std::bitset<KeyCount> bits;bits[id]=true;assert(keys.press(bits,0,{},upper));};
        press(104);press(32);press(48);assert(adapter.calls==1&&adapter.side==0&&adapter.instance==6&&adapter.requests==0);
        keys.refresh(0);assert(panel.desired_led(32)==1&&panel.desired_period(32)==500&&panel.desired_led(33)==1);
        assert(panel.desired_led(48)==1&&panel.desired_period(48)==500&&panel.desired_led(49)==1&&panel.desired_led(54)==0);
        adapter.view.supersources[0].source=7;adapter.view.supersources[1].source=6; // An AUTO swaps the buses.
        press(49);keys.refresh(0);assert(adapter.calls==2&&adapter.side==0&&adapter.instance==7&&panel.desired_led(104)==2&&panel.desired_led(105)==1);
        press(105);press(50);assert(adapter.side==1&&adapter.instance==6); // Program requires explicit delegation.
        adapter.view.supersources[1].windows[0].mapped=false;unsigned before=adapter.calls;press(48);assert(adapter.calls==before); // Keep index, never fall back to another window.
        auto& upper=adapter.view.supersources[1].windows[12];upper.mapped=true;upper.input=14;std::strcpy(upper.id.data(),"upper");
        press(32,true);keys.refresh(0,false);assert(panel.desired_period(32)==500);press(48);assert(adapter.window==12);
        adapter.view.me_count=4;adapter.view.me=0;press(59);assert(adapter.source==1001);keys.refresh(0);assert(panel.desired_led(59)==1);
        adapter.view.me=3;unsigned calls=adapter.calls;press(59);assert(adapter.calls==calls);keys.refresh(0);assert(panel.desired_led(59)==0);
        assert(adapter.requests==0);keys.reset();assert(!keys.owns_lcd());
    }

    {
        Panel p;p.led(0,1);p.lcd(0,"RESTORE THIS");PanelTest test(p);
        Snapshot snapshot;Changes change;uint32_t now=100;
        auto press=[&](unsigned id){change={};change.pressed.set(id);test.input(snapshot,change);test.tick(now+=100);};
        test.start();test.tick(now);assert(test.active()&&has(p.line(0),"TEST"));
        press(160);snapshot.held.set(0);snapshot.held.set(17);change={};change.pressed=snapshot.held;
        test.input(snapshot,change);test.tick(now+=100);assert(p.desired_led(0)==2&&p.desired_led(17)==2&&has(p.line(0),"HELD 2"));
        press(176);press(162);change={};change.rotary[0]=12;change.rotary[5]=-3;
        test.input(snapshot,change);test.tick(now+=100);assert(has(p.line(0),"ENC 6")&&has(p.line(1),"12")&&has(p.line(1),"-3"));
        press(165);assert(!has(p.line(1),"-3"));
        press(176);press(163);test.analog_sample(0,4095);test.analog_sample(1,0x01ff);test.tick(now+=100);
        assert(has(p.line(1),"4095")&&has(p.line(1),"255"));
        press(176);press(161);press(161);assert(p.desired_led(0)==1);press(162);assert(p.desired_led(0)==2);
        press(176);press(164);press(161);assert(p.line(1)[39]==0);press(162);assert(p.line(1)[0]==0&&p.line(1)[28]==7);
        press(176);press(165);press(160);assert(p.desired_digits()[0]==0x88);press(163);assert(p.desired_digits()[0]==15);
        press(176);press(161);test.tick(now+=100);assert(has(p.line(0),"DEMO"));
        press(176);press(176);assert(!test.active()&&p.desired_led(0)==1&&has(p.line(0),"RESTORE THIS"));
    }

    {
        struct Overlays:FakeMixer {
            unsigned layer=99,source=99;bool preview=false;
            bool overlay_delegation()const override{return true;}
            bool toggle_overlay(unsigned l,unsigned src,bool pv)override{layer=l;source=src;preview=pv;++requests;return true;}
        } a;
        a.view.connected=a.view.studio=true;a.view.overlay_channels=8;
        for(auto& o:a.view.overlays)o.known=true;
        Panel p;Mappings mappings;MixerControl mixer(p,a,mappings);KeyControl keys(p,a);
        std::bitset<KeyCount> held,pressed;held[32]=true;keys.overlay_sync(held);pressed[18]=true;
        assert(keys.overlay_press(pressed,false)&&a.layer==0&&a.source==2&&!a.preview&&a.requests==1);
        pressed.reset();pressed[2]=true;assert(keys.overlay_press(pressed,true)&&a.source==14&&a.preview);
        held[33]=true;keys.overlay_sync(held);const auto calls=a.requests;
        assert(keys.overlay_press(pressed,false)&&a.requests==calls); // ambiguous layers consumed
        held.reset();keys.overlay_sync(held);assert(!keys.overlay_press(pressed,false)); // ordinary source
        held[39]=true;keys.overlay_sync(held);pressed[18]=true;
        assert(keys.overlay_press(pressed,false)&&a.requests==calls); // both source buses ambiguous
        mixer.refresh();keys.refresh(0);assert(p.desired_led(32)==1&&p.desired_led(39)==1&&p.desired_period(39)==500&&p.desired_led(40)==0);
        a.view.overlays[0]={true,2,TallyBus::preview};mixer.refresh();assert(p.desired_led(2)==1&&p.desired_led(18)==0);
        a.view.overlays[0].bus=TallyBus::program;mixer.refresh();keys.refresh(0);assert(p.desired_led(2)==0&&p.desired_led(18)==2&&p.desired_led(32)==1);
        a.view.overlays[0].source=14;mixer.refresh();assert(p.desired_led(18)==2&&p.desired_period(18)==500&&p.desired_led(80)==1);
    }
    {
        struct Families:FakeMixer {
            unsigned keypad_transition_slots(TransitionType t)const override{return t==TransitionType::mix?3:t==TransitionType::dme?7:0;}
            unsigned first_keypad_transition_slot(TransitionType t)const override{return t==TransitionType::dme?0:1;}
            unsigned stinger_slots()const override{return 8;}
            unsigned first_stinger_slot()const override{return 1;}
            bool supports_dme_parameters()const override{return false;}
            bool keypad_transition_available(TransitionType,unsigned i)const override{return i!=6;}
            bool valid_wipe_code(uint32_t c)const override{return c==1||c==3||c==17||c==18;}
        } a;
        Panel p;TransitionControl tr(p,a);auto key=[&](unsigned id){std::bitset<KeyCount> k;k.set(id);assert(tr.press(k));};
        key(120);key(147);key(137);key(154);tr.refresh();assert(p.desired_led(147)==2&&p.desired_led(145)==1&&p.desired_led(153)==0&&p.desired_led(154)==0);key(126);assert(a.transition==TransitionType::mix&&a.code==3);
        key(136);key(129);key(126);assert(a.transition==TransitionType::mix&&a.code==3); // preparation does not arm WIPE
        key(121);key(153);tr.refresh();assert(p.desired_led(153)==0&&p.desired_led(129)==2&&p.desired_led(130)==1);key(126);assert(a.transition==TransitionType::wipe&&a.code==1);
        key(154);key(145);key(126);assert(a.transition==TransitionType::stinger&&a.code==1);
        key(153);key(126);assert(a.code==1);key(131);key(126);assert(a.code==1); // 0 and 9 unavailable
        key(122);key(129);key(139);tr.refresh();assert(p.desired_led(129)==2&&p.desired_led(153)==1&&p.desired_led(139)==0&&p.desired_led(130)==0);key(177);key(126);assert(a.transition==TransitionType::dme&&a.code==7&&a.reversed);
        key(136);key(137);key(126);assert(a.transition==TransitionType::dme&&a.code==7);key(122);key(153);tr.refresh();assert(p.desired_led(153)==2);key(179);key(126);assert(a.transition==TransitionType::dme&&a.code==0&&!a.reversed); // preparation does not arm WIPE
    }

    {
        struct Visual:FakeMixer{
            MultiviewSettings mv;unsigned writes=0,format_writes=0;OutputRoutes routes;VideoFormats formats;
            bool server_info(ServerInfo& out)const override{out.fields={{"MODEL","ATEM TEST"},{"PROTOCOL","2.32"}};return true;}
            bool video_formats(VideoFormats& out)const override{out=formats;return true;}
            bool set_video_format(unsigned mode,int previous)override{assert(previous==formats.current);formats.current=int(mode);++format_writes;return true;}
            bool output_routes(OutputRoutes& out)const override{out=routes;return true;}
            bool set_output_source(unsigned i,unsigned source)override{routes.sources[i]=int(source);++routes.revision;return true;}
            bool set_output_route(unsigned i,bool multi)override{routes.sources[i]=multi?routes.multiview:routes.program;++routes.revision;return true;}
            bool multiview_settings(unsigned index,MultiviewSettings& out)const override{if(index>=2)return false;out=mv;out.count=2;return true;}
            bool set_multiview_inputs(unsigned,bool safe,bool enabled)override{for(auto& w:mv.windows)if(multiview_input(w)&&(safe?w.safe:w.meters)>=0){if(safe)w.safe=enabled;else w.meters=enabled;}++mv.revision;return true;}
            bool set_multiview_window(unsigned,unsigned w,bool safe,bool enabled)override{writes++;if(safe)mv.windows[w].safe=enabled;else mv.windows[w].meters=enabled;++mv.revision;return true;}
        }a;
        a.formats.current=12;a.formats.supported={10,12};OutputSource pgm,mv;pgm.id=10010;mv.id=9001;a.routes.choices[0]={pgm,mv};a.routes.choices[1]={pgm,mv};a.routes.count=2;a.routes.program=10010;a.routes.multiview=9001;a.routes.sources[0]=10010;a.routes.sources[1]=9001;
        a.mv.windows[0].source=1;a.mv.windows[0].present=true;a.mv.windows[0].safe=a.mv.windows[0].meters=0;
        a.mv.windows[3].source=10010;a.mv.windows[3].present=true;a.mv.windows[3].safe=0;
        MemoryStore store;Config config;config.backend=Backend::atem;Configuration configuration(store,config);Panel p;Setup menu(p,configuration);menu.attach_mixer(&a);
        auto press=[&](unsigned id){std::bitset<KeyCount> keys;keys[id]=true;menu.press(keys);};
        press(46);press(165);assert(has(p.line(0),"ATEM SERVER SETUP")&&has(p.line(1),"MODEL: ATEM TEST"));assert(p.desired_led(176)==1&&p.desired_led(181)==1&&p.desired_led(180)==0);press(181);assert(has(p.line(1),"PROTOCOL: 2.32")&&p.desired_led(181)==0&&p.desired_led(180)==1);press(180);press(176);assert(has(p.line(1),"INFO"));press(162);assert(has(p.line(0),"1080p50")&&menu.claims_encoders());assert(!has(p.line(1),"PREV")&&!has(p.line(1),"NEXT"));press(162);assert(has(p.line(0),"1080p25"));press(165);assert(a.format_writes==0&&has(p.line(0),"CHANGE FORMAT"));press(176);assert(a.format_writes==0);press(162);menu.rotate({0,0,-1,0,0,0});assert(has(p.line(0),"1080p25"));press(165);menu.rotate({0,0,1,0,0,0});assert(!has(p.line(0),"CHANGE FORMAT")&&a.format_writes==0);menu.rotate({0,0,1,0,0,0});assert(has(p.line(0),"1080p25"));press(165);press(165);assert(a.format_writes==1&&a.formats.current==10);press(176);assert(has(p.line(1),"MVIEW"));press(160);assert(has(p.line(0),"WINDOW 1")&&has(p.line(1),"SAFE")&&has(p.line(1),"METERS"));
        press(162);menu.sync_overlay();assert(a.writes==1&&p.desired_led(162)==2);press(163);menu.sync_overlay();assert(a.writes==2&&p.desired_led(163)==2);
        press(164);menu.sync_overlay();assert(a.mv.windows[0].safe==0&&a.mv.windows[3].safe==0&&p.desired_led(164)==1);press(164);press(165);menu.sync_overlay();assert(a.mv.windows[0].safe==1&&a.mv.windows[0].meters==0&&a.mv.windows[3].safe==0);press(161);assert(has(p.line(0),"WINDOW 4")&&!has(p.line(1),"METERS"));press(163);assert(a.writes==2);
        press(180);assert(has(p.line(0),"WINDOW 1"));press(160);assert(has(p.line(0),"MV2"));press(176);assert(has(p.line(0),"ATEM SERVER SETUP"));press(161);assert(has(p.line(0),"OUTPUT 1"));press(161);menu.sync_overlay();assert(a.routes.sources[0]==9001);press(160);assert(has(p.line(0),"OUTPUT 2"));press(176);press(47);assert(has(p.line(0),"SYSTEM SETUP"));
    }
    {
        struct Capture:FakeMixer{
            unsigned captures=0,clears=0;
            bool keyers()const override{return true;}bool native_key_controls()const override{return true;}
            bool supports_media_capture()const override{return true;}
            bool select_media_still(unsigned slot)override{view.media_still=int(slot);return true;}
            bool capture_media_still()override{if(view.media_capture_status==1)return false;captures++;view.media_capture_status=1;return true;}
            bool clear_media_still(unsigned slot)override{if(view.media_clear_status==1||!view.media_program_known||view.media_on_program)return false;clears++;view.media_clear_slot=int(slot);view.media_clear_status=1;return true;}
        }a;
        a.view.connected=a.view.media_known=a.view.media_program_known=true;a.view.media_still_slots=20;a.view.media_still=4;
        Panel p;KeyControl k(p,a);auto press=[&](unsigned id){std::bitset<KeyCount> key;key[id]=true;assert(k.press(key,0));};
        press(82);k.render();assert(k.owns_lcd()&&has(p.line(0),"FRAME MEM1")&&has(p.line(1),"CAPT")&&has(p.line(1),"MP1:05"));
        press(160);k.render();assert(a.captures==1&&p.desired_period(160)==500&&has(p.line(0),"CAPTURING PROGRAM"));press(160);assert(a.captures==1);
        a.view.media_capture_status=2;a.view.media_capture_slot=7;k.refresh(0);k.render();assert(has(p.line(0),"CAPTURED SLOT 08")&&a.view.media_still==4);
        press(83);k.render();assert(has(p.line(1),"11-20"));press(176);assert(!k.owns_lcd());press(32);assert(a.view.media_still==10&&!k.owns_lcd());
        std::bitset<KeyCount> soft_key;soft_key[160]=true;assert(!k.press(soft_key,0)&&a.captures==1);
        press(82);k.render();assert(has(p.line(1),"DELETE"));
        press(161);k.render();assert(has(p.line(0),"DELETE SLOT 11?")&&has(p.line(1),"YES")&&a.clears==0);
        press(176);k.render();assert(k.owns_lcd()&&has(p.line(1),"CAPT")&&a.clears==0);
        press(161);press(160);assert(a.clears==0&&a.captures==1);
        press(161);std::bitset<KeyCount> chord;chord[161]=chord[32]=true;assert(k.press(chord,0)&&a.clears==0);
        press(161);k.render();assert(a.clears==1&&a.view.media_clear_slot==10&&has(p.line(0),"DELETING SLOT 11"));press(161);assert(a.clears==1);
        a.view.media_clear_status=2;k.refresh(0);k.render();assert(has(p.line(0),"DELETED SLOT 11"));
        a.view.media_on_program=true;press(161);k.render();assert(has(p.line(0),"MP1 ON PROGRAM")&&p.desired_led(161)==0);press(161);assert(a.clears==1);
        press(176);a.view.media_on_program=false;press(161);a.view.media_on_program=true;press(161);assert(a.clears==1); // goes on air after prompt
        press(176);a.view.media_on_program=false;press(161);a.view.media_still=3;k.refresh(0);k.render();assert(has(p.line(1),"DELETE")&&!has(p.line(1),"YES"));press(161);assert(a.clears==1);k.render();assert(has(p.line(0),"DELETE SLOT 04?"));
        press(176);a.view.media_program_known=false;press(161);k.render();assert(has(p.line(0),"TALLY UNKNOWN"));press(161);assert(a.clears==1);
        std::bitset<KeyCount> elsewhere;elsewhere[47]=true;k.cancel_menu_for(elsewhere);assert(!k.owns_lcd());

    }
    {
        struct Delegated:FakeMixer {
            int selected=-1,slot=-1; unsigned routes=0;
            bool keyers()const override{return true;}bool key_source_selection()const override{return true;}
            bool set_key_source(unsigned k,int source)override{slot=int(k);selected=source;view.key_source[k]=source;return true;}
            bool set_dsk_source(unsigned k,int source)override{slot=4+int(k);selected=source;view.dsk_source[k]=source;return true;}
            bool set_aux_source(unsigned role,unsigned source)override{routes++;view.aux_sources[role-1]=int(source);return true;}
        }a;
        a.view.connected=true;a.view.key_available.set();a.view.dsk_known=a.view.dsk2_known=true;a.view.me_count=4;a.view.aux_sources[0]=1200;
        Panel p;KeyControl k(p,a);
        auto press=[&](unsigned id,bool twice=false,bool upper=false){std::bitset<KeyCount> key,doubleKey;key[id]=true;if(twice)doubleKey[id]=true;assert(k.press(key,0,doubleKey,upper));};
        press(93,true);k.refresh(0);assert(p.desired_period(93)==500);press(81);press(34,false,true);assert(a.slot==2&&a.selected==14);k.refresh(0);assert(p.desired_period(34)==500);
        press(94,true);k.refresh(0);assert(p.desired_period(94)==500);press(35);assert(a.slot==3&&a.selected==3);
        press(95,true);k.refresh(0);assert(p.desired_period(95)==500);press(36);assert(a.slot==5&&a.selected==4);
        press(84);k.refresh(0);assert(p.desired_led(84)==2);press(50,false,true);assert(a.routes==1&&a.view.aux_sources[0]==14);k.refresh(0);assert(p.desired_period(50)==500);
        press(59);assert(a.view.aux_sources[0]==1001);a.view.me=3;press(59);assert(a.routes==2); // Last M/E has no cascade.
        std::bitset<KeyCount> held,utility;held[84]=true;utility[111]=true;
        k.overlay_sync(held);assert(k.aux_chord(utility));assert(a.routes==3&&a.view.aux_sources[0]==1003&&a.view.me==3);
        held.reset();held[87]=true;a.view.aux_sources[3]=1200;utility.reset();utility[108]=true;
        k.overlay_sync(held);assert(k.aux_chord(utility));assert(a.routes==4&&a.view.aux_sources[3]==1000);
        a.view.me_count=1;utility.reset();utility[109]=true;assert(k.aux_chord(utility)&&a.routes==4);
        held[84]=true;k.overlay_sync(held);utility.reset();utility[108]=true;assert(k.aux_chord(utility)&&a.routes==4);
        held.reset();held[85]=true;k.overlay_sync(held);assert(k.aux_chord(utility)&&a.routes==4); // Unconfigured role is consumed.
        k.overlay_sync({});assert(!k.aux_chord(utility)); // Normal UTILITY remains available.
        held.reset();held[84]=true;k.overlay_sync(held);k.reset();assert(!k.aux_chord(utility));
        assert(a.requests==0); // Delegation and AUX must never issue a program take.
    }
    {
        struct NativeKeys:FakeMixer{
            unsigned command=0,still=99,orders=0,delegated=0;int source=-1;
            bool keyers()const override{return true;}bool key_source_selection()const override{return true;}
            bool native_key_controls()const override{return true;}
            bool key_setting(unsigned d,unsigned,unsigned id,int src=-1)override{delegated=d;command=id;source=src;orders++;return true;}
            bool select_media_still(unsigned i)override{still=i;orders++;return true;}
        }a;
        a.view.connected=a.view.media_known=true;a.view.media_still_slots=20;a.view.key_available[0]=true;
        Panel p;KeyControl k(p,a);auto press=[&](unsigned id){std::bitset<KeyCount> keys;keys[id]=true;assert(k.press(keys,0));};
        press(82);assert(a.orders==0);press(41);assert(a.still==9);press(83);assert(a.orders==1);press(32);assert(a.still==10);press(41);assert(a.still==19);press(42);assert(a.orders==3);
        press(93);press(67);press(40);assert(a.command==67&&a.source==8);press(70);press(33);assert(a.command==70&&a.source==1);a.view.key_kind[0]=88;k.refresh(0);assert(p.desired_led(67)==1&&p.desired_led(70)==2);
        press(67);k.refresh(0);assert(p.desired_led(67)==2&&p.desired_led(70)==1);press(81);k.refresh(0);assert(p.desired_led(67)==1&&p.desired_led(70)==1);
        press(68);k.refresh(0);assert(p.desired_led(70)==0);
        press(67);std::bitset<KeyCount> upper;upper[40]=true;assert(k.press(upper,0,{},true)&&a.source==20);a.view.key_source[0]=20;k.refresh(0);assert(p.desired_period(40)==500);
        press(94);press(91);assert(a.delegated==0);a.view.key_kind[0]=106;a.view.key_border[0]=1;k.refresh(0);assert(p.desired_led(106)==2&&p.desired_led(96)==1&&p.desired_led(94)==0);
    }
    {
        struct Banks:FakeMixer{unsigned stinger_slots()const override{return 0;}}a;
        Panel p;TransitionControl t(p,a);auto press=[&](unsigned id){std::bitset<KeyCount> k;k[id]=true;assert(t.press(k));};
        press(121);t.refresh();assert(p.desired_led(154)==0);press(154);t.refresh();assert(p.desired_led(154)==0);press(129);press(126);assert(a.code==1);press(154);press(126);assert(a.code==1);
    }
    {
        struct Live:FakeMixer {bool live_bus_changes() const override{return true;}bool live_transition_controls() const override{return true;}} a;
        Panel p;Mappings mappings;MixerControl buses(p,a,mappings);std::bitset<KeyCount> key;key.set(0);
        a.view.connected=a.view.studio=a.view.busy=a.view.transitioning=true;a.view.sources=24;
        assert(buses.press(key));key.reset();key.set(16);assert(buses.press(key));
        key.reset();key.set(127);assert(buses.press(key));a.view.transitioning=false;key.reset();key.set(0);assert(!buses.press(key));
    }
    {
        struct Native:FakeMixer {
            unsigned id=0,v=0,y=0;std::array<unsigned,KeyCount> values{};
            bool dme_keypad_grid() const override{return true;}
            bool mix_dip() const override{return true;}
            unsigned stinger_slots() const override{return 0;}
            bool native_wipe_modifiers() const override{return true;}
            bool wipe_modifiers() const override{return true;}
            bool wipe_modifier_supported(unsigned key) const override{return key==168||key==170||key==174;}
            bool prepare_wipe_modifier(unsigned key,uint32_t a,uint32_t b) override{id=key;v=a;y=b;values[key]=a;return true;}
            unsigned wipe_border_maximum() const override{return 100;}
        } a;
        Panel p;TransitionControl tr(p,a);auto key=[&](unsigned id){std::bitset<KeyCount> k;k.set(id);assert(tr.press(k));};
        key(122);tr.refresh();assert(p.desired_led(154)==0);key(129);key(154);tr.refresh();assert(p.desired_led(129)==2&&p.desired_led(153)==0&&p.desired_led(154)==0&&p.desired_period(136)==500);key(126);assert(a.transition==TransitionType::dme&&a.code==7);
        key(120);tr.refresh();assert(p.desired_led(154)==0);key(138);tr.refresh();assert(tr.type()==TransitionType::dip&&p.desired_led(154)==0);key(126);assert(a.transition==TransitionType::dip);key(145);tr.refresh();assert(tr.type()==TransitionType::mix&&p.desired_led(154)==0);
        key(121);key(154);assert(tr.type()==TransitionType::wipe); // no native stinger
        key(170);tr.refresh();assert(has(p.line(0),"WIPE ASPECT")&&a.id==170&&a.v==50);
        key(160);key(137);key(153);tr.refresh();assert(a.v==50&&p.desired_period(156)==500);key(156);assert(a.v==40);
        Changes reset;reset.pressed.set(170);reset.double_click.set(170);tr.modifiers(reset,100,true);assert(a.v==50);
        key(168);tr.rotary(80);assert(a.values[168]==80);key(174);a.view.connected=true;tr.joystick(-32767,32767);tr.position_tick(1000);for(uint32_t t=1020;t<=3000;t+=20)tr.position_tick(t);assert(a.id==174&&a.v==0&&a.y==0);tr.joystick(0,0);tr.position_tick(3020);assert(a.v==0&&a.y==0);tr.position_rotary(100,200);assert(a.v==100&&a.y==200);
        tr.refresh();assert(p.desired_led(173)==0&&p.desired_led(171)==0);
    }

    {
        struct WipeMixer:FakeMixer {
            bool accept=true;TransitionType sent=TransitionType::mix;uint32_t frames=0;
            bool frame_rates() const override{return true;}
            bool valid_wipe_code(uint32_t c) const override{return c==18||c==23;}
            bool set_transition_rate(TransitionType t,uint32_t v) override{if(!accept)return false;sent=t;frames=v;return true;}
        } a;
        a.view.rate_known=a.view.wipe_rate_known=true;a.view.auto_frames=25;a.view.rate_revision=1;a.view.wipe_frames=40;a.view.wipe_rate_revision=1;
        Panel p;TransitionControl tr(p,a);tr.defaults(TransitionSettings{});
        auto key=[&](unsigned id){std::bitset<KeyCount> k;k.set(id);assert(tr.press(k));};
        tr.refresh();assert(tr.duration(0)==25);key(121);tr.refresh();assert(tr.duration(0)==40);
        key(128);key(147);key(153);a.accept=false;key(156);assert(tr.duration(0)==40);
        a.accept=true;key(156);assert(tr.duration(0)==30&&a.sent==TransitionType::wipe&&a.frames==30);
        a.view.wipe_frames=30;a.view.wipe_rate_revision++;key(120);tr.refresh();assert(tr.duration(0)==25);
        key(136);key(155);key(131);key(131);key(131);key(156);tr.refresh();assert(p.desired_period(156)==500&&tr.type()==TransitionType::mix);
        key(121);tr.refresh();assert(tr.duration(0)==30&&p.desired_period(156)==0);
    }

    {
        Panel p;PreviewMixer a;a.view.connected=a.view.studio=true;a.view.transition_preview_known=true;
        TransitionControl tr(p,a);std::bitset<KeyCount> k,d;k.set(120);d.set(120);
        assert(tr.press(k,d)&&a.desired&&a.previews==1);assert(!a.view.transition_preview);
        a.view.transition_preview=true;tr.refresh();assert(p.desired_led(120)==2&&p.desired_period(120)==500);
        d.reset();assert(tr.press(k,d)&&!a.desired&&a.previews==2);
        // A preview gesture never executes an on-air request.
        assert(a.requests==0);
        Panel no_preview_panel;FakeMixer no_preview;TransitionControl unavailable(no_preview_panel,no_preview);
        d.set(121);std::bitset<KeyCount> wipe;wipe.set(121);
        assert(unavailable.press(wipe,d)&&unavailable.type()==TransitionType::mix&&no_preview.requests==0);
        d.reset();
        a.view.transition_preview=false;tr.refresh();assert(p.desired_period(120)==0);
        a.view.busy=a.view.manual_transition=a.view.transitioning=true;tr.refresh();assert(p.desired_led(126)==0);
        k.reset();k.set(121);assert(tr.press(k));assert(tr.type()==TransitionType::mix);
    }

    {
        EncoderSteps enc;
        std::array<int32_t,6> delta{};
        delta[0]=1;delta[1]=-1;
        for(unsigned i=0;i<7;i++) {const auto v=enc.apply(delta);assert(v[0]==0&&v[1]==0);}
        auto v=enc.apply(delta);assert(v[0]==1&&v[1]==-1);
        enc.reset();delta={5,0,0,0,0,0};assert(enc.apply(delta)[0]==0);
        delta[0]=-5;assert(enc.apply(delta)[0]==0); // reverse cancels the fraction
        delta[0]=-8;assert(enc.apply(delta)[0]==-1);
        enc.reset();delta={23,-23,8,-8,0,16};v=enc.apply(delta);
        assert((v==std::array<int32_t,6>{2,-2,1,-1,0,2}));
        delta={1,-1,0,0,0,0};v=enc.apply(delta);assert(v[0]==1&&v[1]==-1);
        delta={7,0,0,0,0,0};enc.apply(delta);enc.reset();
        delta[0]=1;assert(enc.apply(delta)[0]==0); // reconnect/navigation discards residual
    }
    for(uint32_t rate:{19200u,38400u,76800u})
        for(unsigned mode=0;mode<8;mode++) { baud_test(mode,0,rate); baud_test(mode,0xfffff000u,rate); }
    for(unsigned n=0;n<=192;n++) {
        Frame f; f.type=0x10; f.seq=65535; f.session=0xabcdef12; f.length=uint16_t(n);
        for(unsigned i=0;i<n;i++) f.payload[i]=uint8_t(i*37);
        Wire w; Frame out; assert(encode(f,w)); assert(decode(w.bytes.data(),w.size-1,out));
        assert(out.seq==f.seq&&out.session==f.session&&out.length==n&&out.payload==f.payload);
        w.bytes[w.size-3]^=1; assert(!decode(w.bytes.data(),w.size-1,out));
    }
    Frame f,out; Wire w; f.type=2; f.session=1; assert(encode(f,w));
    Decoder decoder; for(unsigned i=0;i<300;i++) assert(!decoder.feed(1,out)); assert(!decoder.feed(0,out));
    bool found=false; for(std::size_t i=0;i<w.size;i++) found|=decoder.feed(w.bytes[i],out); assert(found);
    {
        Fake wire;Sink listener;Session link(wire,listener);link.start(123,0);
        for(unsigned t=0;t<100;t++)link.tick(t);
        wire.drain();Frame hello;hello.type=0x81;hello.session=123;hello.length=59;hello.payload[1]=1;hello.payload[2]=1;
        wire.inject(hello);for(unsigned t=100;t<250;t++)link.tick(t);
        auto queries=wire.drain();assert(queries.size()==2&&!link.ready());
        const auto query=queries[0].type==8?queries[0]:queries[1];assert(query.type==8);
        Frame info;info.type=0x87;info.session=123;info.seq=query.seq;info.length=29;info.payload[0]=1;
        std::memcpy(info.payload.data()+1,"0.22",5);std::memcpy(info.payload.data()+17,"Sep 24 2026",12);
        auto malformed=info;malformed.payload[16]='X';wire.inject(malformed);
        for(unsigned t=250;t<350;t++)link.tick(t);
        assert(listener.version.empty()&&!link.ready());
        for(unsigned t=350;t<1000;t++)link.tick(t);
        queries=wire.drain();assert(queries.size()==1&&queries[0].type==8&&queries[0].seq==info.seq);
        wire.inject(info);for(unsigned t=1000;t<1100;t++)link.tick(t);
        assert(link.ready()&&listener.version=="0.22"&&listener.date=="Sep 24 2026");
        link.start(124,1100);assert(listener.version.empty());
    }
    Fake transport; Sink sink; Session session(transport,sink); session.start(42,0);
    for(unsigned t=0;t<100;t++) session.tick(t);
    auto frames=transport.drain(); assert(frames.size()==1&&frames[0].type==1);
    Frame hello; hello.type=0x81; hello.session=42; hello.length=59; hello.payload[1]=1; hello.payload[3]=15;
    transport.inject(hello); for(unsigned t=100;t<200;t++) session.tick(t);
    assert(sink.baseline==1&&session.ready()); transport.drain();
    Frame event; event.type=0x82; event.seq=1; event.session=42; event.length=55;
    transport.inject(event); transport.inject(event);
    for(unsigned t=200;t<400;t++) session.tick(t);
    assert(sink.events==1); frames=transport.drain();
    unsigned event_acks=0; for(const auto& fr:frames) if(fr.type==5) event_acks++;
    assert(event_acks>=2);
    uint8_t led[]={162,2}; assert(session.send(0x10,led,2,400));
    for(unsigned t=400;t<1150;t++) session.tick(t);
    frames=transport.drain(); assert(frames.size()==2&&frames[0].seq==frames[1].seq&&frames[0].payload==frames[1].payload);
    Frame ack; ack.type=0x80; ack.seq=1; ack.session=42; ack.length=1;
    transport.inject(ack); for(unsigned t=1150;t<1200;t++) session.tick(t);
    assert(sink.acks==1&&session.ready());
    transport.dead=true; session.tick(1200); assert(session.is_closed()&&sink.losses==1);
    // Duplicate analog events may fill the TX queue; drop the ACKs, not the session.
    struct Blocking:Fake { int write(const uint8_t*,std::size_t) override { return 0; } };
    Blocking queued; Sink queued_sink; Session queued_session(queued,queued_sink);
    queued_session.start(7,0);
    Frame hello_q=hello; hello_q.session=7;
    queued.inject(hello_q);
    for(unsigned i=0;i<200;i++) queued_session.tick(0);
    assert(queued_sink.baseline==1&&queued_sink.losses==0);
    Frame ev=event; ev.session=7; ev.seq=1;
    for(unsigned i=0;i<40;i++) queued.inject(ev);
    for(unsigned i=0;i<4000;i++) queued_session.tick(0);
    assert(queued_sink.losses==0);
    assert(queued_sink.events==1);
    // Negotiate LCD_PATCH and send only the changed character after a full sync.
    Fake patch_transport; Sink patch_sink; Session patch_session(patch_transport,patch_sink);
    patch_session.start(42,0);
    for(unsigned t=0;t<100;t++) patch_session.tick(t);
    patch_transport.drain(); hello.payload[3]=31; patch_transport.inject(hello);
    for(unsigned t=100;t<200;t++) patch_session.tick(t);
    patch_transport.drain(); assert(patch_session.lcd_patch_supported());
    Panel patch_panel; patch_panel.flush(patch_session,200);
    for(unsigned t=200;t<350;t++) patch_session.tick(t);
    frames=patch_transport.drain(); assert(frames.size()==1&&frames[0].type==0x15&&frames[0].length==81);
    patch_transport.inject(ack);
    for(unsigned t=350;t<400;t++) patch_session.tick(t);
    patch_panel.ack(0x15,0);
    char edited[41]{}; std::copy(patch_panel.line(0).begin(),patch_panel.line(0).end(),edited); edited[5]='X';
    assert(patch_panel.lcd(0,edited)); patch_panel.flush(patch_session,400);
    for(unsigned t=400;t<450;t++) patch_session.tick(t);
    frames=patch_transport.drain(); assert(frames.size()==1&&frames[0].type==0x15&&frames[0].length==2);
    assert(frames[0].payload[0]==5&&frames[0].payload[1]=='X');
    ack.seq=frames[0].seq; patch_transport.inject(ack);
    for(unsigned t=450;t<500;t++) patch_session.tick(t);
    patch_panel.ack(0x15,0);
    assert(patch_panel.lcd(0,"LCD ALSO CHANGED")); assert(patch_panel.led(0,1));
    patch_panel.flush(patch_session,500);
    for(unsigned t=500;t<550;t++) patch_session.tick(t);
    frames=patch_transport.drain(); assert(frames.size()==1&&frames[0].type==0x10);
    // Rejecting a value sends one short pulse ahead of pending LCD/LED work.
    // The pulse is timed from successful send, including across clock wrap.
    for(unsigned scenario=0;scenario<4;scenario++) {
        Fake io;Sink listener;Session link(io,listener);Panel p;
        const uint32_t start=scenario==2?0xfffffff0u:200u;
        link.start(42,start);for(unsigned i=0;i<100;i++)link.tick(start);io.drain();
        io.inject(hello);for(unsigned i=0;i<100;i++)link.tick(start);io.drain();
        if(scenario==0){FakeMixer a;TransitionControl tr(p,a);std::bitset<KeyCount> k;auto key=[&](unsigned id){k.reset();k.set(id);tr.press(k);};key(128);key(140);key(156);}
        else if(scenario==1){MemoryStore store;Configuration config(store,Config{});Setup setup(p,config);press(setup,47);press(setup,160);press(setup,160);press(setup,161);press(setup,160);type_number(setup,"999");}
        else p.beep();
        p.flush(link,start);for(unsigned i=0;i<100;i++)link.tick(start);
        auto out=io.drain();assert(out.size()==1&&out[0].type==0x14&&out[0].payload[0]==1);
        Frame reply=ack;reply.seq=out[0].seq;io.inject(reply);for(unsigned i=0;i<100;i++)link.tick(start);io.drain();p.ack(0x14,0);
        if(scenario==3){p.resync();p.flush(link,start+81);for(unsigned i=0;i<100;i++)link.tick(start+81);out=io.drain();for(const auto& f:out)assert(f.type!=0x14||f.payload[0]==0);}
        else {p.flush(link,start+80);for(unsigned i=0;i<100;i++)link.tick(start+80);out=io.drain();assert(out.size()==1&&out[0].type==0x14&&out[0].payload[0]==0);}
    }
    Inputs input; Snapshot state; input.update(state,true); state.held.set(47); state.ms=10;
    assert(input.update(state,false).pressed[47]); state.held.reset(); state.ms=50; input.update(state,false);
    state.held.set(47); state.ms=100; assert(input.update(state,false).double_click[47]);
    state.rotary[0]=0xffffffff; assert(input.update(state,false).rotary[0]==-1);
    assert(input.update(state,true).pressed.none());
    {
        Panel p;home_screen(p,Backend::obs,true,500,"12:34:56");
        assert(has(p.line(0),(std::string(ProductName)+" "+HostVersion).c_str())&&has(p.line(0),"*OBS"));
        assert(std::string(p.line(1).begin(),p.line(1).end())==std::string(32,' ')+"12:34:56");
        home_screen(p,Backend::obs,false,500,"12:34:56");assert(!has(p.line(0),"*"));
        home_screen(p,Backend::obs,false,1000,"12:34:57");assert(has(p.line(0),"*OBS"));
        MemoryStore memory;Config c;Configuration cfg(memory,c);Setup ui(p,cfg);
        press(ui,47);assert(has(p.line(1),"NET")&&has(p.line(1),"SERVER")&&has(p.line(1),"INFO"));
        assert(p.desired_led(176)==1&&p.desired_led(180)==0&&p.desired_led(181)==0);
        press(ui,160);assert(has(p.line(0),"SYSTEM SETUP ~ NET")&&!has(p.line(1),"MASK"));
        press(ui,161);assert(has(p.line(0),"SYSTEM SETUP ~ NET")); // hidden IP unavailable
        press(ui,160);assert(has(p.line(1),"MASK"));press(ui,161);
        assert(has(p.line(0),"IP ADDRESS")&&has(p.line(1),"UNDO")&&p.desired_led(165)==0);
        std::array<int32_t,6> delta{};delta[3]=1;ui.rotate(delta);assert(p.desired_led(165)==1);
        press(ui,165);assert(p.desired_led(165)==0);press(ui,176);press(ui,176);
        press(ui,163);assert(has(p.line(0),"INFO")&&has(p.line(1),"FIRMWARE: N/A"));
        ui.firmware_info("0.22");assert(has(p.line(1),"FIRMWARE: 0.22"));
        press(ui,176);press(ui,176);assert(!ui.active()&&p.desired_led(176)==0);
        press(ui,46);assert(has(p.line(0),"OBS SERVER SETUP"));
        press(ui,47);assert(ui.active()&&has(p.line(0),"SYSTEM SETUP")&&has(p.line(1),"NET"));
        assert(p.desired_led(46)==0&&p.desired_led(47)==1);
        press(ui,46);assert(ui.active()&&has(p.line(0),"OBS SERVER SETUP"));
        assert(p.desired_led(46)==1&&p.desired_led(47)==0);
        press(ui,46);assert(!ui.active());
        press(ui,47);press(ui,47);assert(!ui.active());
        press(ui,46);ui.close_menu();assert(!ui.active());
        press(ui,47);press(ui,161);press(ui,162);press(ui,160);type_number(ui,"4455");
        assert(cfg.saved().active().port==0&&p.desired_led(165)==1);
        press(ui,156);assert(cfg.saved().active().port==4455&&ui.take_apply_request());
        press(ui,165);assert(cfg.saved().active().port==0&&ui.take_apply_request()&&p.desired_led(165)==0);

    }
    {
        Panel p;FakeMixer a;TransitionControl tr(p,a);
        a.view.connected=a.view.dsk2_known=a.view.dsk2_on=true;
        tr.refresh();assert(p.desired_led(119)==1&&p.desired_led(124)==0);
        auto shift=[&](bool press,bool release,bool dbl,uint32_t now) {
            Changes c;c.pressed[119]=press;c.released[119]=release;c.double_click[119]=dbl;
            tr.dsk_shift(c,now);assert(!c.pressed[119]);tr.refresh();
        };
        auto key=[&](unsigned id){std::bitset<KeyCount> k;k.set(id);tr.press(k);};
        shift(true,false,false,100);key(124);assert(a.dsk_slot==1&&p.desired_led(119)==2&&p.desired_led(124)==2);
        shift(false,true,false,150);key(123);assert(a.dsk_slot==0&&p.desired_led(119)==2);
        shift(true,false,true,200);shift(false,true,false,240);shift(false,false,false,600);
        key(123);assert(a.dsk_slot==1&&p.desired_led(119)==2);
        shift(true,false,false,700);key(124);assert(a.dsk_slot==0&&p.desired_led(119)==1);
        tr.sync_dsk_shift(false);tr.refresh();assert(p.desired_led(119)==1);
        Changes chord;chord.pressed.set(119);chord.pressed.set(124);tr.dsk_shift(chord,800);tr.press(chord.pressed);assert(a.dsk_slot==1);
        a.view.dsk2_mixing=true;tr.refresh();assert(p.desired_led(123)==2);
        tr.sync_dsk_shift(false);tr.refresh();assert(p.desired_led(123)==0);
    }
    {
        struct SingleDsk:FakeMixer {
            unsigned dsk_channels() const override{return 1;}
            bool frame_rates() const override{return true;}
            bool accept=true;uint32_t pushed=0;
            bool set_dsk_rate(uint32_t rate,unsigned slot) override{assert(slot==0);if(!accept)return false;pushed=rate;return true;}
        } a;
        Panel p;TransitionControl tr(p,a);tr.defaults(TransitionSettings{});
        a.view.dsk_frames[0]=35;a.view.dsk_rate_revision[0]=1;tr.refresh();assert(tr.duration(1)==35);
        Changes c;c.pressed.set(119);c.double_click.set(119);tr.dsk_shift(c,100);tr.refresh();assert(!c.pressed[119]&&p.desired_led(119)==0);
        auto key=[&](unsigned id){std::bitset<KeyCount> b;b.set(id);tr.press(b);};
        key(123);assert(a.dsk_slot==0&&a.rate==35);
        key(128);key(128);key(147);key(153);a.accept=false;key(156);assert(tr.duration(1)==35&&a.pushed==0);
        a.accept=true;key(156);assert(tr.duration(1)==30&&a.pushed==30);
        a.view.dsk_frames[0]=40;a.view.dsk_rate_revision[0]++;tr.refresh();assert(tr.duration(1)==40);
        tr.defaults(TransitionSettings{});tr.refresh();assert(tr.duration(1)==40);
    }
    {
        struct FtbOnly:FakeMixer {
            bool supports_ftb() const override{return true;}
            bool frame_rates() const override{return true;}
            bool accept=true;uint32_t sent=0;
            bool fade_to_black(uint32_t frames) override {sent=frames;return true;}
            bool set_ftb_rate(uint32_t frames) override {if(!accept)return false;sent=frames;return true;}
        } a;
        Panel p;TransitionControl tr(p,a);tr.defaults(TransitionSettings{});
        a.view.connected=true;a.view.ftb_frames=35;a.view.ftb_rate_revision=1;tr.refresh();assert(tr.duration(2)==35&&!a.keyers());
        auto key=[&](unsigned id){std::bitset<KeyCount> b;b.set(id);tr.press(b);};
        key(115);assert(a.sent==35);tr.refresh();assert(p.desired_led(115)==0); // no optimistic tally
        a.view.ftb=true;tr.refresh();assert(p.desired_led(115)==2&&p.desired_led(126)==0);
        a.view.ftb=false;tr.refresh();assert(p.desired_led(115)==0);
        key(128);key(128);key(128);key(147);key(153);a.accept=false;key(156);assert(tr.duration(2)==35);
        a.accept=true;key(156);assert(tr.duration(2)==30&&a.sent==30);
        a.view.ftb_frames=40;a.view.ftb_rate_revision++;tr.refresh();assert(tr.duration(2)==40);
        tr.defaults(TransitionSettings{});tr.refresh();assert(tr.duration(2)==40);
    }
    {
        struct Keys:FakeMixer {
            bool keyers() const override{return true;}
            unsigned takes=0;bool requested_background=false;std::array<bool,4> requested_keys{};
            bool toggle_key(unsigned) override{takes++;return true;}
            bool set_next_transition(bool b,const std::array<bool,4>& k) override{requested_background=b;requested_keys=k;return true;}
        } a;
        Panel p;KeyControl control(p,a);
        a.view.connected=a.view.next_known=a.view.keyers_known=true;a.view.key_available.set(0);a.view.key_available.set(1);
        a.view.next_background=true;a.view.next_key={true,true,true,true};a.view.key_on[0]=true;
        std::bitset<KeyCount> keys,doubles;keys.set(116);doubles.set(116);
        assert(control.press(keys,0,doubles));assert((a.requested_background&&a.requested_keys==std::array<bool,4>{}&&a.takes==0&&a.view.key_on[0]));
        doubles.reset();assert(control.press(keys,0,doubles));assert(!a.requested_background&&a.requested_keys==a.view.next_key);
        a.view.next_background=false;a.view.next_key={};keys.reset();keys.set(116);keys.set(117);
        assert(control.press(keys,0));assert(a.requested_background&&a.requested_keys[0]);
        control.refresh(0);assert(p.desired_led(113)==2&&p.desired_led(114)==0);
        keys.set(113);assert(control.press(keys,0));assert(a.takes==0); // no on-air commands from a chord
    }
    Panel panel; MemoryStore store; Config config; Configuration configuration(store,config); Setup setup(panel,configuration);
    {
        Panel p;MemoryStore memory;Configuration cfg(memory,Config{});Setup menu(p,cfg);
        std::bitset<KeyCount> key;key.set(47);menu.press(key);key.reset();key.set(164);menu.press(key);
        assert(menu.testing()&&menu.claims(126)&&menu.claims(0)&&menu.claims(31));
        Snapshot state;Changes changes;changes.pressed.set(176);menu.test_inputs(state,changes);
        assert(!menu.testing()&&menu.active()&&has(p.line(1),"TEST"));
        assert(!menu.take_apply_request()&&cfg.revision()==0);
    }

    press(setup,47); press(setup,160); press(setup,160); // NETWORK, static
    press(setup,161); press(setup,160); type_number(setup,"10"); press(setup,156); press(setup,176); // IP first octet; no premature validation
    assert(store.writes==0);
    press(setup,163); press(setup,160); type_number(setup,"10"); press(setup,156); press(setup,176); // gateway matches new network
    press(setup,176); assert(store.writes==1&&!store.saved.dhcp&&store.saved.ip[0]==10&&store.saved.gateway[0]==10);
    press(setup,161); press(setup,160); press(setup,164); press(setup,176); // kavtor in draft
    assert(configuration.saved().backend==Backend::kavtor);
    press(setup,162); press(setup,160); type_number(setup,"8080"); store.fail=true; press(setup,156);
    assert(has(panel.line(0),"SAVE FAILED")&&configuration.saved().active().port==9100);
    assert(configuration.saved().endpoints[unsigned(Backend::obs)].port==0);
    assert(configuration.saved().endpoints[unsigned(Backend::obs)].host[0]==192);
    store.fail=false; press(setup,176); assert(store.saved.active().port==8080&&store.saved.backend==Backend::kavtor);
    assert(store.saved.endpoints[unsigned(Backend::obs)].port==0);
    auto web=configuration.saved(); const auto revision=configuration.revision(); web.active().port=9090;
    assert(configuration.save(web,revision)==SaveResult::saved);
    assert(configuration.save(config,revision)==SaveResult::conflict);
    assert(configuration.saved().active().port==9090);
    {
        Panel p; FakeMixer a; TransitionControl tr(p,a);
        MemoryStore memory; Config c; Configuration cfg(memory,c); Setup ui(p,cfg);
        auto key=[&](unsigned id){std::bitset<KeyCount> b;b.set(id);tr.press(b);};
        auto clean=[&](){for(const auto& control:Controls) if(control.id>=128&&control.id<=156) assert(p.desired_led(control.id)==0);};
        key(121);key(153);tr.refresh();assert(p.desired_led(136)==2&&p.desired_led(153)==2);
        key(169);key(160);tr.refresh();clean();assert(p.desired_led(160)==2);
        key(156);tr.refresh();assert(p.desired_led(136)==2&&p.desired_led(153)==2);
        key(160);tr.refresh();clean();key(176);tr.refresh();assert(p.desired_led(153)==2);
        const auto saved_soft=tr.soft_value();
        key(160);key(137);key(153);key(160);tr.refresh();
        assert(tr.soft_value()==saved_soft&&p.desired_led(160)==0&&p.desired_led(136)==2&&p.desired_led(153)==2);
        key(176); // leave SOFT, retaining WIPE
        press(ui,47);press(ui,161);press(ui,162);press(ui,160);
        tr.refresh(false);ui.refresh_keypad();clean();assert(p.desired_led(160)==2);
        press(ui,156);tr.refresh(false);ui.refresh_keypad();
        assert(p.desired_led(136)==2&&p.desired_led(153)==2&&p.desired_led(160)==0);
        press(ui,160);tr.refresh(false);ui.refresh_keypad();clean();
        press(ui,176);tr.refresh(false);ui.refresh_keypad();assert(p.desired_led(153)==2);
        press(ui,160);type_number(ui,"1234");press(ui,160);
        tr.refresh(false);ui.refresh_keypad();
        assert(!ui.owns_keypad()&&p.desired_led(160)==0&&p.desired_led(136)==2&&p.desired_led(153)==2);
        press(ui,160);ui.refresh_keypad();assert(p.desired_digits()[0]==0); // discarded port draft
        press(ui,160);assert(!ui.owns_keypad());
    }
    {
        Panel p; MemoryStore memory; Config c; Configuration cfg(memory,c); Setup ui(p,cfg);
        press(ui,47);press(ui,161);press(ui,162); // open PORT, without selecting F1
        assert(!ui.owns_keypad()&&p.desired_led(160)==0);
        type_number(ui,"123");
        std::array<int32_t,6> delta{};delta[0]=5;ui.rotate(delta);
        assert(!ui.owns_keypad()&&p.desired_led(160)==0);
        press(ui,160);ui.refresh_keypad();assert(ui.owns_keypad()&&p.desired_digits()[0]==5);
        type_number(ui,"8080");press(ui,156);
        assert(!ui.owns_keypad()&&p.desired_led(160)==0);
        press(ui,176);press(ui,176);assert(cfg.saved().active().port==8080);
    }
    // Confirmed state, not command dispatch, drives tally. SHIFT shares source slots across buses.
    FakeMixer adapter; Mappings mappings; assert(valid_mappings(mappings));
    MixerControl mixer(panel,adapter,mappings); adapter.view.connected=adapter.view.studio=true;
    adapter.view.sources=24;
    adapter.view.preview=1; adapter.view.program=2; mixer.refresh();
    assert(panel.desired_led(1)==1&&panel.desired_led(18)==2);
    adapter.view.transitioning=true; adapter.view.both_sources=true; mixer.refresh();
    assert(panel.desired_led(1)==2&&panel.desired_led(18)==2);
    adapter.view.both_sources=false; mixer.refresh();
    assert(panel.desired_led(1)==1&&panel.desired_led(18)==2);
    adapter.view.preview=0; adapter.view.transitioning=false; mixer.refresh();
    assert(panel.desired_led(0)==1&&panel.desired_led(1)==0); // descending ID, shared bank
    adapter.view.preview=1; mixer.refresh();
    std::bitset<KeyCount> keys; keys.set(3); assert(mixer.press(keys));
    mixer.refresh(); assert(adapter.target==3&&panel.desired_led(3)==0&&panel.desired_led(1)==1);
    keys.reset(); keys.set(19); assert(mixer.press(keys));
    assert(adapter.action==MixerAction::program&&adapter.target==3);
    keys.reset(); keys.set(80); Changes shift_down; shift_down.pressed=keys; mixer.shift(shift_down); mixer.refresh();
    assert(mixer.second_layer()&&panel.desired_led(80)==2&&panel.desired_led(1)==1);
    keys.reset(); keys.set(0); assert(mixer.press(keys)&&adapter.target==12);
    keys.reset();keys.set(16);assert(mixer.press(keys));
    assert(adapter.action==MixerAction::program&&adapter.target==12);
    adapter.view.preview=12; adapter.view.program=13; mixer.refresh();
    assert(panel.desired_led(0)==1&&panel.desired_led(17)==2);
    assert(panel.desired_period(0)==500&&panel.desired_period(17)==500);
    Changes shift_up; shift_up.released.set(80); mixer.shift(shift_up); mixer.refresh(301);
    assert(!mixer.second_layer()&&panel.desired_led(80)==1);
    assert(panel.desired_led(0)==1&&panel.desired_period(0)==500);
    assert(panel.desired_led(17)==2&&panel.desired_period(17)==500);
    adapter.view.preview=0;mixer.refresh(302);assert(panel.desired_led(80)==1); // upper PGM
    adapter.view.program=1;mixer.refresh(303);assert(panel.desired_led(80)==0);
    adapter.view.preview=12;mixer.refresh(304);assert(panel.desired_led(80)==1); // upper PST
    adapter.view.studio=false;mixer.refresh(305);assert(panel.desired_led(80)==0);
    adapter.view.studio=true;adapter.view.connected=false;mixer.refresh(306);assert(panel.desired_led(80)==0);
    adapter.view.connected=true;adapter.view.program=13;
    adapter.view.transitioning=true; mixer.refresh(); assert(panel.desired_led(0)==1&&panel.desired_period(0)==500);
    adapter.view.both_sources=true; mixer.refresh(); assert(panel.desired_led(0)==2&&panel.desired_period(0)==500);
    adapter.view.both_sources=false;
    adapter.view.transitioning=false;
    shift_down.double_click.set(80); mixer.shift(shift_down); mixer.shift(shift_up); mixer.refresh();
    assert(mixer.second_layer()&&panel.desired_led(80)==2); // double click locks after release
    adapter.view.connected=false; mixer.refresh(); assert(panel.desired_led(0)==0&&panel.desired_led(17)==0&&panel.desired_led(80)==2);
    assert(!mixer.press(keys));
    adapter.view.connected=true; adapter.view.sources=0; keys.reset(); keys.set(127);
    assert(!mixer.press(keys)); // read-only connection cannot CUT without mapped sources
    mappings.buttons[80]={MixerAction::cut,0}; assert(!valid_mappings(mappings));
    {
        Panel p; FakeMixer a; Mappings m; MixerControl bus(p,a,m);
        Inputs inputs; Snapshot s; inputs.update(s,true);
        auto event=[&](uint32_t ms,bool held) { s.ms=ms;s.held[80]=held;bus.shift(inputs.update(s,false),ms);bus.refresh(ms); };
        event(100,true);assert(bus.second_layer());
        event(150,false);assert(!bus.second_layer()&&p.desired_led(80)==2);
        event(250,true);assert(p.desired_led(80)==2);event(300,false);assert(bus.second_layer()&&p.desired_led(80)==2);
        event(700,true);assert(!bus.second_layer()&&p.desired_led(80)==0); // single press unlocks immediately
        event(750,false);assert(!bus.second_layer());
        event(1200,true);event(1250,false);event(1600,true);event(1650,false);
        assert(!bus.second_layer()); // slow clicks do not lock
        bus.refresh(1901);assert(p.desired_led(80)==0);
        event(2000,true);event(2050,false);event(2100,true);event(2150,false);
        assert(bus.second_layer());bus.sync_shift(false);bus.refresh(2151);assert(!bus.second_layer()&&p.desired_led(80)==0);
        event(3000,true);bus.refresh(3500);event(3600,false);assert(p.desired_led(80)==0); // long hold: no release delay
        bus.sync_shift(false);Changes down;down.pressed.set(80);Changes up;up.released.set(80);
        bus.shift(down,0xfffffff0u);bus.shift(up,20);bus.refresh(100);assert(p.desired_led(80)==2);
        bus.refresh(400);assert(p.desired_led(80)==0); // timer wrap
    }
    {
        Panel p;
        struct Frames:FakeMixer {
            bool frame_rates() const override { return true; }
            uint32_t pushed=0,ftb_frames=0;
            bool set_rate(uint32_t frames) override { pushed=frames; return true; }
            bool keyers() const override { return true; }
            bool fade_to_black(uint32_t frames) override { ftb_frames=frames; return true; }
        } a;
        TransitionControl tr(p,a);
        auto key=[&](unsigned id){std::bitset<KeyCount> b;b.set(id);return tr.press(b);};
        tr.defaults(TransitionSettings{});
        key(128); tr.refresh();
        const std::array<uint8_t,7> rate25={5,2,15,15,15,15,1};
        assert(p.desired_digits()==rate25&&tr.duration(0)==25);
        key(145); key(156); assert(tr.duration(0)==1&&a.pushed==1);
        key(128); key(145); key(153); key(153); key(153); key(153); key(156); assert(tr.duration(1)==25);
        a.view.auto_frames=40; a.view.rate_known=true; a.view.rate_revision=1;
        tr.refresh(); assert(tr.duration(0)==40);
        key(128); key(128); tr.refresh();
        const std::array<uint8_t,7> rate40={0,4,15,15,15,15,1};
        assert(p.desired_digits()==rate40);
        key(128); key(128); key(140); key(146); key(153); key(156);
        assert(tr.duration(2)==20&&tr.duration(0)==40);
        key(115); assert(a.ftb_frames==20&&a.pushed==1);
    }
    {
        Panel p; FakeMixer a; TransitionControl tr(p,a);
        auto key=[&](unsigned id){std::bitset<KeyCount> b;b.set(id);return tr.press(b);};
        tr.refresh(); assert(p.desired_led(120)==2&&p.desired_led(121)==0&&p.desired_led(122)==0);
        const std::array<uint8_t,7> blank={15,15,15,15,15,15,0};
        assert(p.desired_digits()==blank);
        key(128);tr.refresh();
        const std::array<uint8_t,7> rate300={0,0,3,15,15,15,1};
        assert(p.desired_digits()==rate300);
        tr.refresh(false);assert(p.desired_digits()==rate300); // SYSTEM SETUP does not take the keypad
        key(176);tr.refresh();assert(p.desired_digits()==blank);
        key(136);key(153);tr.refresh();
        const std::array<uint8_t,7> wipe23={3,2,15,15,15,15,0};
        assert(p.desired_digits()==wipe23);
        key(126);tr.refresh();assert(p.desired_digits()==wipe23&&p.desired_led(136)==2);
        key(121);tr.refresh();assert(p.desired_led(120)==0&&p.desired_led(121)==2);
        key(122);tr.refresh();assert(p.desired_led(121)==0&&p.desired_led(122)==2);
        key(128);key(145);key(146);key(153);key(153);key(156);
        assert(tr.duration(0)==1200);key(126);assert(a.transition==TransitionType::dme&&a.rate==1200);
        tr.refresh();assert(p.desired_led(128)==2); // AUTO does not dismiss the rate editor
        key(128);key(137);key(153);key(153);key(156);assert(tr.duration(1)==400&&tr.duration(0)==1200);
        key(176);key(128);tr.refresh();assert(p.desired_digits()[6]==2); // reopen DSK
        key(121);tr.refresh();assert(p.desired_led(136)==2&&p.desired_led(128)==0);
        key(126);tr.refresh();assert(p.desired_led(136)==2); // type WIPE opens a persistent editor
        key(128);tr.refresh();assert(p.desired_digits()[6]==2); // WIPE did not forget DSK
        key(128);key(145);key(156);assert(tr.duration(2)==300); // rejects 1 ms
        key(140);key(137);key(153);key(153);key(156);assert(tr.duration(2)==400);
        key(128);assert(tr.duration(0)==1200);key(176);
        key(136);key(154);tr.refresh();assert(p.desired_led(121)==2&&p.desired_led(154)==2);
        key(126);assert(a.transition==TransitionType::stinger&&a.rate==1200);
        key(120);key(121);assert(tr.type()==TransitionType::stinger); // remembered WIPE subtype
        key(136);key(155);key(145);key(156);assert(tr.type()==TransitionType::wipe); // WIPE family stays armed; DIRECT chooses its subtype
        key(121);key(126);assert(a.transition==TransitionType::wipe&&a.code==1);
        constexpr unsigned ids[]={153,145,146,147,137,138,139,129,130,131};
        constexpr unsigned codes[]={23,5,21,24,18,9,6,1,3,17};
        for(unsigned i=0;i<10;i++) {key(136);key(160);key(ids[i]);tr.refresh();assert(p.desired_led(ids[i])==2);key(126);assert(a.code==codes[i]);}
        key(178); tr.refresh(); assert(p.desired_led(178)==2&&p.desired_led(179)==2&&p.desired_led(177)==0);
        key(126); assert(!a.reversed); ++a.view.completed_auto; a.view.busy=false;tr.refresh();
        assert(p.desired_led(178)==2&&p.desired_led(177)==2&&p.desired_led(179)==0);
        key(126); assert(a.reversed);a.view.busy=false;tr.refresh(); // failed take: do not alternate
        key(126); assert(a.reversed);++a.view.completed_auto;a.view.busy=false;tr.refresh();
        key(126);assert(!a.reversed);
        key(177);key(126);assert(a.reversed);key(179);key(126);assert(!a.reversed);
        tr.refresh();assert(p.desired_led(178)==2&&p.desired_led(179)==2);
        key(177);++a.view.completed_auto;a.view.busy=false;tr.refresh();
        assert(p.desired_led(178)==2&&p.desired_led(177)==2); // explicit next sense wins over completion
        key(178);tr.refresh();assert(p.desired_led(178)==0&&p.desired_led(177)==2);
        key(126);++a.view.completed_auto;a.view.busy=false;tr.refresh();
        assert(p.desired_led(177)==2); // fixed REV after leaving alternation
        key(169);tr.refresh();assert(p.desired_led(169)==2);
        const auto previous_keypad=p.desired_digits();
        key(160);tr.refresh();assert(p.desired_led(160)==2);
        tr.refresh(false);assert(p.desired_led(169)==1); // another menu owns the LCD
        key(146);key(137);key(156);assert(tr.soft_value()==24);
        tr.refresh();assert(p.desired_led(160)==0&&p.desired_digits()==previous_keypad);
        tr.rotary(1);assert(tr.soft_value()==25);key(126);assert(a.softness==25);
        tr.refresh();assert(p.desired_led(169)==2); // AUTO retains a manually opened editor
        key(120);tr.refresh();assert(p.desired_led(169)==1); // persists outside WIPE
        key(169);tr.rotary(1000);assert(tr.soft_value()==100);
        key(160);key(145);key(153);key(145);key(156);assert(tr.soft_value()==100); // rejects 101
        key(140);key(153);key(156);assert(tr.soft_value()==0);
        tr.rotary(-1);assert(tr.soft_value()==0);key(176);tr.refresh();assert(p.desired_digits()==blank);
        assert(p.desired_led(169)==0);
        tr.defaults(TransitionSettings{});assert(tr.soft_value()==3);
        Inputs gestures;Snapshot snap;gestures.update(snap,true);
        auto soft_event=[&](uint32_t ms,bool held) {
            snap.ms=ms;snap.held[169]=held;auto c=gestures.update(snap,false);
            tr.modifiers(c,ms,true);tr.press(c.pressed,c.double_click);tr.advance_modifiers(ms,true);
        };
        soft_event(100,true);soft_event(150,false);soft_event(250,true);soft_event(300,false);
        tr.advance_modifiers(700,true);tr.refresh();
        assert(tr.soft_value()==0&&p.desired_led(169)==0&&!tr.owns_lcd()); // no delayed focus steal
        key(176);tr.refresh();assert(p.desired_led(169)==0);
        key(128);key(145);key(146); // incomplete AUTO rate draft: 12
        key(169);assert(tr.duration(0)==300); // another menu discards the draft
        tr.rotary(7);tr.refresh();
        assert(p.desired_led(160)==0);
        key(160);key(137);key(153);key(156);tr.refresh();
        assert(tr.soft_value()==40&&p.desired_led(160)==0);
        key(176);key(153);key(153);key(156);assert(tr.duration(0)==300);
    }
    {
        Panel p; FakeMixer a; TransitionControl tr(p,a); Inputs input; Snapshot s; input.update(s,true);
        auto event=[&](uint32_t ms,bool held,bool allowed=true) {
            s.ms=ms;s.held[169]=held;auto change=input.update(s,false);
            tr.modifiers(change,ms,allowed);tr.press(change.pressed,change.double_click);
            tr.advance_modifiers(ms,allowed);tr.refresh(allowed);
        };
        std::bitset<KeyCount> key;key.set(128);tr.press(key);tr.refresh();
        const auto lcd=p.line(0);const auto digits=p.desired_digits();
        event(100,true);assert(p.line(0)==lcd&&p.desired_digits()==digits);
        event(150,false);event(250,true);event(300,false);tr.advance_modifiers(800,true);tr.refresh();
        assert(tr.soft_value()==0&&p.line(0)==lcd&&p.desired_digits()==digits);
        event(1000,true);event(1050,false);tr.advance_modifiers(1301,true);tr.refresh();
        assert(p.desired_led(169)==2); // a single click still opens SOFT
        tr.rotary(25);event(2000,true);event(2050,false);event(2200,true);event(2250,false);
        assert(tr.soft_value()==0&&p.desired_led(169)==2); // code 0 is also a local wipe
        tr.rotary(20);p.lcd(0,"SYSTEM SETUP");
        event(3000,true,false);event(3050,false,false);event(3200,true,false);event(3250,false,false);
        assert(tr.soft_value()==0&&has(p.line(0),"SYSTEM SETUP"));
        // Navigation cancels a pending single click instead of opening SOFT later.
        event(4000,true);Changes navigation;navigation.pressed.set(128);tr.modifiers(navigation,4100,true);
        tr.press(navigation.pressed);tr.refresh();const auto next=p.line(0);
        tr.advance_modifiers(4500,true);tr.refresh();assert(p.line(0)==next);
    }
    {
        Panel p; FakeMixer a; TransitionControl tr(p,a);
        auto key=[&](unsigned id){std::bitset<KeyCount> b;b.set(id);tr.press(b);tr.refresh();};
        key(121);key(137); // preset 4, code 18
        const auto preset=p.desired_digits();
        key(155);key(153);key(156); // invalid direct code 0
        key(155);assert(p.desired_led(155)==1&&p.desired_led(137)==2&&p.desired_digits()==preset);
        key(126);assert(a.code==18&&a.transition==TransitionType::wipe);
        key(154);key(155);key(146);key(145);key(156); // confirm DIRECT 21 from USER WIPE
        key(126);assert(a.code==21&&a.transition==TransitionType::wipe);
        key(155);assert(tr.type()==TransitionType::wipe&&p.desired_led(154)==1&&p.desired_led(155)==1&&p.desired_led(137)==2);
        key(126);assert(a.transition==TransitionType::wipe&&a.code==18);
        key(129);key(155);key(145);key(155); // a new preset replaces the remembered one; discard draft
        assert(p.desired_led(129)==2);key(126);assert(a.code==1);
        key(155);key(145);key(130);key(156); // commit direct 18
        key(155);key(153); // preset 23
        key(155);assert(p.desired_led(155)==2&&p.desired_digits()[0]==8&&p.desired_digits()[1]==1);
        key(121);key(126);assert(a.code==18&&a.transition==TransitionType::wipe);
    }
    {
        Panel p;FakeMixer a;TransitionControl tr(p,a);
        auto key=[&](unsigned id){std::bitset<KeyCount> b;b.set(id);tr.press(b);tr.refresh();};
        key(121);key(153); // preset 23, so a later digit would retarget the wipe
        key(169);key(160);key(145);key(137); // SOFT draft 14, not wipe preset 5
        assert(tr.soft_value()==3&&p.desired_led(156)==1&&p.desired_period(156)==500);
        key(156);assert(tr.soft_value()==14&&p.desired_led(156)==0);
        key(126);assert(a.softness==14&&a.code==23);
    }
    {
        Panel p;FakeMixer a;TransitionControl tr(p,a);
        std::bitset<KeyCount> key;key.set(128);tr.press(key);tr.refresh();
        const auto old_digits=p.desired_digits();
        Changes first;first.pressed.set(169);tr.modifiers(first,100,true);
        tr.advance_modifiers(500,true);tr.refresh();assert(p.desired_led(169)==2);
        Changes delayed;delayed.pressed.set(169);delayed.double_click.set(169);
        tr.modifiers(delayed,600,true);tr.refresh();
        assert(tr.soft_value()==0&&!tr.owns_lcd()&&p.desired_digits()==old_digits&&p.desired_led(169)==0);
        key.reset();key.set(123);tr.press(key);assert(a.action==MixerAction::dsk_mix&&a.rate==300);
        key.reset();key.set(124);tr.press(key);assert(a.action==MixerAction::dsk_cut);
        a.view.connected=a.view.dsk_known=a.view.dsk_on=a.view.dsk_mixing=true;tr.refresh();
        assert(p.desired_led(123)==2&&p.desired_led(124)==2);
        a.view.dsk_mixing=false;tr.refresh();assert(p.desired_led(123)==0&&p.desired_led(124)==2);
        a.view.connected=false;tr.refresh();assert(p.desired_led(123)==0&&p.desired_led(124)==0);
    }
    {
        Panel p;FakeMixer a;Mappings m;MixerControl bus(p,a,m);
        for(unsigned i=0;i<3;i++) {
            std::bitset<KeyCount> key;key.set(63-i);assert(bus.press_output(key));
            assert(a.action==MixerAction::output&&a.target==i);
            a.view.connected=true;a.view.output_known[i]=true;a.view.output_active[i]=true;
            bus.refresh();assert(p.desired_led(63-i)==2);
            a.view.output_active[i]=false;bus.refresh();assert(p.desired_led(63-i)==0);
            m.buttons[63-i]={MixerAction::cut,0};assert(!valid_mappings(m));m.buttons[63-i]={};
        }
        std::bitset<KeyCount> chord;chord.set(61);chord.set(63);const auto count=a.requests;
        assert(bus.press_output(chord)&&a.requests==count);
        a.view.output_active.fill(true);a.view.connected=false;bus.refresh();
        for(unsigned id=61;id<=63;id++) assert(p.desired_led(id)==0);
    }
    {
        AnalogCalibration cal;assert(cal.valid());
        cal.tbar={10,0,4000};assert(cal.valid());assert(cal.tbar.normalize(0,false)==0&&cal.tbar.normalize(4095,false)==4095);
        cal.x={12,140,240};assert(cal.valid());
        assert(cal.x.normalize(12,true)==-32767&&cal.x.normalize(140,true)==0&&cal.x.normalize(240,true)==32767);
        cal.y.center=cal.y.high;assert(!cal.valid());
        AnalogCalibration circular;circular.tbar={-3,0,1112};circular.tbar_margin=4;
        circular.x={0,129,255};circular.y={0,121,255};circular.joystick_deadband=4;
        assert(circular.valid());
        for(int raw:{4090,4092,4093,4094,4095,0,1}) assert(circular.tbar_value(raw)==0);
        for(int raw:{1108,1112,1115}) assert(circular.tbar_value(raw)==4095);
        int last=-1;
        for(int step=-8;step<=1120;step++) {
            int value=circular.tbar_value((step+4096)%4096);assert(value>=last);last=value;
        }
        for(int raw=125;raw<=133;raw++) assert(circular.x_value(raw)==0);
        for(int raw=117;raw<=125;raw++) assert(circular.y_value(raw)==0);
        assert(circular.x_value(0)==-32767&&circular.x_value(255)==32767);
        assert(circular.y_value(0)==-32767&&circular.y_value(255)==32767);
        circular.joystick_deadband=121;assert(!circular.valid());
        Fake wire;Sink sink;Session link(wire,sink);link.start(99,0);
        for(unsigned ms=0;ms<100;ms++)link.tick(ms);
        wire.drain();Frame hello;hello.type=0x81;hello.session=99;hello.length=59;hello.payload[1]=1;hello.payload[3]=128;
        wire.inject(hello);for(unsigned ms=100;ms<200;ms++)link.tick(ms);
        auto frames=wire.drain();bool negotiated=false;for(const auto& f:frames)if(f.type==7)negotiated=f.length==1&&f.payload[0]==1;
        assert(negotiated&&!link.ready());Frame ack;ack.type=0x80;ack.seq=1;ack.session=99;ack.length=1;wire.inject(ack);
        for(unsigned ms=200;ms<250;ms++)link.tick(ms);
        wire.drain();assert(link.ready()&&link.split_analog());
        Frame sample;sample.type=0x83;sample.session=99;sample.length=4;sample.seq=65535;sample.payload[2]=15;sample.payload[3]=255;
        wire.inject(sample);for(unsigned ms=250;ms<280;ms++)link.tick(ms);
        assert(sink.values[0]==4095&&sink.analogs[0]==2);
        sample.seq=0;sample.payload[2]=0;sample.payload[3]=100;wire.inject(sample);
        sample.seq=2;sample.payload[3]=102;wire.inject(sample); // dropped sample is allowed
        sample.seq=1;sample.payload[3]=101;wire.inject(sample); // delayed sample is ignored
        sample.type=0x84;sample.payload[2]=30;sample.payload[3]=240;wire.inject(sample);
        for(unsigned ms=280;ms<380;ms++)link.tick(ms);
        assert(sink.values[0]==102&&sink.analogs[0]==4&&sink.values[1]==(30*256+240)&&sink.analogs[1]==2);
        assert(sink.gaps[0]==1&&sink.gaps[1]==0);
        assert(sink.events==0&&sink.losses==0&&wire.drain().empty()); // no ACKs or button snapshots
        Frame controls;controls.type=0x85;controls.session=99;controls.seq=1;controls.length=51;
        controls.payload[4]=1;controls.payload[30]=7;wire.inject(controls);
        for(unsigned ms=380;ms<490;ms++)link.tick(ms);
        assert(sink.events==1&&sink.analogs[0]==4&&sink.analogs[1]==2);
        assert(sink.last.held[0]&&sink.last.rotary[0]==7);
        frames=wire.drain();assert(frames.size()==1&&frames[0].type==5&&frames[0].seq==1);
        Frame diag;diag.type=0x86;diag.session=99;diag.length=32;diag.payload[11]=200;wire.inject(diag);
        for(unsigned ms=490;ms<550;ms++) link.tick(ms);
        assert(sink.diagnostics==1&&sink.events==1&&sink.analogs[0]==4);
    }
    {
        Panel panel;FakeMixer adapter;adapter.view.connected=adapter.view.studio=true;
        TransitionControl control(panel,adapter);
        adapter.view.transitioning=true;adapter.view.manual_transition=true;
        control.refresh();assert(panel.desired_led(126)==0);
        adapter.view.manual_transition=false;control.refresh();assert(panel.desired_led(126)==2);
        adapter.view.transitioning=false;control.refresh();assert(panel.desired_led(126)==0);
        control.tbar(2000);assert(adapter.requests==0); // reconnect mid-travel cannot take
        control.tbar(4095);assert(adapter.requests==0);
        control.tbar(3000);assert(adapter.target==1095&&adapter.requests==1);
        control.tbar(2000);assert(adapter.target==2095);
        control.tbar(4095);assert(adapter.target==0); // return cancels
        adapter.view.busy=false;control.tbar(4095);
        control.tbar(2000);assert(adapter.target==2095);
        control.tbar(0);assert(adapter.target==4095);
        const auto count=adapter.requests;control.tbar(200);assert(adapter.requests==count); // terminal latched
        adapter.view.busy=false;control.tbar(1000);assert(adapter.target==1000&&adapter.requests==count+1); // return stroke starts without another endpoint sample
        control.reset_tbar();adapter.view.busy=false;control.tbar(2500);
        assert(adapter.target==1000); // new acquisition needs endpoint
    }
    {
        struct PickupMixer:FakeMixer {
            bool manual_me_pickup() const override {return true;}
            bool me_delegation() const override {return true;}
        };
        Panel p;PickupMixer a;a.view.connected=a.view.studio=true;
        TransitionControl tr(p,a);
        tr.tbar(0);tr.tbar(2000);
        a.view.manual_transition=a.view.transitioning=true;a.view.manual_position=2000;
        const auto started=a.requests;
        a.view.me=1;a.view.busy=a.view.manual_transition=a.view.transitioning=false;
        tr.refresh();tr.tbar(3000);assert(a.requests==started);
        tr.advance_modifiers(250,true);tr.refresh();assert((p.desired_indicators()&0x30)==0x30);
        tr.advance_modifiers(500,true);tr.refresh();assert((p.desired_indicators()&0x30)==0);
        tr.tbar(4095);tr.refresh();assert(p.desired_indicators()&0x30);
        a.view.me=0;a.view.busy=a.view.manual_transition=a.view.transitioning=true;
        a.view.manual_position=2000;tr.refresh();tr.tbar(2500);assert(a.requests==started);
        tr.tbar(1900);assert(a.requests==started+1&&a.target==1900); // Crossing picks up.
        tr.tbar(0);assert(a.target==0); // Returning to origin cancels.
        a.view.busy=a.view.manual_transition=a.view.transitioning=false;tr.tbar(0);
        tr.tbar(2000);a.view.manual_transition=a.view.transitioning=true;a.view.manual_position=2000;
        a.view.me=1;a.view.busy=a.view.manual_transition=a.view.transitioning=false;tr.refresh();
        tr.tbar(0); // Acquire the other M/E's endpoint.
        a.view.me=0;a.view.busy=a.view.manual_transition=a.view.transitioning=true;tr.refresh();
        tr.tbar(0);assert(a.target==0); // Origin reached before pickup: cancel at cut.
        a.view.busy=a.view.manual_transition=a.view.transitioning=false;tr.tbar(0);tr.tbar(2000);
        a.view.manual_transition=a.view.transitioning=true;a.view.manual_position=2000;
        a.view.me=1;a.view.busy=a.view.manual_transition=a.view.transitioning=false;tr.refresh();tr.tbar(4095);
        a.view.me=0;a.view.busy=a.view.manual_transition=a.view.transitioning=true;tr.refresh();
        tr.tbar(4095);assert(a.target==4095); // Opposite endpoint before pickup: complete at cut.
    }
    {
        Panel p;FakeMixer a;a.view.connected=a.view.studio=true;TransitionControl tr(p,a);
        tr.tbar(20);assert(a.requests==0); // acquire within the smaller endpoint allowance
        tr.tbar(32);assert(a.requests==0); // release hysteresis
        tr.tbar(33);assert(a.requests==1&&a.target==33);
        tr.tbar(4074);assert(a.target==4074);
        tr.tbar(4075);assert(a.target==4095);
        auto count=a.requests;tr.tbar(4080);assert(a.requests==count);
        a.view.busy=false;tr.tbar(4080);assert(a.requests==count);
        tr.tbar(4062);assert(a.requests==count+1&&a.target==33);
        tr.tbar(21);assert(a.target==4074);
        tr.tbar(20);assert(a.target==4095);
        count=a.requests;a.view.busy=false;tr.tbar(32);assert(a.requests==count);
        tr.tbar(33);assert(a.requests==count+1&&a.target==33);
        tr.tbar(20);assert(a.target==0);

    }
    {
        struct DirectDme:FakeMixer {unsigned first_direct_dme_code() const override{return 0;}bool valid_dme_code(unsigned code)const override{return code<3;}};
        Panel p;DirectDme a;TransitionControl tr(p,a);
        auto key=[&](unsigned id,bool dbl=false){std::bitset<KeyCount> b,d;b.set(id);if(dbl)d.set(id);tr.press(b,d);tr.refresh();};
        key(121);key(137);const auto preset=p.desired_digits();
        key(154);assert(p.desired_led(154)==2&&p.desired_led(145)==2&&p.desired_digits()[0]==1);
        key(146);assert(tr.type()==TransitionType::stinger&&p.desired_led(146)==2&&p.desired_led(145)==1);
        key(126);assert(a.transition==TransitionType::stinger&&a.code==2);
        key(153);key(126);assert(a.code==0&&p.desired_led(153)==2);
        key(154);assert(p.desired_led(137)==2&&p.desired_digits()==preset);
        key(155);key(146); // unfinished DIRECT draft 2
        key(154);key(154);assert(p.desired_led(155)==1&&p.desired_led(154)==1&&p.desired_digits()==preset&&p.desired_led(156)==0);
        key(155);key(146);key(145);key(156);key(126);assert(a.code==21&&a.transition==TransitionType::wipe);
        key(122);assert(tr.type()==TransitionType::dme&&p.desired_led(136)==2&&p.desired_period(136)==500&&p.desired_led(153)==2);
        key(145);key(126);assert(a.transition==TransitionType::slide&&p.desired_led(145)==2&&p.desired_led(153)==1);
        key(146);key(126);assert(a.transition==TransitionType::swipe&&p.desired_led(146)==2);
        key(154);key(155);assert(tr.type()==TransitionType::dme&&p.desired_led(154)==0&&p.desired_led(155)==2);
        key(128);key(136);assert(tr.type()==TransitionType::dme&&p.desired_period(136)==0);
        key(136,true);assert(tr.type()==TransitionType::dme&&p.desired_period(136)==500);
        key(169);key(160);key(136);assert(tr.type()==TransitionType::dme&&p.desired_led(160)==0&&p.desired_period(136)==0);
        key(120);key(136,true);assert(tr.type()==TransitionType::mix&&p.desired_led(120)==2&&p.desired_period(136)==500);
    }
    {
        Panel p;FakeMixer a;TransitionControl tr(p,a);
        auto key=[&](unsigned id){std::bitset<KeyCount>b;b.set(id);tr.press(b);tr.refresh();};
        key(122);const auto mover=p.line(0);key(173);assert(p.line(0)==mover&&p.desired_led(173)==0);
        key(120);const auto mix=p.line(0);key(173);assert(p.line(0)==mix&&p.desired_led(173)==0);
        key(122);key(145);key(173);assert(tr.type()==TransitionType::slide&&has(p.line(0),"SLIDE")&&p.desired_led(173)==2&&p.desired_led(161)==2);
        const auto digits=p.desired_digits();key(160);key(126);
        assert(a.dme_params.entry==0&&!a.dme_params.inward&&p.desired_digits()==digits);
        key(176);assert(p.desired_led(173)==1&&p.desired_led(160)==0);
        key(146);key(122);key(173);assert(p.desired_led(161)==2);key(162);key(165);key(126);
        assert(a.dme_params.entry==2&&a.dme_params.inward&&p.desired_led(162)==2&&p.desired_led(165)==2);
        key(176);const auto before=p.line(0);
        Changes click;click.pressed.set(173);tr.modifiers(click,100,true);
        tr.advance_modifiers(401,true);tr.refresh(); // delayed second edge after menu opened
        click.pressed.set(173);click.double_click.set(173);tr.modifiers(click,420,true);tr.refresh();
        assert(p.line(0)==before&&p.desired_led(173)==1); // Slide still modified
        key(126);assert(a.dme_params.entry==1&&!a.dme_params.inward);
        key(145);key(122);key(126);assert(a.dme_params.entry==0); // independent stored Slide settings
        Changes reset;reset.pressed.set(173);reset.double_click.set(173);tr.modifiers(reset,700,true);tr.refresh();
        assert(p.desired_led(173)==0);key(126);assert(a.dme_params.entry==1);
        key(173);key(160);key(120);assert(p.desired_led(173)==0); // Ordinary MIX has no applicable DME modifier.
        key(122);key(173);key(161);key(176);assert(p.desired_led(173)==0);
    }
    {
        // Preparing another family cannot change what AUTO will take.
        Panel p;FakeMixer a;TransitionControl tr(p,a);
        auto key=[&](unsigned id,bool twice=false){std::bitset<KeyCount>b,d;b.set(id);if(twice)d.set(id);tr.press(b,d);tr.refresh();};
        key(136,true);key(146);key(173);assert(has(p.line(0),"SWIPE"));
        key(160);key(165);key(126);assert(a.transition==TransitionType::mix&&p.desired_led(120)==2);
        key(122);key(126);assert(a.transition==TransitionType::swipe&&a.dme_params.entry==0&&a.dme_params.inward);
        key(136);key(154);key(126);assert(a.transition==TransitionType::swipe&&p.desired_led(122)==2);
        key(121);key(126);assert(a.transition==TransitionType::stinger);
        key(154);key(137);key(126);assert(a.transition==TransitionType::wipe&&a.code==18);
        key(136,true);key(145);key(173);key(162);key(126);assert(a.transition==TransitionType::wipe&&a.code==18);
        key(122);key(126);assert(a.transition==TransitionType::slide&&a.dme_params.entry==2);
        key(136);key(155);key(145);key(130);key(156);key(155);key(153);key(155);
        assert(p.desired_digits()[0]==8&&p.desired_digits()[1]==1&&p.desired_led(156)==0);
    }
    {
        // A navigation/function key discards a draft; ENTER alone commits it.
        Panel p;FakeMixer a;TransitionControl tr(p,a);
        auto key=[&](unsigned id){std::bitset<KeyCount>b;b.set(id);tr.press(b);tr.refresh();};
        for(unsigned leave:{128u,136u,120u,121u,122u,169u,173u,47u,3u,63u,126u}) {
            TransitionControl tr(p,a);
            auto key=[&](unsigned id){std::bitset<KeyCount>b;b.set(id);tr.press(b);tr.refresh();};
            tr.defaults(TransitionSettings{});key(128);key(145);key(146);
            assert(p.desired_led(156)==1&&p.desired_period(156)==500);
            key(leave);assert(tr.duration(0)==300&&p.desired_led(156)==0);
        }
        tr.defaults(TransitionSettings{});key(128);key(145);key(146);key(153);key(156);
        assert(tr.duration(0)==120&&p.desired_led(156)==0);
        key(121);key(153);key(169);const auto saved=tr.soft_value();
        key(145);assert(tr.soft_value()==saved&&p.desired_led(160)==0&&p.desired_led(156)==0); // no automatic F1 capture
        key(160);key(146);key(138);assert(p.desired_led(156)==1);key(47);key(160);
        assert(tr.soft_value()==saved&&p.desired_digits()[0]==saved&&p.desired_led(156)==0);
        key(146);key(138);key(156);assert(tr.soft_value()==25&&p.desired_led(156)==0);
    }
    {
        // Local SOFT, global edits, copy-to-current and reset-to-neutral.
        Panel p;FakeMixer a;TransitionControl tr(p,a);
        auto key=[&](unsigned id,bool twice=false){std::bitset<KeyCount>b,d;b.set(id);if(twice)d.set(id);tr.press(b,d);tr.refresh();};
        key(121);key(153);key(169);key(161);tr.rotary(22);assert(tr.soft_value()==25);
        key(176);key(137);assert(tr.soft_value()==3);key(153);assert(tr.soft_value()==25);
        key(169);key(161);tr.rotary(7);assert(tr.soft_value()==10); // global applies to all
        key(169,true);assert(tr.soft_value()==0); // complete reset, including global
        key(176);key(137);assert(tr.soft_value()==0);
        key(153);key(169);key(162);assert(tr.soft_value()==0); // copy global switches to local editing
        tr.rotary(5);assert(tr.soft_value()==5);key(176);key(137);assert(tr.soft_value()==0);
    }
    {
        Panel p;MemoryStore memory;Config c;Configuration cfg(memory,c);Setup ui(p,cfg);
        press(ui,47);press(ui,161);press(ui,162);press(ui,160);type_number(ui,"123");ui.refresh_keypad();
        assert(p.desired_led(156)==1&&p.desired_period(156)==500);
        press(ui,128);assert(!ui.owns_keypad()&&p.desired_led(156)==0);
        press(ui,160);ui.refresh_keypad();assert(p.desired_digits()[0]==0);
        type_number(ui,"456");press(ui,156);assert(!ui.owns_keypad());
        press(ui,160);ui.refresh_keypad();assert(p.desired_digits()[0]==6);
        type_number(ui,"789");press(ui,47);assert(!ui.active()&&cfg.saved().active().port==456&&p.desired_led(156)==0);
    }
    {
        Panel p;FakeMixer a;Mappings m;MixerControl control(p,a,m);
        a.view.connected=a.view.studio=true;a.view.sources=24;
        for(bool shift:{false,true})for(int layer:{0,12}) {
            control.sync_shift(shift);a.view.preview=2+layer;a.view.program=3+layer;control.refresh();
            assert(p.desired_led(2)==1&&p.desired_led(19)==2);
            assert(p.desired_period(2)==(layer?500u:0u)&&p.desired_period(19)==(layer?500u:0u));
            std::bitset<KeyCount> b;b.set(1);control.press(b);assert(a.target==(shift?13u:1u));
        }
    }
    {
        // The LCD keeps the confirmed SOFT value; only 7seg shows the draft.
        Panel p;FakeMixer a;TransitionControl tr(p,a);
        auto key=[&](unsigned id){std::bitset<KeyCount>b;b.set(id);tr.press(b);tr.refresh();};
        key(121);key(153);key(169);
        assert(has(p.line(1),"GLOBAL")&&has(p.line(1),"RSTGL"));
        key(161);assert(has(p.line(1),"CUSTOM"));
        for(bool global:{false,true}) {
            if(global)key(161);
            const auto confirmed=tr.soft_value();const auto lcd=p.line(1);
            key(160);key(146);key(138); // draft 25
            assert(p.line(1)==lcd&&tr.soft_value()==confirmed);
            assert(p.desired_digits()[0]==5&&p.desired_digits()[1]==2&&p.desired_period(156)==500);
            key(156);assert(tr.soft_value()==25&&p.line(1)!=lcd&&p.desired_led(156)==0);
            const auto committed=p.line(1);
            key(160);key(147);key(153); // draft 30
            assert(p.line(1)==committed&&tr.soft_value()==25);
            key(160);assert(p.line(1)==committed&&tr.soft_value()==25&&p.desired_led(156)==0);
            tr.rotary(-5);tr.refresh(); // global starts from a different confirmed value
        }
    }
    {
        Panel p;FakeMixer a;TransitionControl tr(p,a);
        auto key=[&](unsigned id){std::bitset<KeyCount>b;b.set(id);tr.press(b);tr.refresh();};
        key(121);key(153);key(169);tr.rotary(22);tr.refresh(); // GLOBAL 25
        assert(tr.soft_value()==25&&has(p.line(1),"GLOBAL"));
        key(161);tr.rotary(10);key(176);key(137);key(169);tr.rotary(20); // CUSTOM 35 and 45
        Changes first;first.pressed.set(169);tr.modifiers(first,100,true);
        Changes second;second.pressed.set(169);second.double_click.set(169);tr.modifiers(second,220,true);tr.refresh();
        assert(tr.soft_value()==0&&has(p.line(1),"GLOBAL"));
        key(160);assert(p.desired_digits()[0]==0);key(176);key(176);
        key(153);assert(tr.soft_value()==0); // another custom wipe was also reset
        key(137);assert(tr.soft_value()==0);key(169);key(162);assert(tr.soft_value()==0); // RSTGL also retrieves zero
    }
    {
        struct StyleMixer:FakeMixer { bool wipe_modifiers() const override { return true; } };
        Panel p; StyleMixer a; TransitionControl tr(p,a);
        auto key=[&](unsigned id,bool dbl=false){
            std::bitset<KeyCount> b,d; b.set(id); if(dbl) d.set(id); tr.press(b,d); tr.refresh();
        };
        key(171);
        assert(p.desired_digits()[0]==1);
        key(160); assert(p.desired_digits()[0]==2&&p.desired_led(156)==0);
        key(160); assert(p.desired_digits()[0]==4);
        key(160); assert(p.desired_digits()[0]==9);
        key(160); assert(p.desired_digits()[0]==6&&p.desired_digits()[1]==1);
        key(160); assert(p.desired_digits()[0]==1);
        key(160); key(160,true); assert(p.desired_digits()[0]==1);
        tr.rotary(1); tr.refresh(); assert(p.desired_digits()[0]==2);
    }
    IPv4 ip; assert(!parse_ip("1.2.3.999",ip)); assert(!parse_ip("1.2.3",ip));
    config.mask={255,0,255,0}; assert(!valid_config(config));
    {
        std::istringstream v1("1 0 2 4455\n192.168.1.147\n255.255.255.0\n192.168.1.1\n1.1.1.1\n127.0.0.1\n");
        Config migrated; assert(parse_settings(v1,migrated));
        assert(migrated.backend==Backend::obs&&migrated.endpoints[unsigned(Backend::obs)].port==4455);
        assert(migrated.endpoints[unsigned(Backend::obs)].host[0]==127);
        assert(migrated.endpoints[unsigned(Backend::kavtor)].port==9100);
        assert(migrated.endpoints[unsigned(Backend::kavtor)].host[0]==127);
        std::ostringstream stored; assert(write_settings(stored,migrated));
        Config again; std::istringstream v2(stored.str()); assert(parse_settings(v2,again)&&same_config(migrated,again));
        Config profiles=migrated;profiles.select_server(1);profiles.backend=Backend::atem;profiles.active().host={192,168,1,92};profiles.select_server(2);profiles.backend=Backend::atem;profiles.active().host={192,168,1,93};profiles.select_server(0);assert(profiles.backend==Backend::obs&&profiles.active().port==4455);
        std::ostringstream bank;assert(write_settings(bank,profiles));Config restored;std::istringstream bank_in(bank.str());assert(parse_settings(bank_in,restored)&&same_config(profiles,restored));restored.select_server(1);assert(restored.backend==Backend::atem&&restored.active().host[3]==92);restored.select_server(2);assert(restored.active().host[3]==93);
        migrated.backend=Backend::kavtor; migrated.endpoints[unsigned(Backend::kavtor)].port=9100;
        assert(migrated.endpoints[unsigned(Backend::obs)].port==4455);
    }
    assert(panel.led(0,1)); assert(!panel.led(1,2)); // shared bus color
    assert(!panel.led(250,1)); assert(panel.led(162,2,1000));
    std::cout<<"C++ core PASS: baud negotiation/fallback/wrap, codec, partial I/O, retries, dedup, close, gestures, LCD patch, soft keys, encoders, EXIT save, validation\n";
}
