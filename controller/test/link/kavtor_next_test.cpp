#include "link/kavtor_posix.hpp"
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
using namespace bkds::link;using Json=nlohmann::json;
template<class F> void wait_for(F fn){auto until=std::chrono::steady_clock::now()+std::chrono::seconds(4);while(!fn()&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(5));assert(fn());}
int main(){
    int listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(bind(listener,reinterpret_cast<sockaddr*>(&addr),sizeof addr)==0);assert(listen(listener,1)==0);socklen_t len=sizeof addr;assert(getsockname(listener,reinterpret_cast<sockaddr*>(&addr),&len)==0);
    std::atomic<bool> stop=false;std::atomic<unsigned> next_count=0,on_air_commands=0,preview_commands=0,rehearsals=0;
    std::thread peer([&]{int fd=accept(listener,nullptr,nullptr);assert(fd>=0);bool transitioning=false;bool transition_preview=false;bool background=true;std::array<bool,4> keys{true,true,false,false};
        auto state=[&]{auto text=Json({{"event","state"},{"transitionPreview",transition_preview},{"me",0},{"preview",1},{"program",2},{"transitioning",transitioning},{"take","mix"},{"keys",Json::array({Json{{"on",true},{"source",0}},Json{{"on",false},{"source",1}}})},{"next",{{"background",background},{"keys",keys}}}}).dump()+"\n";assert(send(fd,text.data(),text.size(),MSG_NOSIGNAL)==ssize_t(text.size()));};
        state();std::string pending;
        while(!stop){pollfd p{fd,POLLIN,0};if(poll(&p,1,50)<=0)continue;char bytes[1024];auto n=recv(fd,bytes,sizeof bytes,0);if(n<=0)break;pending.append(bytes,size_t(n));size_t end;
            while((end=pending.find('\n'))!=std::string::npos){auto line=Json::parse(pending.substr(0,end));pending.erase(0,end+1);auto cmd=line.value("cmd",std::string());
                if(cmd=="next"){
                    if(line.at("target")=="background")background=!background;else{auto i=line.at("slot").get<unsigned>();assert(i<2);keys[i]=!keys[i];}
                    assert(background||keys[0]||keys[1]);auto count=++next_count;if(count==1)std::this_thread::sleep_for(std::chrono::milliseconds(150));state();
                }else if(cmd=="mix"&&transition_preview){transitioning=true;state();std::this_thread::sleep_for(std::chrono::milliseconds(100));transitioning=false;state();++rehearsals;}else if(cmd=="trans_preview"){transition_preview=line.at("on").get<bool>();++preview_commands;state();}else if(cmd=="state"||cmd=="hello")state();else if(cmd=="key_on"||cmd=="cut"||cmd=="mix")++on_air_commands;
            }
        }close(fd);
    });
    Config config;config.backend=Backend::kavtor;config.active().host={127,0,0,1};config.active().port=ntohs(addr.sin_port);KavtorAdapter adapter;adapter.configure(config);
    wait_for([&]{return adapter.state().next_known;});auto before=adapter.state();assert(before.key_on[0]&&before.key_available[0]&&!before.key_available[2]);
    std::array<bool,4> keys{true,true,false,false};assert(adapter.set_next_transition(false,keys));wait_for([&]{return next_count>0;});
    assert(adapter.set_next_transition(true,{}));wait_for([&]{return !adapter.state().busy;});
    auto live=adapter.state();assert((live.next_background&&live.next_key==std::array<bool,4>{}&&live.key_on==before.key_on&&live.program==before.program&&live.preview==before.preview));
    keys={false,false,true,false};assert(!adapter.set_next_transition(false,keys));assert(!adapter.set_next_transition(false,{}));
    assert(adapter.supports_transition_preview());
    assert(adapter.set_transition_preview(true));wait_for([&]{return adapter.state().transition_preview;});
    const auto web=Json::parse(adapter.web_status());assert(web.at("transitionPreview")==true&&web.at("transitionPreviewSupported")==true);
    const auto completed=adapter.state().completed_auto;
    assert(adapter.automatic(TransitionType::mix,0,0,false,0));
    wait_for([&]{return adapter.state().transitioning;});
    assert(!adapter.state().both_sources);
    wait_for([&]{return rehearsals==1&&!adapter.state().busy;});
    assert(adapter.state().completed_auto==completed);
    assert(adapter.set_transition_preview(false));wait_for([&]{return !adapter.state().transition_preview;});
    assert(preview_commands==2);
    assert(on_air_commands==0&&next_count==4);stop=true;peer.join();close(listener);
    std::cout<<"KAVTOR NEXT PASS: coalesced BKGD reset, all arms cleared, on-air keys and buses preserved\n";
}
