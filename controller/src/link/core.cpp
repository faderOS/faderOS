#include "core.hpp"
#include <algorithm>
#include <cstdio>
namespace bkds::link {
static uint16_t read16(const uint8_t* p) { return uint16_t((p[0]<<8)|p[1]); }
uint32_t read32(const uint8_t* p) { return (uint32_t(read16(p))<<16)|read16(p+2); }
static void put16(uint8_t* p,uint16_t v) { p[0]=uint8_t(v>>8); p[1]=uint8_t(v); }
static void put32(uint8_t* p,uint32_t v) { put16(p,uint16_t(v>>16)); put16(p+2,uint16_t(v)); }
static uint32_t crc(const uint8_t* p,std::size_t n) {
    uint32_t c=0xffffffff;
    while(n--) { c^=*p++; for(unsigned b=0;b<8;b++) c=(c>>1)^((c&1)?0xedb88320u:0u); }
    return ~c;
}
bool encode(const Frame& f,Wire& w) {
    if(f.length>MaxPayload) return false;
    std::array<uint8_t,206> raw{};
    raw[0]=1; raw[1]=f.type; put16(raw.data()+2,f.seq); put32(raw.data()+4,f.session);
    put16(raw.data()+8,f.length); std::copy_n(f.payload.begin(),f.length,raw.begin()+10);
    put32(raw.data()+10+f.length,crc(raw.data(),10+f.length));
    std::size_t pos=1,mark=0; uint8_t code=1;
    for(std::size_t i=0;i<14u+f.length;i++) {
        if(raw[i]) { w.bytes[pos++]=raw[i]; code++; }
        else { w.bytes[mark]=code; mark=pos++; code=1; }
    }
    w.bytes[mark]=code; w.bytes[pos++]=0; w.size=pos; return true;
}
bool decode(const uint8_t* p,std::size_t size,Frame& f) {
    std::array<uint8_t,206> raw{}; std::size_t i=0,n=0;
    while(i<size) {
        unsigned code=p[i++]; if(!code||i+code-1>size) return false;
        for(unsigned k=1;k<code;k++) { if(n==raw.size()) return false; raw[n++]=p[i++]; }
        if(code!=255&&i<size) { if(n==raw.size()) return false; raw[n++]=0; }
    }
    if(n<14||raw[0]!=1||read16(raw.data()+8)!=n-14||crc(raw.data(),n-4)!=read32(raw.data()+n-4)) return false;
    f.type=raw[1]; f.seq=read16(raw.data()+2); f.session=read32(raw.data()+4); f.length=uint16_t(n-14);
    std::copy_n(raw.begin()+10,f.length,f.payload.begin()); return true;
}
bool Decoder::feed(uint8_t byte,Frame& f) {
    if(byte) {
        if(size==bytes.size()) { discard=true; size=0; }
        if(!discard) bytes[size++]=byte;
        return false;
    }
    bool ok=!discard&&size&&decode(bytes.data(),size,f); reset(); return ok;
}
bool snapshot(const uint8_t* p,std::size_t n,Snapshot& s) {
    if(n!=55) return false;
    s.ms=read32(p);
    for(std::size_t i=0;i<KeyCount;i++) s.held[i]=(p[4+i/8]>>(i%8))&1;
    s.tbar=read16(p+27); s.x=p[29]; s.y=p[30];
    for(unsigned i=0;i<6;i++) s.rotary[i]=read32(p+31+4*i);
    return true;
}
Changes Inputs::update(const Snapshot& s,bool baseline) {
    Changes c;
    if(baseline) seen.reset();
    else {
        c.pressed=s.held&~previous.held; c.released=previous.held&~s.held;
        for(std::size_t i=0;i<KeyCount;i++) if(c.pressed[i]) {
            if(seen[i]&&uint32_t(s.ms-clicked[i])<=300) { c.double_click.set(i); seen.reset(i); }
            else { seen.set(i); clicked[i]=s.ms; }
        }
        for(unsigned i=0;i<6;i++) {
            uint32_t d=s.rotary[i]-previous.rotary[i];
            c.rotary[i]=int32_t(d<=0x7fffffff ? int64_t(d):int64_t(d)-0x100000000LL);
        }
    }
    previous=s; return c;
}
std::array<int32_t,6> EncoderSteps::apply(const std::array<int32_t,6>& deltas) {
    std::array<int32_t,6> steps{};
    for(unsigned i=0;i<6;i++) {
        const int64_t total=int64_t(remainder[i])+deltas[i];
        steps[i]=int32_t(total/CountsPerStep);
        remainder[i]=int32_t(total%CountsPerStep);
    }
    return steps;
}
bool Session::enqueue(const Frame& f,bool urgent) {
    if(count==outgoing.size()) return false;
    Wire w; if(!encode(f,w)) return false;
    w.type=f.type; w.seq=f.seq; w.command=f.type!=5;
    std::size_t slot=count;
    if(urgent&&count) slot=offset?1:0;
    if(slot<count) {
        for(std::size_t i=count;i>slot;i--)
            outgoing[(first+i)%outgoing.size()]=outgoing[(first+i-1)%outgoing.size()];
    }
    outgoing[(first+slot)%outgoing.size()]=w;
    count++; return true;
}
void Session::closed() { closed_=true; connected=active=inflight=false; listener.lost("transport"); }
void Session::handshake(uint32_t now) {
    info_attempted=false;listener.firmware_info("","");
    split_attempted=split_active=false;analog_seen.fill(false);
    first=count=offset=0; decoder.reset(); connected=false; command_seq=event_seq=capabilities=0;
    pending={}; pending.type=1; pending.session=nonce; pending_sent=false;
    // Delimit any unfinished frame or boot text before the HELLO.
    outgoing[0]={}; outgoing[0].bytes[0]=0; outgoing[0].size=1; count=1;
    enqueue(pending); inflight=true; attempts=1; sent_at=progress_at=now;
}
void Session::start(uint32_t seed,uint32_t now) {
    nonce=seed?seed:1; closed_=false; active=true; speed_attempted=false;
    connected=inflight=false; first=count=offset=0; decoder.reset(); speed=Speed::normal;
    if(preferred_baud==9600) handshake(now);
    else { speed=Speed::recover; speed_at=now; }
}
void Session::reconnect(uint32_t now,const char* why) {
    listener.lost(why); if(!++nonce) nonce=1;
    connected=inflight=false; first=count=offset=0; decoder.reset(); pending_sent=false;
    if(preferred_baud==9600) { speed=Speed::settle; speed_at=now; return; }
    speed=Speed::recover; speed_at=now; speed_attempted=true;
}
bool Session::send(uint8_t type,const uint8_t* p,std::size_t n,uint32_t now) {
    if(!ready()) return false;
    return issue(type,p,n,now);
}
bool Session::issue(uint8_t type,const uint8_t* p,std::size_t n,uint32_t now) {
    if(!connected||inflight||n>MaxPayload) return false;
    Frame f; f.type=type; f.session=nonce; f.seq=uint16_t(command_seq%65535+1); f.length=uint16_t(n);
    if(n) std::copy_n(p,n,f.payload.begin());
    if(!enqueue(f,true)) return false;
    command_seq=f.seq; pending=f; attempts=1; pending_sent=false; sent_at=now; inflight=true; return true;
}
void Session::receive(const Frame& f,uint32_t now) {
    if(f.session!=nonce) return;
    if(f.type==0x81&&inflight&&pending.type==1&&f.seq==0) {
        Snapshot s;
        if(f.length!=59||read16(f.payload.data())!=1||!snapshot(f.payload.data()+4,55,s)) return;
        capabilities=read16(f.payload.data()+2);
        connected=true; inflight=false; last_ack=now;
        Frame ack; ack.type=5; ack.session=nonce;
        if(!enqueue(ack,true)) { reconnect(now,"hello-ack-queue"); return; }
        listener.state(s,true);
        listener.analog(0,uint16_t(s.ms),s.tbar);listener.analog(1,uint16_t(s.ms),uint16_t(s.x)<<8|s.y);
    } else if(f.type==0x87&&inflight&&pending.type==8&&f.seq==pending.seq) {
        if(f.length!=29||f.payload[0]!=1||f.payload[16]||f.payload[28])return;
        for(unsigned i=1;i<29;i++)if(f.payload[i]&&(f.payload[i]<32||f.payload[i]>126))return;
        inflight=false;last_ack=now;
        listener.firmware_info(reinterpret_cast<const char*>(f.payload.data()+1),reinterpret_cast<const char*>(f.payload.data()+17));
    } else if(f.type==0x80&&inflight&&pending.type!=1&&f.seq==pending.seq&&f.length==1) {
        uint8_t type=pending.type; inflight=false; last_ack=now;
        if(f.payload[0]==3) { reconnect(now,"seq"); return; }
        if(type==4&&speed==Speed::request) {
            if(f.payload[0]) { speed=Speed::normal; return; }
            speed=Speed::change; speed_at=now; return;
        }
        if(type==6&&speed==Speed::confirm) {
            if(f.payload[0]) { reconnect(now,"baud-confirm"); return; }
            speed=Speed::normal; return;
        }
        if(type==7) { split_active=f.payload[0]==0; return; }
        listener.ack(type,f.payload[0]);
    } else if((f.type==0x83||f.type==0x84)&&connected&&split_active) {
        if(f.length!=4) return;
        const unsigned axis=f.type-0x83;
        const uint16_t delta=uint16_t(f.seq-analog_seq[axis]);
        if(analog_seen[axis]&&(!delta||delta>=0x8000)) return;
        const auto value=read16(f.payload.data()+2);
        if(!axis&&value>4095) return;
        if(analog_seen[axis]&&delta>1) listener.analog_gap(axis,uint16_t(delta-1));
        analog_seen[axis]=true;analog_seq[axis]=f.seq;
        listener.analog(axis,read16(f.payload.data()),value);
    } else if(f.type==0x86&&connected&&split_active) {
        if(f.length!=32) return;
        std::array<uint32_t,8> values{};
        for(unsigned i=0;i<8;i++) values[i]=read32(f.payload.data()+4*i);
        listener.diagnostic(values);
    } else if((f.type==0x82||(f.type==0x85&&split_active))&&connected) {
        Snapshot s;
        if(f.type==0x85) {
            if(f.length!=51) return;
            std::array<uint8_t,55> full{};
            std::copy_n(f.payload.begin(),27,full.begin());std::copy_n(f.payload.begin()+27,24,full.begin()+31);
            if(!snapshot(full.data(),full.size(),s)) return;
        } else if(!snapshot(f.payload.data(),f.length,s)) return;
        if(f.seq!=event_seq&&f.seq!=uint16_t(event_seq%65535+1)) { reconnect(now,"event-gap"); return; }
        Frame ack; ack.type=5; ack.seq=f.seq; ack.session=nonce;
        if(!enqueue(ack)) return;
        if(f.seq!=event_seq) { event_seq=f.seq; listener.state(s,false);
            if(!split_active) { listener.analog(0,uint16_t(s.ms),s.tbar);listener.analog(1,uint16_t(s.ms),uint16_t(s.x)<<8|s.y); } }
    }
}
void Session::tick(uint32_t now) {
    if(!active||closed_) return;
    std::array<uint8_t,256> bytes{};
    int n=transport.read(bytes.data(),bytes.size());
    if(n<0) { closed(); return; }
    if(speed==Speed::recover) {
        const int result=transport.set_baud(9600);
        if(result<0||(result==0&&uint32_t(now-speed_at)>3500)) { closed(); return; }
        if(result==1) {
            current_baud=9600;
            // Firmware link timeout is 5000 ms. Stay silent beyond it before
            // HELLO, including a warm host restart from an established fast link.
            if(uint32_t(now-speed_at)>=5500) { speed=Speed::normal; handshake(now); }
        }
        return;
    }
    for(int i=0;i<n;i++) { Frame f; if(decoder.feed(bytes[std::size_t(i)],f)) receive(f,now); }
    if(speed==Speed::settle) {
        if(uint32_t(now-speed_at)>=400) { speed=Speed::normal; handshake(now); }
        return;
    }
    const unsigned limit=pending.type==1?5u:pending.type==2?8u:3u;
    const uint32_t wait=pending.type==1?2000u:pending.type==2?400u:700u;
    if(inflight&&pending_sent&&uint32_t(now-sent_at)>=wait) {
        if(attempts>=limit) {
            std::snprintf(lost_why,sizeof lost_why,"timeout-%u",unsigned(pending.type));
            reconnect(now,lost_why);
        } else if(enqueue(pending,true)) { attempts++; pending_sent=false; sent_at=now; }
    }
    if(connected&&!inflight&&speed==Speed::normal&&wants_speed()&&!count) {
        uint8_t payload[4]; put32(payload,preferred_baud);
        if(issue(4,payload,4,now)) { speed_attempted=true; speed=Speed::request; }
    }
    if(speed==Speed::change&&!count) {
        int result=transport.set_baud(preferred_baud);
        if(result<0||uint32_t(now-speed_at)>800) { reconnect(now,"baud-change"); return; }
        if(result==1) { current_baud=preferred_baud; decoder.reset(); speed=Speed::guard; speed_at=now; }
    }
    if(speed==Speed::guard&&uint32_t(now-speed_at)>=300) {
        uint8_t payload[4]; put32(payload,current_baud);
        if(issue(6,payload,4,now)) speed=Speed::confirm;
    }
    if(connected&&!inflight&&speed==Speed::normal&&!wants_speed()&&(capabilities&0x80)&&!split_attempted) {
        uint8_t mode=1;if(issue(7,&mode,1,now)) split_attempted=true;
    }
    if(connected&&!inflight&&speed==Speed::normal&&!wants_speed()&&(capabilities&0x100)&&!info_attempted) {
        if(issue(8,nullptr,0,now))info_attempted=true;
    }
    if(ready()&&uint32_t(now-last_ack)>=400) {
        uint8_t indicators=0; send(0x13,&indicators,1,now);
    }
    if(count) {
        auto& w=outgoing[first]; int wrote=transport.write(w.bytes.data()+offset,w.size-offset);
        if(wrote<0) { closed(); return; }
        if(wrote) { offset+=std::size_t(wrote); progress_at=now; }
        if(offset==w.size) {
            if(inflight&&w.command&&w.type==pending.type&&w.seq==pending.seq) {
                pending_sent=true; sent_at=now;
            }
            offset=0; first=(first+1)%outgoing.size(); count--;
        }
        else if(uint32_t(now-progress_at)>4000) reconnect(now,"tx-stall");
    } else progress_at=now;
}
}
