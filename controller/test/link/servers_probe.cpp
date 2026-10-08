#include "link/atem_posix.hpp"
#include "link/adapter_router.hpp"
#include <cassert>
#include <chrono>
#include <thread>
#include <memory>
#include <iostream>
using namespace bkds::link;
template<class F> void wait_for(F f){auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!f()&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(5));assert(f());}
int main(int argc,char** argv){assert(argc==5);AdapterRouter router;std::array<std::unique_ptr<AtemAdapter>,3> servers;Config config;config.backend=Backend::atem;
for(unsigned i=0;i<3;i++){servers[i]=std::make_unique<AtemAdapter>(std::string(argv[4])+std::to_string(i));router.attach(Backend::atem,*servers[i],i);config.select_server(i);config.backend=Backend::atem;config.active().host={127,0,0,1};config.active().port=uint16_t(std::stoi(argv[i+1]));router.configure(config);}
for(unsigned i=0;i<3;i++){std::cerr<<"Connecting slot "<<i<<"\n";wait_for([&]{return servers[i]->state().connected;});}
std::cerr<<"Rate command\n";
// All three sessions remain alive; only slot 3 receives a rate command.
assert(router.set_rate(77));wait_for([&]{return !router.state().busy&&router.state().auto_frames==77;});
std::cerr<<"Inactive state update\n";wait_for([&]{return servers[1]->state().auto_frames==42;});
config.select_server(1);router.configure(config);assert(router.state().auto_frames==42);assert(servers[2]->state().auto_frames==77);config.select_server(0);router.configure(config);assert(router.state().auto_frames==25);
std::cout<<"SERVERS PASS: three simultaneous ATEM sessions, inactive state updates, command isolation, selection without reconnect\n";
}
