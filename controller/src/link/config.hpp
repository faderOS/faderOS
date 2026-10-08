#pragma once
#include "core.hpp"
#include <iosfwd>
namespace bkds::link {
using IPv4=std::array<uint8_t,4>;
enum class Backend : uint8_t { vmix,atem,obs,midi,kavtor };
inline constexpr unsigned BackendCount=5;
inline constexpr const char* BackendNames[]={"vMix","ATEM","OBS WebSocket","MIDI","kavtor"};
struct Endpoint {
    IPv4 host{127,0,0,1};
    uint16_t port=0; // 0 = the adapter's own default (OBS 4455, kavtor 9100).
    bool operator==(const Endpoint& other) const { return host==other.host&&port==other.port; }
};
inline std::array<Endpoint,BackendCount> protocol_endpoints() {
    std::array<Endpoint,BackendCount> endpoints{};
    endpoints[unsigned(Backend::vmix)]={{127,0,0,1},8099};
    endpoints[unsigned(Backend::atem)]={{127,0,0,1},9910};
    endpoints[unsigned(Backend::obs)]={{192,168,1,100},0};
    endpoints[unsigned(Backend::midi)]={{127,0,0,1},0};
    endpoints[unsigned(Backend::kavtor)]={{127,0,0,1},9100};
    return endpoints;
}
struct ServerProfile {Backend backend=Backend::obs;std::array<Endpoint,BackendCount> endpoints=protocol_endpoints();bool enabled=false;bool operator==(const ServerProfile& o)const{return backend==o.backend&&endpoints==o.endpoints&&enabled==o.enabled;}};
struct Config {
    unsigned server=0;std::array<ServerProfile,3> servers{};
    bool dhcp=true;
    Backend backend=Backend::obs;
    std::array<Endpoint,BackendCount> endpoints=protocol_endpoints();
    IPv4 ip{192,168,1,50},mask{255,255,255,0},gateway{192,168,1,1},dns{1,1,1,1};
    void select_server(unsigned index){if(index>=3||index==server)return;servers[server].backend=backend;servers[server].endpoints=endpoints;servers[server].enabled=true;server=index;backend=servers[index].backend;endpoints=servers[index].endpoints;servers[index].enabled=true;}
    Endpoint& active() { return endpoints[unsigned(backend)]; }
    const Endpoint& active() const { return endpoints[unsigned(backend)]; }
};
bool parse_ip(const char*,IPv4&);
bool valid_config(const Config&);
bool parse_settings(std::istream&,Config&);
bool write_settings(std::ostream&,const Config&);
struct ConfigStore {
    virtual bool save(const Config&)=0;
    virtual ~ConfigStore()=default;
};
bool same_config(const Config&,const Config&);
enum class SaveResult { unchanged,saved,invalid,io_error,conflict };
class Configuration {
    ConfigStore& store;
    Config current;
    uint32_t revision_=0;
public:
    Configuration(ConfigStore& s,const Config& c):store(s),current(c) {}
    const Config& saved() const { return current; }
    uint32_t revision() const { return revision_; }
    SaveResult save(const Config&,uint32_t expected_revision);
};
}
