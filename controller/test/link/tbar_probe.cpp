#include "link/obs_posix.hpp"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <iostream>
using namespace bkds::link;
template<class F> void wait(F f) {
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    while(!f()&&std::chrono::steady_clock::now()<end) std::this_thread::sleep_for(std::chrono::milliseconds(3));
    assert(f());
}
int main(int argc,char** argv) {
    assert(argc==2);Config c;c.active().host={127,0,0,1};c.active().port=std::atoi(argv[1]);
    std::array<std::string,24> scenes{};scenes[0]="A";scenes[1]="B";
    ObsAdapter obs("",scenes);obs.configure(c);wait([&]{return obs.state().connected;});
    for(unsigned pass=0;pass<3;pass++) {
        auto before=obs.state();auto type=pass==1?TransitionType::wipe:TransitionType::mix;
        assert(obs.manual(1000,type,0,pass==1,25));
        wait([&]{return obs.state().transitioning;});
        assert(obs.state().manual_transition);
        assert(!obs.request(MixerAction::cut,0));
        for(unsigned i=1001;i<=3000;i++)assert(obs.manual(i,type)); // coalesced burst
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        assert(obs.manual(pass==2?0:4095,type));
        assert(obs.manual(2048,type)); // cannot overwrite a pending terminal
        wait([&]{return !obs.state().busy;});
        assert(obs.state().connected&&!obs.state().transitioning&&!obs.state().manual_transition);
        if(pass<2) assert(obs.state().program==before.preview&&obs.state().preview==before.program);
        else assert(obs.state().program==before.program&&obs.state().preview==before.preview);
        assert(obs.state().completed_auto==before.completed_auto+(pass<2));
    }
    auto before=obs.state();assert(obs.manual(2000,TransitionType::mix));
    wait([&]{return obs.state().transitioning;});obs.cancel_manual();
    wait([&]{return !obs.state().busy;});assert(obs.state().connected&&obs.state().program==before.program);
    assert(!obs.manual(1000,TransitionType::stinger));
    // Shutdown must release an active transition before closing the socket.
    assert(obs.manual(2000,TransitionType::mix));wait([&]{return obs.state().transitioning;});
    std::cout<<"TBAR PASS: mix/wipe, coalescing, terminal latch, completion/cancel, conflict, shutdown\n";
}
