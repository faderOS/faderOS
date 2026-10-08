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
template<class F> void wait_for(F fn){auto until=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!fn()&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(5));assert(fn());}
int main(){int listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);assert(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof address)==0);assert(listen(listener,1)==0);socklen_t length=sizeof address;assert(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&length)==0);
    std::atomic<bool> stop=false;std::atomic<unsigned> calls=0;
    std::thread peer([&]{int fd=accept(listener,nullptr,nullptr);assert(fd>=0);bool swapped=false;
        auto send_json=[&](Json value){auto text=value.dump()+"\n";assert(send(fd,text.data(),text.size(),MSG_NOSIGNAL)==ssize_t(text.size()));};
        auto sources=[&]{return Json::array({Json{{"source",swapped?7:6},{"prepared",true},{"name","Preview SS"},{"boxes",Json::array({Json{{"id","window"},{"name","Window"},{"button",0},{"input",0}}})}},Json{{"source",swapped?6:7},{"prepared",true},{"name","Program SS"},{"boxes",Json::array({Json{{"id","window"},{"name","Window"},{"button",0},{"input",1}}})}}});};
        auto state=[&]{send_json(Json{{"event","state"},{"me",0},{"preview",swapped?7:6},{"program",swapped?6:7},{"transitioning",true},{"capabilities",{{"supersources",true}}},{"supersources",sources()}});};
        state();std::string pending;
        while(!stop){pollfd ready{fd,POLLIN,0};if(poll(&ready,1,50)<=0)continue;char buffer[2048];auto n=recv(fd,buffer,sizeof buffer,0);if(n<=0)break;pending.append(buffer,size_t(n));size_t end;
            while((end=pending.find('\n'))!=std::string::npos){auto value=Json::parse(pending.substr(0,end));pending.erase(0,end+1);auto command=value.value("cmd",std::string{});
                if(command=="supersource_input"){unsigned call=++calls;assert(value.at("button")==0&&value.at("box")=="window"&&value.at("me")==0);
                    assert(value.at("bus")== (call==3?"program":"preview"));assert(value.at("source")== (call==1?6:call==3?6:7));if(call==4)assert(value.at("input")==1001);
                    if(call==1){send_json(Json{{"event","error"},{"cmd","supersource_input"},{"message","Rejected window change"}});swapped=true;send_json(Json{{"event","tally"},{"preview",7},{"program",6},{"supersources",sources()}});}else send_json(Json{{"event","ack"},{"cmd",command}});
                }else if(command=="hello"||command=="state")state();
            }
        }close(fd);
    });
    Config config;config.backend=Backend::kavtor;config.active().host={127,0,0,1};config.active().port=ntohs(address.sin_port);KavtorAdapter adapter;adapter.configure(config);
    wait_for([&]{auto view=adapter.state();return view.connected&&view.supersources_supported&&view.supersources[0].windows[0].mapped;});
    assert(adapter.state().busy);assert(adapter.set_supersource_input(0,0,2));wait_for([&]{return calls==1&&adapter.state().command_failures==1&&adapter.state().supersources[0].source==7;});assert(adapter.state().busy);
    assert(adapter.set_supersource_input(0,0,3));wait_for([&]{return calls==2;});assert(adapter.set_supersource_input(1,0,4));wait_for([&]{return calls==3;});
    assert(!adapter.set_supersource_input(0,1,0));assert(!adapter.set_supersource_input(0,0,7));
    assert(adapter.set_supersource_input(0,0,1001));wait_for([&]{return calls==4;});assert(!adapter.set_supersource_input(0,0,1004));
    stop=true;adapter.deactivate();peer.join();close(listener);std::cout<<"KAVTOR SUPERSOURCE PASS: bounded metadata, tally swap, explicit bus context, errors preserve main take\n";
}
