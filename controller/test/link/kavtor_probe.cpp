#include "link/kavtor_posix.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using namespace bkds::link;
namespace {
int listen_on(uint16_t& port) {
    const int fd=::socket(AF_INET,SOCK_STREAM,0);
    assert(fd>=0);
    sockaddr_in address{};
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(::bind(fd,reinterpret_cast<sockaddr*>(&address),sizeof address)==0);
    assert(::listen(fd,1)==0);
    socklen_t len=sizeof address;
    assert(::getsockname(fd,reinterpret_cast<sockaddr*>(&address),&len)==0);
    port=ntohs(address.sin_port);
    return fd;
}
bool wait_for(KavtorAdapter& adapter,bool connected) {
    for(int i=0;i<50;i++) {
        if(adapter.state().connected==connected) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}
}
int main() {
    uint16_t port=0;
    const int server=listen_on(port);
    std::atomic<bool> stop{false};
    std::mutex received_lock;
    std::string received;
    std::thread peer([&]{
        const int client=::accept(server,nullptr,nullptr);
        if(client<0) return;
        const char* state="{\"event\":\"state\",\"preview\":1,\"program\":2,\"transitioning\":false,\"dsk\":true}\n";
        if(::send(client,state,std::strlen(state),MSG_NOSIGNAL)<=0) { ::close(client); return; }
        while(!stop) {
            pollfd wait{client,POLLIN,0};
            if(::poll(&wait,1,50)<=0) continue;
            char data[512];
            const auto n=::recv(client,data,sizeof data,0);
            if(n<=0) break;
            std::lock_guard<std::mutex> lock(received_lock);
            received.append(data,std::size_t(n));
            
        }
        ::close(client);
    });
    Config config;
    config.backend=Backend::kavtor;
    config.active().host={127,0,0,1};
    config.active().port=port;
    KavtorAdapter adapter;
    adapter.configure(config);
    assert(wait_for(adapter,true));
    const auto live=adapter.state();
    assert(live.studio&&live.sources==8&&live.preview==1&&live.program==2&&live.dsk_known&&live.dsk_on);
    assert(adapter.request(MixerAction::preview,3));
    bool sent=false;
    for(int i=0;i<50&&!sent;i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::lock_guard<std::mutex> lock(received_lock);
        sent=received.find("\"cmd\":\"pvw\"")!=std::string::npos&&received.find("\"source\":3")!=std::string::npos;
    }
    assert(sent);
    assert(adapter.wipe_style(2,0,1,1,500,500,false,false));
    bool style_sent=false;
    for(int i=0;i<50&&!style_sent;i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        std::lock_guard<std::mutex> lock(received_lock);
        style_sent=received.find("\"multi\":2")!=std::string::npos;
    }
    assert(style_sent);
    assert(!adapter.wipe_style(3,0,1,1,500,500,false,false));
    // A peer keeping TCP open without answering state requests is offline too.
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
    while(adapter.state().connected&&std::chrono::steady_clock::now()<deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(!adapter.state().connected);
    stop=true;
    ::shutdown(server,SHUT_RDWR);
    peer.join();
    ::close(server);
    config.backend=Backend::obs;
    adapter.configure(config);
    assert(wait_for(adapter,false));
    std::cout<<"KAVTOR PROBE OK\n";
    return 0;
}
