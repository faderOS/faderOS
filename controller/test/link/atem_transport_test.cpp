#include "link/atem_transport.hpp"
#include <cassert>
#include <iostream>
using namespace bkds::link::atem;
int main(){Transport t;std::vector<Bytes> replies;auto hello=t.hello(99,0);assert(hello[12]==1);
    t.receive(packet(2,99,0,0,{2,0,0,0,0,0,0,0}),1,replies);assert(t.syn&&replies.size()==1);
    Bytes f;field(f,"InCm",{});assert(t.receive(packet(1,123,0,1,f),2,replies)==f&&t.assigned);
    assert(t.receive(packet(1,123,0,1,f),3,replies).empty()); // duplicate
    assert(t.receive(packet(1,123,0,3,f),4,replies).empty());assert((replies.back()[0]>>3)==8&&u16(replies.back().data()+6)==2);
    assert(t.receive(packet(5,123,0,2,f),5,replies)==f);assert(t.receive(packet(5,123,0,3,f),6,replies)==f);
    auto bad=packet(1,123,0,4,f);bad.pop_back();assert(t.receive(bad,7,replies).empty()&&t.last==3);
    assert(t.receive(packet(1,321,0,4,f),8,replies).empty()&&t.last==3);
    auto command=t.send(f,10);auto retry=t.tick(210);assert(u16(retry.data()+10)==u16(command.data()+10)&&retry.size()==command.size());assert(retry[0]&0x20);
    auto request=packet(8,123);put(request,6,t.pending_id);t.receive(request,211,replies);assert(u16(replies.back().data()+10)==t.pending_id);
    t.receive(packet(16,123,t.pending_id),212,replies);assert(t.pending.empty());
    t.last=32767;assert(t.receive(packet(1,123,0,0,f),213,replies)==f);
    t.next=32767;t.send(f,214);t.receive(packet(16,123,32767),215,replies);assert(t.pending.empty());auto wrap=t.send(f,216);assert(u16(wrap.data()+10)==0);t.receive(packet(16,123,0),217,replies);assert(t.pending.empty());
    bool timeout=false;try{t.tick(4000);}catch(...){timeout=true;}assert(timeout);
    std::cout<<"ATEM transport: loss, duplicate, reorder, retry identity, malformed/session filtering, sequence wrap OK\n";
}
