#include "link/obs_posix.hpp"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>
using namespace bkds::link;
template<class F> void wait(F f) {
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    while(!f()&&std::chrono::steady_clock::now()<end) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    assert(f());
}
int main(int argc,char** argv) {
    assert(argc==2);Config c;c.active().port=uint16_t(std::atoi(argv[1]));c.active().host={127,0,0,1};
    ObsAdapter obs("",{});obs.configure(c);wait([&]{return obs.state().connected;});
    assert(!obs.dsk(true,0));
    for(bool mix:{true,true,false,false}) {
        const bool was_on=obs.state().dsk_on;
        assert(obs.dsk(mix,400));assert(!obs.dsk(mix,400));
        if(mix) {wait([&]{return obs.state().dsk_mixing;});assert(obs.state().dsk_on);}
        wait([&]{return !obs.state().busy;});
        assert(obs.state().connected&&obs.state().dsk_known&&!obs.state().dsk_mixing&&obs.state().dsk_on!=was_on);
    }
    TransitionSettings settings;
    assert(!obs.dsk(false,400,1)); // unassigned second channel
    std::strcpy(settings.dsk2_name.data(),"Other DSK");std::strcpy(settings.dsk2_scene.data(),"Overlay B");obs.set_transitions(settings);
    for(bool mix:{true,true,false,false}) {
        const bool was_on=obs.state().dsk2_on;
        assert(obs.dsk(mix,400,1));
        if(mix) {wait([&]{return obs.state().dsk2_mixing;});assert(obs.state().dsk2_on&&!obs.state().dsk_mixing);}
        wait([&]{return !obs.state().busy;});
        assert(obs.state().dsk2_known&&obs.state().dsk2_on!=was_on&&!obs.state().dsk2_mixing&&!obs.state().dsk_on);
    }
    settings.dsk2_name.fill(0);settings.dsk2_scene.fill(0);
    std::strcpy(settings.dsk_name.data(),"Other DSK");std::strcpy(settings.dsk_scene.data(),"Overlay B");obs.set_transitions(settings);
    assert(obs.dsk(false,500));wait([&]{return !obs.state().busy;});assert(obs.state().dsk_on);
    std::strcpy(settings.dsk_scene.data(),"Missing");obs.set_transitions(settings);
    assert(obs.dsk(false,500));wait([&]{return !obs.state().busy;}); // OFF needs no target
    assert(obs.dsk(true,500));wait([&]{return !obs.state().busy;});assert(obs.state().connected&&!obs.state().dsk_on);
    std::strcpy(settings.dsk_name.data(),"Tied");obs.set_transitions(settings);
    assert(obs.dsk(true,500));wait([&]{return !obs.state().busy;});assert(obs.state().connected);
    std::strcpy(settings.dsk_name.data(),"Reject selection");std::strcpy(settings.dsk_scene.data(),"DSK1");obs.set_transitions(settings);
    assert(obs.dsk(true,500));wait([&]{return !obs.state().busy;});assert(obs.state().connected&&!obs.state().dsk_known);
    std::strcpy(settings.dsk_name.data(),"Missing keyer");obs.set_transitions(settings);
    assert(obs.dsk(true,500));wait([&]{return !obs.state().busy;});assert(obs.state().connected);
    std::cout<<"DSK PASS: MIX in/out, cut in/out, mapping, busy, retained effects and rejections\n";
}
