#include "link/obs_posix.hpp"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
using namespace bkds::link;
template<class F> void wait(F condition) {
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    while(!condition()&&std::chrono::steady_clock::now()<end) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(condition());
}
int main(int argc,char** argv) {
    assert(argc==3||argc==4); Mappings mappings; std::array<std::string,24> scenes;
    assert(load_mappings(argv[2],mappings,scenes));
    const auto original=scenes;
    assert(!load_mappings(std::string(argv[2])+".invalid",mappings,scenes));
    assert(scenes==original); // rejected profile never partially replaces live bindings
    Config c; c.active().host={127,0,0,1}; c.active().port=uint16_t(std::atoi(argv[1]));
    ObsAdapter obs(argc==4?"wrong-password":"test-password",scenes); obs.configure(c);
    if(argc==4) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
        assert(obs.state().auth_required);
        assert(!obs.state().connected&&!obs.request(MixerAction::cut,0));
        std::cout<<"OBS PASS: wrong password cannot control the mixer\n"; return 0;
    }
    wait([&]{return obs.state().connected;});
    ServerInfo info;assert(obs.server_info(info));assert(info.fields[1].second=="32.2.0"&&info.fields[2].second=="5.6.3");
    assert(obs.state().studio&&obs.state().program==1&&obs.state().preview==0);
    assert(!obs.state().available[23]); // configured name absent from OBS
    assert(!obs.request(MixerAction::preview,23));
    assert(obs.request(MixerAction::preview,13));
    assert(obs.state().preview==0); // queued != confirmed
    assert(!obs.request(MixerAction::cut,0)); // one outstanding operation
    wait([&]{return obs.state().preview==13;});
    assert(obs.state().busy); // event published before delayed RPC response
    wait([&]{auto s=obs.state(); return !s.busy&&s.preview==13;});
    assert(!obs.request(MixerAction::program,23));
    assert(obs.request(MixerAction::program,0));
    wait([&]{auto s=obs.state();return !s.busy&&s.program==0;});
    assert(obs.state().preview==13);
    assert(obs.request(MixerAction::cut,0));
    wait([&]{auto s=obs.state(); return !s.busy&&s.program==13&&s.preview==0;});
    wait([&]{return obs.state().transitioning;});
    assert(!obs.request(MixerAction::program,0));
    wait([&]{return !obs.state().transitioning;});
    wait([&]{return obs.state().program==2;}); // external operator update
    wait([&]{return !obs.state().connected;});
    assert(!obs.request(MixerAction::cut,0));
    wait([&]{return obs.state().connected;}); // resync, not replay
    assert(obs.state().program==2);
    std::cout<<"OBS PASS: auth, explicit shared sources, missing scene, confirmed tally, CUT, external update, reconnect\n";
}
