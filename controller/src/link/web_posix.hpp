#pragma once
#include "obs_posix.hpp"
namespace bkds::link {
class KavtorAdapter;
class VmixAdapter;
class AtemAdapter;
class WebConfig {
    struct Impl; Impl* impl;
public:
    WebConfig(unsigned port,const std::string& path,ObsAdapter& obs,KavtorAdapter* kavtor=nullptr,VmixAdapter* vmix=nullptr,AtemAdapter* atem=nullptr,unsigned slot=0);
    ~WebConfig();
    bool valid() const;
    bool bind_profile(const std::string&,ObsAdapter&,KavtorAdapter*,VmixAdapter*,AtemAdapter*,unsigned slot=0);
    bool sync_system(Configuration&,const char* firmware,const char* date,uint32_t baud,bool connected,bool enabled,bool idle,const char* active_protocol=nullptr);
    bool take(Mappings&,std::array<std::string,24>&);
};
}
