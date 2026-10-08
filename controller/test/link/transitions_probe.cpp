#include "link/obs_posix.hpp"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
using namespace bkds::link;
template<class F> void wait(F f) {
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    while(!f()&&std::chrono::steady_clock::now()<end) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    assert(f());
}
int main(int argc,char** argv) {
    assert(argc==2); Config c;c.active().host={127,0,0,1};c.active().port=uint16_t(std::atoi(argv[1]));
    std::array<std::string,24> scenes{};scenes[0]="A";scenes[1]="B";
    ObsAdapter obs("",scenes);TransitionSettings settings;
    std::strcpy(settings.names[3].data(),"Stinger localized");
    std::strcpy(settings.stingers[2].data(),"Second stinger");
    std::strcpy(settings.stinger_reverse[2].data(),"Stinger localized");
    std::strcpy(settings.stingers[0].data(),"Second stinger");
    std::strcpy(settings.names[1].data(),"Wipe B");obs.set_transitions(settings);obs.configure(c);
    wait([&]{return obs.state().connected;});
    for(unsigned i=0;i<6;i++) {
        const auto old=obs.state();
        if(i==4)obs.dme_parameters({0,false});
        if(i==5)obs.dme_parameters({2,true});
        assert(obs.automatic(TransitionType(i),1234+i,i==3?1:0));
        if(i>=4)obs.dme_parameters({3,false}); // running request must keep its latched parameters
        assert(!obs.automatic(TransitionType::mix,999));
        wait([&]{return obs.state().transitioning;});
        assert(obs.state().both_sources);
        assert(obs.state().busy&&!obs.request(MixerAction::cut,0));
        if(i==3) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            assert(obs.state().transitioning&&obs.state().busy); // end before video end
        }
        wait([&]{return !obs.state().busy;});
        assert(obs.state().connected&&!obs.state().transitioning);
        assert(obs.state().program==old.preview&&obs.state().preview==old.program);
    }
    settings.wipes[0].code=9;settings.wipes[0].has_pattern=true;std::strcpy(settings.wipes[0].pattern.data(),"iris.png");obs.set_transitions(settings);
    for(bool reverse:{false,true}) {
        const auto completed=obs.state().completed_auto;
        assert(obs.automatic(TransitionType::wipe,1450,9,reverse,reverse?100:25));
        wait([&]{return !obs.state().busy;});
        assert(obs.state().connected&&obs.state().completed_auto==completed+1);
    }
    assert(obs.automatic(TransitionType::wipe,900,999));wait([&]{return !obs.state().busy;});
    assert(std::string(obs.state().message.data()).find("PATTERN NOT MAPPED")!=std::string::npos);
    assert(obs.automatic(TransitionType::wipe,900,0,true,0));wait([&]{return !obs.state().busy;});
    assert(obs.state().connected);
    assert(obs.automatic(TransitionType::wipe,900,21));wait([&]{return !obs.state().busy;});
    assert(obs.state().connected);
    assert(obs.automatic(TransitionType::wipe,900,23));wait([&]{return !obs.state().busy;});
    assert(std::string(obs.state().message.data()).find("PATTERN NOT MAPPED")!=std::string::npos);
    assert(!obs.automatic(TransitionType::wipe,900,0,false,101));
    for(unsigned slot:{2u,2u,0u}) {
        static unsigned call=0;
        const auto completed=obs.state().completed_auto;
        assert(obs.automatic(TransitionType::stinger,900,slot,call++==1));
        wait([&]{return !obs.state().busy;});
        assert(obs.state().connected&&obs.state().completed_auto==completed+1);
    }
    assert(obs.automatic(TransitionType::stinger,900,3));wait([&]{return !obs.state().busy;});
    assert(std::string(obs.state().message.data()).find("SLOT NOT MAPPED")!=std::string::npos);
    // No implicit choice between two wipes; no fallback if an explicit name vanishes.
    settings.names[1].fill(0);obs.set_transitions(settings);
    assert(obs.automatic(TransitionType::wipe,900));wait([&]{return !obs.state().busy;});
    assert(obs.state().connected&&std::string(obs.state().message.data()).find("MAP TRANSITION")!=std::string::npos);
    std::strcpy(settings.names[2].data(),"Missing");obs.set_transitions(settings);
    assert(obs.automatic(TransitionType::dme,900));wait([&]{return !obs.state().busy;});
    assert(obs.state().connected&&std::string(obs.state().message.data()).find("NOT FOUND")!=std::string::npos);
    assert(!obs.automatic(TransitionType::mix,0));
    std::strcpy(settings.names[1].data(),"Wipe B");obs.set_transitions(settings);
    assert(obs.automatic(TransitionType::wipe,1000,0,false,42));
    wait([&]{return !obs.state().connected;}); // rejected settings: restore, never fire
    std::cout<<"TRANSITIONS PASS: MIX/WIPE/DME/stinger, forced duration, busy/end/video-end, swap, missing/ambiguous mapping\n";
}
