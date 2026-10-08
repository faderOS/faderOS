#include "link/kavtor_posix.hpp"
#include "link/transitions.hpp"
#include <nlohmann/json.hpp>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
#include <iostream>
using namespace bkds::link;
using Json=nlohmann::json;
template<class F> void wait_for(F fn) {
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(4);
    while(!fn()&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(5));
    assert(fn());
}
int main() {
    int listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);
    sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(bind(listener,reinterpret_cast<sockaddr*>(&addr),sizeof addr)==0);assert(listen(listener,1)==0);
    socklen_t len=sizeof addr;assert(getsockname(listener,reinterpret_cast<sockaddr*>(&addr),&len)==0);
    std::atomic<bool> stop=false;
    std::atomic<unsigned> soft_amount=999,aspect_w=0,aspect_h=0;
    std::atomic<unsigned> samples=0,last_mix=0,last_dme=0,last_wipe=0;
    std::atomic<unsigned> sony_dme_manual=0;
    std::atomic<bool> image_background=false,broadcast=false,global_backgrounds=false,last_background_global=false;std::atomic<unsigned> gain_a=100,gain_b=100,dip_rgb=0;
    std::atomic<bool> move_manual=false,cube_manual=false,page_manual=false;std::atomic<unsigned> backgrounds=0;
    std::atomic<bool> sony_namespace=false,advertise_dip=false,advertise_dme=false,dip_manual=false,dip_auto=false;
    std::thread peer([&] {
        int fd=accept(listener,nullptr,nullptr);assert(fd>=0);
        bool automatic=false,preview=false;
        unsigned me=0;std::array<int,4> pos{};std::array<bool,4> active{};
        std::array<int,4> pgm{{0,0,0,0}},pvw{{1,1,1,1}};
        std::array<Json,4> keys;std::array<Json,2> dsks;Json bg={{"cube",-1},{"global",-1}},bg_scopes=Json::object();
        for(auto& row:keys)row={{"source",0},{"on",false},{"processing",{{"mode","linear"},{"mask",false}}}};
        for(auto& row:dsks)row={{"source",0},{"on",false},{"processing",{{"mode","linear"},{"mask",false}}}};
        auto state=[&] {
            if(image_background)bg["cube"]=Json{{"image","/media/background.png"}};
            Json effective=bg;if(global_backgrounds)for(const auto& effect:{"move","cube","zoom","page_curl","page_roll"})if(!bg_scopes.value(effect,false))effective[effect]=bg["global"];
            auto data=Json({{"event","state"},{"me",me},{"program",pgm[me]},{"preview",pvw[me]},
                {"manual",active[me]&&!automatic},{"position",pos[me]},{"transitioning",active[me]},
                {"keys",keys},{"dsks",dsks},{"capabilities",{{"mixPreparation",true},{"broadcastMixes",broadcast.load()},{"dmeBackground",true},{"dmeBackgroundScopes",global_backgrounds.load()},{"sonyDmes",Json::array({1001,1002,1003,1004,2601,2602,2603,2604})},{"keyInversion",broadcast.load()},{"keyModes",broadcast?Json::array({"linear","chroma","luma"}):Json::array({"linear","chroma"})},{"mixModes",(broadcast?Json::array({"mix","dip","vfade","fadecut","cutfade","nam","supermix"}):advertise_dip?Json::array({"mix","dip","vfade","fadecut","cutfade"}):Json::array({"mix","vfade","fadecut","cutfade"}))},{"dmeEffects",(advertise_dme?Json::array({"push","slide","move","cube","zoom","page_curl","page_roll"}):Json::array({"push","slide"}))},{"sonyWipes",Json::array({1,3,5,6,9,17,18,21,23,24})}}},
                {"superMixGainA",gain_a.load()},{"superMixGainB",gain_b.load()},{"dipColor","#000000"},{"dmeBackgrounds",effective},{"dmeBackgroundScopes",bg_scopes},{"colorSources",Json::array({2})},{"transitionPreview",preview},{"take","mix"},{"next",{{"background",true},{"keys",Json::array({false,false,false,false})}}}}).dump()+"\n";
            assert(send(fd,data.data(),data.size(),MSG_NOSIGNAL)==ssize_t(data.size()));
        };
        state();std::string pending;
        while(!stop) {
            pollfd p{fd,POLLIN,0};if(poll(&p,1,50)<=0)continue;
            char data[8192];auto count=recv(fd,data,sizeof data,0);if(count<=0)break;
            pending.append(data,size_t(count));size_t end;
            while((end=pending.find('\n'))!=std::string::npos) {
                auto line=Json::parse(pending.substr(0,end));pending.erase(0,end+1);
                auto cmd=line.value("cmd",std::string());
                if(cmd=="manual") {
                    if(line.value("mode",std::string())=="dip")dip_manual=true;
                    if(line.value("type",std::string())=="dme"){if(line.contains("sony")){sony_dme_manual=line.at("sony");assert(!line.contains("effect")&&!line.contains("direction"));}else if(line.at("effect")=="move")move_manual=true;else if(line.at("effect")=="cube"){assert(line.at("reverse")==true);cube_manual=true;}else {assert(line.at("effect")=="page_curl"||line.at("effect")=="page_roll");page_manual=true;}}
                    if(line.value("type",std::string())=="wipe") {assert(line.contains("sony"));last_wipe=line.at("sony").get<unsigned>();sony_namespace=true;}
                    pos[me]=line.at("position").get<int>();++samples;
                    active[me]=pos[me]>0&&pos[me]<4095;
                    if(pos[me]==4095)std::swap(pgm[me],pvw[me]);
                    state();
                } else if(cmd=="mix"||cmd=="dme"||cmd=="wipe") {
                    if(cmd=="mix") {
                        const auto mode=line.value("mode",std::string("mix"));
                        if(mode=="dip")dip_auto=true;
                        last_mix=mode=="mix"?1:mode=="dip"?5:mode=="vfade"?2:mode=="fadecut"?3:mode=="cutfade"?4:0;
                    } else if(cmd=="wipe") {
                        sony_namespace=line.contains("sony");last_wipe=line.at(sony_namespace?"sony":"smpte").get<unsigned>();
                    } else {
                        if(line.contains("sony")){last_dme=line.at("sony");assert(!line.contains("effect")&&!line.contains("direction"));}else if(line.at("effect")=="move")last_dme=10;else if(line.at("effect")=="cube")last_dme=9;else if(line.at("effect")=="page_curl")last_dme=13;else if(line.at("effect")=="page_roll")last_dme=14;else {
                        const auto direction=line.at("direction").get<std::string>();
                        last_dme=(line.at("effect")=="slide"?4u:0u)+(direction=="left"?1:direction=="right"?2:direction=="top"?3:4); }
                    }
                    automatic=true;active[me]=true;state();
                    std::swap(pgm[me],pvw[me]);active[me]=false;automatic=false;state();
                } else if(cmd=="mix_params") {gain_a=line.at("aGain");gain_b=line.at("bGain");state();}
                else if(cmd=="dip_color") {dip_rgb=std::stoul(line.at("color").get<std::string>().substr(1),nullptr,16);state();}
                else if(cmd=="dme_background") {
                    const auto effect=line.at("effect").get<std::string>();
                    if(line.contains("custom")){bool custom=line.at("custom");if(custom&&(line.value("copyGlobal",false)||!bg.contains(effect)))bg[effect]=bg["global"];bg_scopes[effect]=custom;state();}
                    else if(line.at("source")==6){auto failure=Json{{"event","error"},{"cmd",cmd},{"message","Rejected background"}}.dump()+"\n";assert(send(fd,failure.data(),failure.size(),MSG_NOSIGNAL)==ssize_t(failure.size()));}
                    else {bg[effect]=line.at("source");if(effect!="global")bg_scopes[effect]=true;last_background_global=effect=="global";++backgrounds;state();}
                } else if(cmd=="key_processing") {
                    const unsigned slot=line.at("slot").get<unsigned>();
                    auto& row=line.at("target")=="dsk"?dsks.at(slot):keys.at(slot);
                    row["processing"].update(line.at("settings"));state();
                } else if(cmd=="trans_preview") {preview=line.at("on").get<bool>();state();}
                else if(cmd=="wipe_style") {aspect_w=line.at("aspectW").get<unsigned>();aspect_h=line.at("aspectH").get<unsigned>();}
                else if(cmd=="wipe_edge") {soft_amount=line.at("amount").get<unsigned>();}
                else if(cmd=="me") {me=line.at("slot").get<unsigned>();assert(me<4);state();}
                else if(cmd=="hello"||cmd=="state")state();
            }
        }
        close(fd);
    });
    Config config;config.backend=Backend::kavtor;config.active().host={127,0,0,1};config.active().port=ntohs(addr.sin_port);
    KavtorAdapter adapter;adapter.configure(config);wait_for([&]{return adapter.state().connected;});
    wait_for([&]{return adapter.native_key_controls();});
    assert(adapter.key_setting(0,0,91));wait_for([&]{return adapter.state().key_kind[0]==91;});
    assert(adapter.key_setting(0,0,100));wait_for([&]{return adapter.state().key_mask[0]==1;});
    assert(adapter.key_setting(4,1,91));wait_for([&]{return adapter.state().dsk_kind[1]==91;});
    assert(adapter.key_setting(0,0,89));wait_for([&]{return adapter.state().key_kind[0]==89;});
    assert(!adapter.key_setting(0,0,88)); // Never pretend that chroma is a luma key.
    assert(!adapter.key_setting(0,0,70,1)); // Separate key source is not supported yet.
    assert(adapter.wipe_aspect_percentage());
    Panel panel;TransitionControl controls(panel,adapter);
    std::bitset<KeyCount> buttons;buttons.set(170);assert(controls.press(buttons));
    controls.rotary(25);
    wait_for([&]{return aspect_w==125&&aspect_h==100;});
    controls.refresh();
    assert(std::string(panel.line(1).data(),40).find("125%")!=std::string::npos);
    controls.rotary(-50);
    wait_for([&]{return aspect_w==75&&aspect_h==100;});
    assert(adapter.set_transition_preview(true));wait_for([&]{return adapter.state().transition_preview;});
    assert(adapter.manual(2000,TransitionType::wipe,23,false,0));wait_for([&]{return adapter.state().manual_transition;});
    const unsigned held_samples=samples;
    assert(adapter.prepare_wipe_modifier(169,80));wait_for([&]{return soft_amount==32;});
    assert(samples==held_samples); // Stationary T-bar: update softness, not position.
    assert(adapter.prepare_wipe_modifier(169,0));wait_for([&]{return soft_amount==0;});
    adapter.cancel_manual();wait_for([&]{return !adapter.state().busy;});
    assert(adapter.set_transition_preview(false));wait_for([&]{return !adapter.state().transition_preview;});
    assert(!adapter.prepare_wipe_modifier(169,50));
    assert(adapter.manual(1000,TransitionType::mix,0,false,0));
    wait_for([&]{return adapter.state().transitioning;});
    assert(adapter.state().manual_transition&&adapter.state().busy);
    assert(adapter.set_me(1));wait_for([&]{return adapter.state().me==1;});
    assert(!adapter.state().busy);
    assert(adapter.set_me(0));wait_for([&]{return adapter.state().me==0;});
    assert(adapter.state().manual_transition&&adapter.state().manual_position==1000);
    const auto completed=adapter.state().completed_auto;
    for(unsigned i=1001;i<3000;++i)assert(adapter.manual(uint16_t(i),TransitionType::mix,0,false,0));
    assert(adapter.manual(4095,TransitionType::mix,0,false,0));
    assert(adapter.manual(100,TransitionType::mix,0,false,0)); // terminal cannot be overwritten
    wait_for([&]{return !adapter.state().busy;});
    assert(adapter.state().program==1&&adapter.state().completed_auto==completed+1);
    assert(samples<100); // no backlog of thousands of intermediate positions
    assert(adapter.manual(2000,TransitionType::mix,0,false,0));wait_for([&]{return adapter.state().transitioning;});
    adapter.cancel_manual();wait_for([&]{return !adapter.state().busy;});
    assert(adapter.state().program==1&&adapter.state().completed_auto==completed+1);
    wait_for([&]{return adapter.keypad_transition_slots(TransitionType::mix)==4;});
    assert(adapter.keypad_transition_slots(TransitionType::dme)==8&&adapter.supports_dme());
    assert(adapter.keypad_transition_label(TransitionType::mix,2)=="VFADE");
    for(unsigned mode=2;mode<=4;++mode) {
        assert(adapter.manual(2000,TransitionType::mix,mode,false,0));wait_for([&]{return adapter.state().transitioning;});
        adapter.cancel_manual();wait_for([&]{return !adapter.state().busy;});
        assert(adapter.automatic(TransitionType::mix,1,mode,false,0));
        wait_for([&]{return last_mix==mode&&!adapter.state().busy;});
    }
    advertise_dip=true;
    assert(adapter.set_me(1));
    wait_for([&]{return adapter.state().me==1;});
    wait_for([&]{return adapter.keypad_transition_label(TransitionType::mix,5)=="DIP";});
    assert(adapter.set_me(0));
    wait_for([&]{return adapter.state().me==0;});
    assert(adapter.manual(2000,TransitionType::mix,5,false,0));
    wait_for([&]{return dip_manual&&adapter.state().transitioning;});
    adapter.cancel_manual();wait_for([&]{return !adapter.state().busy;});
    assert(adapter.automatic(TransitionType::mix,1,5,false,0));
    wait_for([&]{return dip_auto&&!adapter.state().busy;});
    assert(!adapter.automatic(TransitionType::mix,1,6,false,0));
    assert(!adapter.manual(1000,TransitionType::mix,6,false,0));
    assert(adapter.automatic(TransitionType::dme,1,7,true,0));
    wait_for([&]{return last_dme==8&&!adapter.state().busy;}); // REV flips TOP to BOTTOM.
    assert(!adapter.automatic(TransitionType::dme,1,9,false,0));
    assert(!adapter.manual(1000,TransitionType::dme,1,false,0)); // Push/slide still use timed operation.
    assert(!adapter.keypad_transition_available(TransitionType::dme,0));
    advertise_dme=true;assert(adapter.set_me(1));wait_for([&]{return adapter.state().me==1;});
    wait_for([&]{return adapter.keypad_transition_available(TransitionType::dme,0);});
    assert(adapter.first_keypad_transition_slot(TransitionType::dme)==0&&adapter.keypad_transition_slots(TransitionType::dme)==9);
    assert(adapter.keypad_transition_label(TransitionType::dme,0)=="MOVE"&&adapter.keypad_transition_label(TransitionType::dme,9)=="CUBE");
    assert(adapter.automatic(TransitionType::dme,1,0,false,0));wait_for([&]{return last_dme==10&&!adapter.state().busy;});
    assert(adapter.manual(1000,TransitionType::dme,0,false,0));wait_for([&]{return move_manual&&adapter.state().transitioning;});adapter.cancel_manual();wait_for([&]{return !adapter.state().busy;});
    assert(adapter.manual(1000,TransitionType::dme,9,true,0));wait_for([&]{return cube_manual&&adapter.state().transitioning;});adapter.cancel_manual();wait_for([&]{return !adapter.state().busy;});
    assert(adapter.automatic(TransitionType::dme,1,9,true,0));wait_for([&]{return last_dme==9&&!adapter.state().busy;});
    for(unsigned code:{1001u,1002u,1003u,1004u,2601u,2602u,2603u,2604u}) {
        assert(adapter.valid_dme_code(code)&&!adapter.valid_wipe_code(code));
        assert(adapter.automatic(TransitionType::dme,1,code,false,0));wait_for([&]{return last_dme==code&&!adapter.state().busy;});
        assert(adapter.manual(2000,TransitionType::dme,code,false,0));wait_for([&]{return sony_dme_manual==code&&adapter.state().busy;});
        adapter.cancel_manual();wait_for([&]{return !adapter.state().busy;});
    }
    assert(!adapter.valid_dme_code(22)&&!adapter.valid_dme_code(1005));
    assert(adapter.valid_dme_code(13)&&adapter.keypad_transition_available(TransitionType::dme,13));
    assert(adapter.keypad_transition_label(TransitionType::dme,13)=="PAGE TURN");
    assert(adapter.manual(2000,TransitionType::dme,13,false,0));wait_for([&]{return page_manual&&adapter.state().busy;});adapter.cancel_manual();wait_for([&]{return !adapter.state().busy;});
    assert(adapter.automatic(TransitionType::dme,1,14,false,0));wait_for([&]{return last_dme==14&&!adapter.state().busy;});
    assert(!adapter.keypad_transition_available(TransitionType::stinger,1)); // Empty media slots stay unavailable.
    assert(adapter.supports_dme_background(9)&&adapter.state().color_sources[2]);
    assert(adapter.manual(2000,TransitionType::dme,9,true,0));wait_for([&]{return adapter.state().busy;});
    assert(adapter.set_dme_background(9,2));wait_for([&]{return backgrounds==1&&adapter.state().dme_backgrounds[1]==2;});assert(adapter.state().busy);
    auto failures=adapter.state().command_failures;assert(adapter.set_dme_background(9,6));wait_for([&]{return adapter.state().command_failures>failures;});assert(adapter.state().busy);
    adapter.cancel_manual();wait_for([&]{return !adapter.state().busy;});
    assert(adapter.valid_wipe_code(23)&&adapter.valid_wipe_code(18)&&!adapter.valid_wipe_code(999));
    assert(!adapter.manual(2000,TransitionType::wipe,999,false,0));
    assert(adapter.manual(2000,TransitionType::wipe,23,false,0));wait_for([&]{return adapter.state().transitioning&&last_wipe==23;});
    adapter.cancel_manual();wait_for([&]{return !adapter.state().busy;});
    assert(adapter.automatic(TransitionType::wipe,1,18,false,0));
    wait_for([&]{return last_wipe==18&&!adapter.state().busy;});assert(sony_namespace);
    assert(adapter.submit("{\"cmd\":\"wipe\",\"smpte\":102}"));
    wait_for([&]{return last_wipe==102&&!adapter.state().busy;});assert(!sony_namespace);
    assert(!adapter.submit("{\"cmd\":\"wipe\",\"smpte\":23,\"sony\":23}"));
    broadcast=true;assert(adapter.set_me(2));wait_for([&]{return adapter.keypad_transition_slots(TransitionType::mix)==7;});
    assert(adapter.state().key_luma_supported&&adapter.state().key_invert_supported);
    assert(adapter.key_setting(0,0,88));wait_for([&]{return adapter.state().key_kind[0]==88;});
    assert(adapter.key_setting(0,0,102));wait_for([&]{return adapter.state().key_invert[0]==1;});
    assert(adapter.key_setting(4,1,88));wait_for([&]{return adapter.state().dsk_kind[1]==88;});
    assert(adapter.key_setting(4,1,102));wait_for([&]{return adapter.state().dsk_invert[1]==1;});
    assert(adapter.key_setting(0,0,102));wait_for([&]{return adapter.state().key_invert[0]==0;});
    assert(adapter.supports_mix_preparation(true)&&adapter.keypad_transition_available(TransitionType::mix,6));
    assert(adapter.prepare_mix(true,60,80));wait_for([&]{return gain_a==60&&gain_b==80&&adapter.state().super_gain_a==60;});
    assert(!adapter.prepare_mix(true,101,80));
    assert(adapter.prepare_mix(false,0x21abcd));wait_for([&]{return dip_rgb==0x21abcd;});
    global_backgrounds=true;assert(adapter.set_me(3));wait_for([&]{return adapter.state().dme_background_scopes_supported;});
    assert(!adapter.dme_background_custom(0));assert(adapter.set_dme_background(0,2));wait_for([&]{return last_background_global&&adapter.dme_background(0)==2;});
    assert(adapter.set_dme_background_scope(0,true));wait_for([&]{return adapter.dme_background_custom(0);});
    assert(adapter.set_dme_background(0,1));wait_for([&]{return !last_background_global&&adapter.dme_background(0)==1;});
    assert(adapter.set_dme_background_scope(0,false));wait_for([&]{return !adapter.dme_background_custom(0)&&adapter.dme_background(0)==2;});
    assert(adapter.set_dme_background_scope(0,true));wait_for([&]{return adapter.dme_background_custom(0)&&adapter.dme_background(0)==1;});
    assert(adapter.set_dme_background_scope(0,true,true));wait_for([&]{return adapter.dme_background(0)==2;});
    image_background=true;wait_for([&]{return adapter.state().dme_backgrounds[1]==-3;});
    stop=true;peer.join();close(listener);
    std::cout<<"KAVTOR MANUAL PASS: coalescing, M/E freeze, terminal latch, completion and cancellation\n";
}
