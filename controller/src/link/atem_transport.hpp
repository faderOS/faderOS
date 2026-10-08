#pragma once
// ATEM UDP framing. The caller owns the socket and clocks; no platform I/O here.
#include <cstdint>
#include <vector>
#include <stdexcept>
#include <algorithm>
namespace bkds::link::atem {
using Bytes=std::vector<uint8_t>;
inline uint16_t u16(const uint8_t* p){return uint16_t((unsigned(p[0])<<8)|p[1]);}
inline void put(Bytes& b,size_t at,unsigned v){b.at(at)=uint8_t(v>>8);b.at(at+1)=uint8_t(v);}
inline Bytes packet(unsigned flags,uint16_t session,uint16_t ack=0,uint16_t seq=0,const Bytes& payload={}){
    if(payload.size()>2035)throw std::runtime_error("ATEM PACKET TOO LARGE");
    Bytes b(12,0);b.insert(b.end(),payload.begin(),payload.end());put(b,0,(flags<<11)|b.size());put(b,2,session);put(b,4,ack);put(b,10,seq);return b;
}
inline void field(Bytes& b,const char* name,const Bytes& data){size_t at=b.size();b.resize(at+8);put(b,at,8+data.size());std::copy_n(name,4,b.begin()+at+4);b.insert(b.end(),data.begin(),data.end());}
inline bool covered(uint16_t ack,uint16_t seq){return ((ack-seq)&0x7fff)<0x4000;}
struct Transport {
    uint16_t session=0,last=0,next=1,pending_id=0;bool syn=false,assigned=false;
    Bytes pending;uint64_t sent_at=0,last_rx=0;unsigned retries=0;
    Bytes hello(uint16_t id,uint64_t now){*this={};session=id;last_rx=now;return packet(2,id,0,0,{1,0,0,0,0,0,0,0});}
    Bytes send(const Bytes& fields,uint64_t now){if(!assigned||!pending.empty())throw std::runtime_error("ATEM TRANSPORT BUSY");pending_id=next;next=(next+1)&0x7fff;pending=packet(1,session,0,pending_id,fields);sent_at=now;retries=0;return pending;}
    Bytes retry(uint64_t now){if(++retries>10)throw std::runtime_error("ATEM ACK TIMEOUT");sent_at=now;auto b=pending;b[0]|=0x20;return b;}
    // Deliver payload only in sequence, including after loss or duplicate packets.
    Bytes receive(const Bytes& b,uint64_t now,std::vector<Bytes>& replies){
        if(b.size()<12||(u16(b.data())&0x7ff)!=b.size())return {};
        unsigned flags=b[0]>>3;auto sid=u16(b.data()+2),seq=uint16_t(u16(b.data()+10)&0x7fff);
        if(flags&2){if(b.size()<20||sid!=session||b[12]!=2)throw std::runtime_error("ATEM HANDSHAKE REJECTED");if(assigned)throw std::runtime_error("ATEM SESSION RESTARTED");syn=true;last=seq;last_rx=now;replies.push_back(packet(16,session,last));return {};}
        if(!syn)return {};
        if(!assigned){session=sid;assigned=true;}else if(sid!=session)return {};
        last_rx=now;
        if(flags&8){auto from=uint16_t(u16(b.data()+6)&0x7fff);if(pending.empty()||from!=pending_id)throw std::runtime_error("ATEM RETRANSMIT LOST");replies.push_back(retry(now));}
        if((flags&16)&&!pending.empty()&&covered(u16(b.data()+4)&0x7fff,pending_id))pending.clear();
        if(!(flags&1))return {};
        if(seq==((last+1)&0x7fff)){last=seq;replies.push_back(packet(16,session,last));return Bytes(b.begin()+12,b.end());}
        replies.push_back(packet(16,session,last));
        if(!covered(last,seq)){auto r=packet(8,session);put(r,6,(last+1)&0x7fff);replies.push_back(std::move(r));}
        return {};
    }
    Bytes tick(uint64_t now){if(now-last_rx>3000)throw std::runtime_error("ATEM CONNECTION TIMEOUT");if(!pending.empty()&&now-sent_at>=200)return retry(now);return {};}
};
}
