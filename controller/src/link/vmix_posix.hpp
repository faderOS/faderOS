#pragma once
#include "mixer.hpp"
#include <string>
namespace bkds::link {
class VmixAdapter final:public MixerAdapter {
    struct Impl; Impl* impl;
public:
    explicit VmixAdapter(const std::string& mapping_path="vmix-mappings.json");
    ~VmixAdapter() override;
    void configure(const Config&) override;
    void deactivate() override;
    MixerState state() override;
    bool server_info(ServerInfo&) const override;
    bool request(MixerAction,unsigned) override;
    bool automatic(TransitionType,uint32_t,uint32_t=0,bool=false,uint32_t=0) override;
    bool manual(uint16_t,TransitionType,uint32_t=0,bool=false,uint32_t=0) override;
    void cancel_manual() override;
    bool overlay_delegation() const override {return true;}
    bool toggle_overlay(unsigned,unsigned,bool) override;
    bool supports_dme() const override {return true;}
    unsigned keypad_transition_slots(TransitionType t) const override {return t==TransitionType::mix?3:t==TransitionType::dme?7:0;}
    unsigned first_keypad_transition_slot(TransitionType t) const override {return t==TransitionType::dme?0:1;}
    bool keypad_transition_available(TransitionType,unsigned) const override;
    std::string keypad_transition_label(TransitionType,unsigned) const override;
    std::string dme_label(unsigned) const override;
    bool supports_dme_parameters() const override {return false;}
    unsigned stinger_slots() const override {return 8;}
    unsigned first_stinger_slot() const override {return 1;}
    uint32_t first_direct_dme_code() const override {return 0;}
    bool valid_dme_code(uint32_t code) const override {return code<=7&&keypad_transition_available(TransitionType::dme,code);}
    bool valid_wipe_code(uint32_t) const override;
    bool supports_ftb() const override {return true;}
    bool fade_to_black(uint32_t) override;
    std::string web_status();
    bool save_mappings(const std::string& json,std::string& error);
};
}
