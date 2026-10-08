#pragma once
#include "menu.hpp"
#include "config.hpp"
namespace bkds::link {
inline constexpr const char* ProductName="faderOS";
inline constexpr const char* HostVersion="0.28.0";
inline constexpr const char* ProtocolLabels[]={"VMIX","ATEM","OBS","MIDI","KAVTOR"};
inline void home_screen(Panel& panel,Backend backend,bool connected,uint32_t now,const char* clock) {
    std::string top=std::string(ProductName)+" "+HostVersion;
    std::string status=(connected||((now/500)%2==0)?"*":" ")+std::string(ProtocolLabels[unsigned(backend)]);
    top.resize(40-status.size(),' ');top+=status;panel.lcd(0,top.c_str());
    std::string bottom=clock; if(bottom.size()>40)bottom.resize(40);
    bottom.insert(0,40-bottom.size(),' ');panel.lcd(1,bottom.c_str());
}
}
