#include "link/vmix_posix.hpp"
#include <nlohmann/json.hpp>
#include <cassert>
#include <chrono>
#include <thread>
#include <iostream>
using namespace bkds::link;
using Json=nlohmann::json;
template<class F> void wait(F f){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(6);while(!f()&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(10));if(!f()){std::cerr<<"Timed out\n";std::abort();}}
int main(int argc,char** argv){assert(argc>=4);Config c;c.backend=Backend::vmix;assert(parse_ip(argv[1],c.active().host));c.active().port=uint16_t(std::stoi(argv[2]));VmixAdapter v(argv[3]);v.configure(c);
    wait([&]{return v.state().connected;});auto j=Json::parse(v.web_status());std::cout<<"VMIX CONNECTED: "<<j["inputs"].size()<<" inputs\n";
    if(argc<5)return 0;
    assert(j["inputs"].size()>=2);auto profile=j["profile"];for(unsigned i=0;i<std::min<size_t>(24,j["inputs"].size());i++)profile["sources"][i]=j["inputs"][i]["key"];
    std::string error;auto older=profile;
    older["transitions"]["version"]=2;
    older["transitions"]["mix_types"].insert(older["transitions"]["mix_types"].begin()+1,"Merge");
    older["transitions"]["dme_types"].erase(older["transitions"]["dme_types"].begin());
    assert(v.save_mappings(Json({{"revision",j["revision"]},{"profile",older}}).dump(),error));
    assert(Json::parse(v.web_status())["profile"]==profile);
    assert(!v.save_mappings(Json({{"revision",j["revision"]},{"profile",profile}}).dump(),error));
    wait([&]{return v.state().available[0]&&v.state().available[1];});
    if(std::string(argv[4])=="disconnect") {
        assert(v.request(MixerAction::cut,0));
        wait([&]{return !v.state().connected;});
        wait([&]{return v.state().connected;});
        assert(v.state().program==1&&v.state().preview==0);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        assert(v.state().program==1&&v.state().preview==0);
        std::cout<<"VMIX PASS: lost reply reconnects without replaying CUT\n";return 0;
    }
    if(std::string(argv[4])=="overlay-disconnect") {
        assert(v.toggle_overlay(0,0,false));wait([&]{return !v.state().connected;});wait([&]{return v.state().connected;});
        assert(v.state().overlays[0].source==0&&v.state().overlays[0].bus==TallyBus::program);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        assert(v.state().overlays[0].source==0&&v.state().overlays[0].bus==TallyBus::program);
        std::cout<<"VMIX OVERLAY PASS: lost reply resyncs without replaying the on-air operation\n";return 0;
    }
    auto manual_test=[&]{
        for(unsigned pass=0;pass<4;pass++){
            auto before=v.state();const auto type=pass==1?TransitionType::wipe:TransitionType::mix;
            assert(!v.manual(1000,TransitionType::stinger));
            assert(v.manual(1000,type,1,pass==1));wait([&]{return v.state().transitioning;});
            assert(v.state().manual_transition&&v.state().both_sources&&!v.request(MixerAction::cut,0));
            for(unsigned i=1001;i<=4094;i++)assert(v.manual(uint16_t(i),type));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));assert(v.state().busy&&v.state().program==before.program);
            if(pass==3)v.cancel_manual();else assert(v.manual(pass==2?0:4095,type));
            assert(v.manual(2048,type)); // a terminal update must not be overwritten
            wait([&]{return !v.state().busy;});auto after=v.state();assert(after.connected&&!after.manual_transition&&!after.transitioning);
            assert(after.program==(pass<2?before.preview:before.program)&&after.preview==(pass<2?before.program:before.preview));
            assert(after.completed_auto==before.completed_auto+(pass<2));
        }
    };
    if(std::string(argv[4])=="manual"){manual_test();std::cout<<"VMIX TBAR PASS: complete both strokes, cancel, coalesce, protect endpoints\n";return 0;}
    j=Json::parse(v.web_status());profile=j["profile"];
    for(auto& st:profile["transitions"]["stingers"])st["total_ms"]=100;
    profile["transitions"]["wipes"]["23"]={{"normal","Wipe"},{"reverse","Zoom"}};
    assert(v.save_mappings(Json({{"revision",j["revision"]},{"profile",profile}}).dump(),error));
    auto bad=profile;bad["transitions"]["mix_types"][0]="Cut&Input=bad";
    assert(!v.save_mappings(Json({{"revision",Json::parse(v.web_status())["revision"]},{"profile",bad}}).dump(),error));
    wait([&]{return v.state().available[0]&&v.state().available[1];});
    const auto original=v.state();assert(original.preview>=0&&original.program>=0);
    assert(!v.request(MixerAction::preview,23));assert(!v.automatic(TransitionType::wipe,300,999));
    assert(v.request(MixerAction::preview,1));wait([&]{return !v.state().busy;});assert(v.state().preview==1);
    assert(v.request(MixerAction::program,1));wait([&]{return !v.state().busy;});assert(v.state().program==1&&v.state().preview==1);
    assert(v.request(MixerAction::preview,0));wait([&]{return !v.state().busy;});assert(v.state().preview==0);
    assert(v.request(MixerAction::cut,0));wait([&]{return !v.state().busy;});assert(v.state().program==0&&v.state().preview==1);
    const auto completed=v.state().completed_auto;
    assert(v.automatic(TransitionType::mix,350));wait([&]{return v.state().transitioning;});assert(v.state().both_sources&&!v.request(MixerAction::cut,0));
    wait([&]{return !v.state().busy;});assert(v.state().connected&&v.state().program==1&&v.state().preview==0&&v.state().completed_auto==completed+1);
    for(auto t:{TransitionType::wipe,TransitionType::dme,TransitionType::slide,TransitionType::swipe}) {
        assert(v.automatic(t,350,t==TransitionType::wipe?23:0));wait([&]{return !v.state().busy;});assert(v.state().connected);
    }
    assert(v.automatic(TransitionType::wipe,350,23,true));wait([&]{return !v.state().busy;});
    for(unsigned n=1;n<=3;n++){assert(v.automatic(TransitionType::mix,350,n));wait([&]{return !v.state().busy;});}
    for(unsigned n=0;n<=7;n++){assert(v.automatic(TransitionType::dme,350,n));wait([&]{return !v.state().busy;});}
    for(unsigned n:{2u,7u}){assert(v.automatic(TransitionType::dme,350,n,true));wait([&]{return !v.state().busy;});}
    for(unsigned n:{1u,3u,17u,18u}){assert(v.automatic(TransitionType::wipe,350,n));wait([&]{return !v.state().busy;});}
    for(unsigned n:{1u,3u}){assert(v.automatic(TransitionType::wipe,350,n,true));wait([&]{return !v.state().busy;});}
    for(unsigned n=1;n<=8;n++){assert(v.automatic(TransitionType::stinger,9000,n));wait([&]{return !v.state().busy;});}
    assert(!v.automatic(TransitionType::stinger,350,0)&&!v.automatic(TransitionType::stinger,350,9));
    assert(!v.automatic(TransitionType::dme,350,8));
    assert(v.valid_wipe_code(23)&&!v.valid_wipe_code(999));
    assert(v.request(MixerAction::program,unsigned(original.program)));wait([&]{return !v.state().busy;});
    assert(v.request(MixerAction::preview,unsigned(original.preview)));wait([&]{return !v.state().busy;});
    assert(v.state().program==original.program&&v.state().preview==original.preview);
    manual_test();
    assert(v.overlay_delegation()&&v.state().overlay_channels==8);
    assert(!v.toggle_overlay(8,0,false)&&!v.toggle_overlay(0,23,true));
    const auto overlay_buses=v.state();
    for(unsigned layer=0;layer<8;layer++) {
        auto apply=[&](unsigned source,bool preview,int expected,TallyBus bus){
            assert(v.toggle_overlay(layer,source,preview));
            assert(!v.toggle_overlay(layer,source,preview)); // one outstanding command
            wait([&]{return !v.state().busy;});const auto s=v.state();
            assert(s.connected&&s.overlays[layer].known&&s.overlays[layer].source==expected&&s.overlays[layer].bus==bus);
            assert(s.preview==overlay_buses.preview&&s.program==overlay_buses.program);
        };
        assert(v.state().overlays[layer].known&&v.state().overlays[layer].source==-1);
        apply(0,true,0,TallyBus::preview);apply(0,true,-1,TallyBus::none);
        apply(0,false,0,TallyBus::program);
        apply(1,true,1,TallyBus::preview); // native channel moves from program to preview
        apply(1,false,1,TallyBus::program); // same input, different destination: promote
        apply(1,false,-1,TallyBus::none);
        apply(0,false,0,TallyBus::program);apply(1,false,1,TallyBus::program); // replace
        apply(1,false,-1,TallyBus::none);
    }
    assert(v.toggle_overlay(7,0,true));wait([&]{return !v.state().busy;});
    assert(v.request(MixerAction::cut,0));wait([&]{return !v.state().busy;});
    assert(v.state().overlays[7].bus==TallyBus::program);
    assert(v.toggle_overlay(7,0,false));wait([&]{return !v.state().busy;});
    assert(v.request(MixerAction::cut,0));wait([&]{return !v.state().busy;});
    assert(v.state().program==overlay_buses.program&&v.state().preview==overlay_buses.preview);
    const bool black=v.state().ftb;assert(v.state().ftb_known);assert(v.fade_to_black(300));wait([&]{return !v.state().busy;});assert(v.state().ftb!=black);assert(v.fade_to_black(300));wait([&]{return !v.state().busy;});assert(v.state().ftb==black);
    v.deactivate();assert(!v.state().connected&&!v.request(MixerAction::cut,0));
    std::cout<<"VMIX PASS: GUID mappings, preview, hot punch preserves preview, CUT swap, forced MIX duration, original buses restored\n";
}
