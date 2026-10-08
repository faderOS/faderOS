#pragma once
#include "platform.hpp"
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>

namespace bkds::link {
constexpr std::size_t MaxPayload=192, MaxWire=208, KeyCount=184;
struct Frame {
    uint8_t type=0;
    uint16_t seq=0,length=0;
    uint32_t session=0;
    std::array<uint8_t,MaxPayload> payload{};
};
struct Wire { std::array<uint8_t,MaxWire> bytes{}; std::size_t size=0; uint8_t type=0; uint16_t seq=0; bool command=false; };
bool encode(const Frame&,Wire&);
bool decode(const uint8_t*,std::size_t,Frame&);
uint32_t read32(const uint8_t*);
class Decoder {
    std::array<uint8_t,MaxWire> bytes{};
    std::size_t size=0;
    bool discard=false;
public:
    bool feed(uint8_t byte,Frame&);
    void reset() { size=0; discard=false; }
};
struct Snapshot {
    uint32_t ms=0;
    std::bitset<KeyCount> held{};
    uint16_t tbar=0;
    uint8_t x=128,y=128;
    std::array<uint32_t,6> rotary{};
};
bool snapshot(const uint8_t*,std::size_t,Snapshot&);
struct Changes {
    std::bitset<KeyCount> pressed{},released{},double_click{};
    std::array<int32_t,6> rotary{};
};
class Inputs {
    Snapshot previous{};
    std::array<uint32_t,KeyCount> clicked{};
    std::bitset<KeyCount> seen{};
public:
    Changes update(const Snapshot&,bool baseline);
};
// UI sensitivity only: retain raw cumulative counts in Inputs/protocol.
class EncoderSteps {
    std::array<int32_t,6> remainder{};
public:
    static constexpr int32_t CountsPerStep=8;
    std::array<int32_t,6> apply(const std::array<int32_t,6>& deltas);
    void reset() { remainder.fill(0); }
};
// Return 0 for would-block, negative for closed/error, positive bytes transferred.
// Neither method may block. No POSIX or SDK dependencies in the core.
struct Transport {
    virtual int read(uint8_t*,std::size_t)=0;
    virtual int write(const uint8_t*,std::size_t)=0;
    // 1 completed, 0 pending, -1 unsupported/error. Must not block.
    virtual int set_baud(uint32_t rate) { return rate==9600?1:-1; }
    virtual ~Transport()=default;
};
struct Listener {
    virtual void state(const Snapshot&,bool baseline)=0;
    virtual void analog(unsigned /* axis: 0 T-bar, 1 joystick */,uint16_t /* sample ms */,uint16_t /* value: T-bar or X:Y */) {}
    virtual void firmware_info(const char*,const char*) {}
    virtual void diagnostic(const std::array<uint32_t,8>&) {}
    virtual void analog_gap(unsigned,uint16_t) {}
    virtual void ack(uint8_t type,uint8_t status)=0;
    virtual void lost(const char* why)=0;
    virtual ~Listener()=default;
};
class Session {
    Transport& transport;
    Listener& listener;
    Decoder decoder;
    std::array<Wire,16> outgoing{};
    std::size_t first=0,count=0,offset=0;
    Frame pending{};
    uint32_t nonce=0,sent_at=0,last_ack=0,progress_at=0;
    uint16_t command_seq=0,event_seq=0,capabilities=0;
    unsigned attempts=0;
    bool split_attempted=false,split_active=false,info_attempted=false;
    std::array<bool,2> analog_seen{};
    std::array<uint16_t,2> analog_seq{};
    bool pending_sent=false;
    char lost_why[24]{};
    enum class Speed { normal,request,change,guard,confirm,recover,settle } speed=Speed::normal;
    uint32_t preferred_baud=9600,current_baud=9600,speed_at=0;
    bool speed_attempted=false;
    bool issue(uint8_t,const uint8_t*,std::size_t,uint32_t);
    bool wants_speed() const { return preferred_baud!=9600&&!speed_attempted&&(capabilities&0x20); }
    bool active=false,connected=false,inflight=false,closed_=false;
    bool enqueue(const Frame&,bool urgent=false);
    void handshake(uint32_t);
    void receive(const Frame&,uint32_t);
    void reconnect(uint32_t,const char* why);
    void closed();
public:
    Session(Transport& t,Listener& l):transport(t),listener(l) {}
    void prefer_baud(uint32_t rate) { preferred_baud=rate; }
    uint32_t baud() const { return current_baud; }
    void start(uint32_t seed,uint32_t now);
    void tick(uint32_t now);
    bool send(uint8_t type,const uint8_t* payload,std::size_t size,uint32_t now);
    bool ready() const { return (!(capabilities&0x100)||info_attempted)&&(!(capabilities&0x80)||split_attempted)&&connected&&!inflight&&!closed_&&speed==Speed::normal&&!wants_speed(); }
    bool split_analog() const { return split_active; }
    bool lcd_icons_supported() const { return (capabilities&0x400)!=0; }
    bool lcd_graphics_supported() const { return (capabilities&0x200)!=0; }
    bool lcd_patch_supported() const { return (capabilities&0x10)!=0; }
    bool is_closed() const { return closed_; }
};
}
