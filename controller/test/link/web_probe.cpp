#include "link/web_posix.hpp"
#include "link/vmix_posix.hpp"
#include "link/atem_posix.hpp"
#include <chrono>
#include <thread>
#include <cstdlib>
#include <memory>
using namespace bkds::link;
struct MemoryStore:ConfigStore { bool save(const Config&) override { return true; } };
int main(int argc,char** argv){
    if(argc!=3&&argc!=4&&argc!=5)return 2;
    std::array<std::string,24> scenes{};Mappings m;
    ObsAdapter obs("",scenes,std::string(argv[2])+".credentials.json");
    VmixAdapter vmix(std::string(argv[2])+".vmix.json");
    AtemAdapter atem(std::string(argv[2])+".atem.json");
    WebConfig web(unsigned(std::atoi(argv[1])),argv[2],obs,nullptr,&vmix,&atem);
    if(!web.valid())return 1;
    Config initial;
    // Optional read-only hardware preview. No serial port or control commands.
    if(argc>=4){initial.backend=Backend::atem;if(!parse_ip(argv[3],initial.active().host))return 2;if(argc==5)initial.active().port=uint16_t(std::atoi(argv[4]));atem.configure(initial);}
    MemoryStore store;Configuration config(store,initial);
    std::array<std::unique_ptr<ObsAdapter>,2> others;for(unsigned i=0;i<2;i++)others[i]=std::make_unique<ObsAdapter>("",scenes,std::string(argv[2])+".server"+std::to_string(i+2)+".credentials.json");
    std::array<std::unique_ptr<VmixAdapter>,2> other_vmix;std::array<std::unique_ptr<AtemAdapter>,2> other_atem;
    for(unsigned i=0;i<2;i++){const auto suffix=".server"+std::to_string(i+2);other_vmix[i]=std::make_unique<VmixAdapter>(std::string(argv[2])+suffix+".vmix.json");other_atem[i]=std::make_unique<AtemAdapter>(std::string(argv[2])+suffix+".atem.json");}
    unsigned bound=0;
    for(;;){const unsigned selected=config.saved().server;if(selected!=bound&&web.bind_profile(std::string(argv[2])+(selected?".server"+std::to_string(selected+1):""),selected?*others[selected-1]:obs,nullptr,selected?other_vmix[selected-1].get():&vmix,selected?other_atem[selected-1].get():&atem,selected))bound=selected;web.sync_system(config,"test","test",38400,true,true,true);if(web.take(m,scenes))(bound?*others[bound-1]:obs).set_sources(scenes);std::this_thread::sleep_for(std::chrono::milliseconds(10));}
}
