#include "config.hpp"
#include <istream>
#include <ostream>
#include <string>
namespace bkds::link {
bool parse_ip(const char* p,IPv4& ip) {
    IPv4 result{};
    for(unsigned i=0;i<4;i++) {
        unsigned value=0,digits=0;
        while(*p>='0'&&*p<='9') { value=value*10+unsigned(*p++-'0'); if(++digits>3||value>255) return false; }
        if(!digits) return false;
        result[i]=uint8_t(value);
        if(i<3) { if(*p++!='.') return false; } else if(*p) return false;
    }
    ip=result; return true;
}
bool valid_config(const Config& c) {
    if(unsigned(c.backend)>4||c.server>=3) return false;
    for(const auto& s:c.servers)if(unsigned(s.backend)>=BackendCount)return false;
    const uint32_t mask=read32(c.mask.data()),inv=~mask;
    if(!mask||(inv&(inv+1))) return false;
    if(!c.dhcp) {
        const uint32_t ip=read32(c.ip.data()),gw=read32(c.gateway.data());
        if(c.ip[0]==0||c.ip[0]==127||c.ip[0]>=224) return false;
        if(inv&&(!(ip&inv)||(ip&inv)==inv)) return false;
        if(gw&&((gw&mask)!=(ip&mask))) return false;
    }
    return true;
}
bool same_config(const Config& a,const Config& b) {
    if(a.server>=3||b.server>=3)return false;
    return a.server==b.server&&[&]{auto x=a.servers,y=b.servers;x[a.server]={a.backend,a.endpoints,true};y[b.server]={b.backend,b.endpoints,true};return x==y;}()&&a.dhcp==b.dhcp&&a.backend==b.backend&&a.endpoints==b.endpoints&&a.ip==b.ip&&
      a.mask==b.mask&&a.gateway==b.gateway&&a.dns==b.dns;
}
static bool read_ip(std::istream& in,IPv4& ip) {
    std::string text;
    return bool(in>>text)&&parse_ip(text.c_str(),ip);
}
bool parse_settings(std::istream& in,Config& c) {
    unsigned version=0,dhcp=0,backend=0;
    if(!(in>>version>>dhcp>>backend)||dhcp>1||backend>=BackendCount) return false;
    Config tmp;
    tmp.dhcp=dhcp;
    tmp.backend=Backend(backend);
    if(version==1) {
        unsigned port=0;
        if(!(in>>port)||port>65535) return false;
        for(auto* ip:{&tmp.ip,&tmp.mask,&tmp.gateway,&tmp.dns}) if(!read_ip(in,*ip)) return false;
        if(!read_ip(in,tmp.endpoints[backend].host)) return false;
        tmp.endpoints[backend].port=uint16_t(port);
    } else if(version==2||version==3) {
        for(auto* ip:{&tmp.ip,&tmp.mask,&tmp.gateway,&tmp.dns}) if(!read_ip(in,*ip)) return false;
        for(unsigned i=0;i<BackendCount;i++) {
            unsigned port=0;
            if(!read_ip(in,tmp.endpoints[i].host)||!(in>>port)||port>65535) return false;
            tmp.endpoints[i].port=uint16_t(port);
        }
        if(version==3){if(!(in>>tmp.server)||tmp.server>=3)return false;for(auto& server:tmp.servers){unsigned enabled=0,type=0;if(!(in>>enabled>>type)||enabled>1||type>=BackendCount)return false;server.enabled=enabled;server.backend=Backend(type);for(auto& endpoint:server.endpoints){unsigned port=0;if(!read_ip(in,endpoint.host)||!(in>>port)||port>65535)return false;endpoint.port=uint16_t(port);}}}
    } else return false;
    tmp.servers[tmp.server].backend=tmp.backend;tmp.servers[tmp.server].endpoints=tmp.endpoints;tmp.servers[tmp.server].enabled=true;
    std::string trailing;
    if(in>>trailing||!valid_config(tmp)) return false;
    c=tmp;
    return true;
}
bool write_settings(std::ostream& out,const Config& c) {
    if(!valid_config(c)) return false;
    auto line=[&](const IPv4& ip) {
        out<<unsigned(ip[0])<<'.'<<unsigned(ip[1])<<'.'<<unsigned(ip[2])<<'.'<<unsigned(ip[3]);
    };
    out<<3<<' '<<unsigned(c.dhcp)<<' '<<unsigned(c.backend)<<'\n';
    for(const auto* ip:{&c.ip,&c.mask,&c.gateway,&c.dns}) { line(*ip); out<<'\n'; }
    for(const Endpoint& endpoint:c.endpoints) { line(endpoint.host); out<<' '<<endpoint.port<<'\n'; }
    out<<c.server<<'\n';
    for(unsigned i=0;i<3;i++){const auto& server=c.servers[i];out<<unsigned(i==c.server||server.enabled)<<' '<<unsigned(i==c.server?c.backend:server.backend)<<'\n';for(const auto& endpoint:i==c.server?c.endpoints:server.endpoints){line(endpoint.host);out<<' '<<endpoint.port<<'\n';}}
    return bool(out);
}
SaveResult Configuration::save(const Config& requested,uint32_t expected) {
    Config next=requested;if(next.server<3){next.servers[next.server].backend=next.backend;next.servers[next.server].endpoints=next.endpoints;next.servers[next.server].enabled=true;}
    if(expected!=revision_) return SaveResult::conflict;
    if(!valid_config(next)) return SaveResult::invalid;
    if(same_config(next,current)) return SaveResult::unchanged;
    if(!store.save(next)) return SaveResult::io_error;
    current=next; ++revision_; return SaveResult::saved;
}
}
