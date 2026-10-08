#include "link/obs_posix.hpp"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <thread>
#include <iostream>
using namespace bkds::link;
template<class F> void wait(F f) {
    auto until=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    while(!f()&&std::chrono::steady_clock::now()<until) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    assert(f());
}
int main(int argc,char** argv) {
    assert(argc==2);Config c;c.active().port=uint16_t(std::atoi(argv[1]));c.active().host={127,0,0,1};
    ObsAdapter obs("",{});obs.configure(c);
    wait([&]{return obs.state().output_known[2];});
    assert(!obs.state().studio&&obs.state().output_active[1]); // existing recording
    for(unsigned i=0;i<3;i++) for(unsigned j=0;j<2;j++) {
        auto before=obs.state().output_active[i];assert(obs.output(OutputKind(i)));
        assert(!obs.output(OutputKind(i)));
        wait([&]{return !obs.state().busy&&obs.state().output_active[i]!=before;});
        assert(obs.state().connected);
    }
    // Rejected start never becomes a lit tally and does not disconnect.
    assert(obs.output(OutputKind::stream));wait([&]{return !obs.state().busy;});
    assert(obs.state().connected&&!obs.state().output_active[0]);
    // Server now changes virtual camera externally, then disconnects and reconnects.
    wait([&]{return obs.state().output_active[2];});
    wait([&]{return !obs.state().connected;});assert(!obs.state().output_known[2]);
    wait([&]{return obs.state().connected&&obs.state().output_known[2];});
    assert(obs.state().output_active[2]);
    std::cout<<"OUTPUTS PASS: initial state, start/stop, refusal, external event, reconnect without replay\n";
}
