#pragma once
#include "config.hpp"
#include "mixer.hpp"
#include <string>
namespace bkds::link {
// Linux TCP client for kavtor's JSON-line panel protocol. Outside the portable core.
class KavtorAdapter final:public MixerAdapter {
    struct Impl;
    Impl* impl;
public:
    KavtorAdapter();
    ~KavtorAdapter() override;
    void configure(const Config&) override;
    void deactivate() override;
    MixerState state() override;
    bool server_info(ServerInfo&) const override;
    bool request(MixerAction,unsigned) override;
    bool overlay_layout(OverlayLayout&) const override;
    bool set_aux_source(unsigned,unsigned) override;
    bool set_supersource_input(unsigned,unsigned,unsigned) override;
    bool set_multiview_bank(bool) override;
    bool set_overlay_layout(const OverlayLayout&) override;
    bool frame_rates() const override { return true; }
    bool set_rate(uint32_t frames) override;
    bool automatic(TransitionType type,uint32_t duration,uint32_t code,bool reverse,uint32_t softness) override;
    bool manual(uint16_t,TransitionType,uint32_t,bool,uint32_t) override;
    void cancel_manual() override;
    bool manual_me_pickup() const override {return true;}
    bool fade_to_black(uint32_t frames) override;
    bool supports_transition_preview() const override;
    bool set_transition_preview(bool) override;
    bool supports_dme() const override;
    bool prepare_wipe_border_profile(int,int,int) override;
    bool prepare_dust(const std::array<uint32_t,3>&) override;
    bool supports_mix_preparation(bool)const override;
    bool prepare_mix(bool,uint32_t,uint32_t=0)override;
    unsigned stinger_slots() const override;
    bool supports_dme_parameters() const override {return false;}
    unsigned keypad_transition_slots(TransitionType) const override;
    std::string keypad_transition_label(TransitionType,unsigned) const override;
    bool supports_dme_background(unsigned) const override;
    bool set_dme_background_scope(unsigned,bool,bool=false)override;
    bool set_dme_background(unsigned,int) override;
    unsigned first_keypad_transition_slot(TransitionType) const override;
    bool keypad_transition_available(TransitionType,unsigned) const override;
    bool valid_wipe_code(uint32_t) const override;
    bool valid_dme_code(uint32_t) const override;
    bool wipe_tiles_supported(uint32_t) const override;
    bool wipe_tiles(uint32_t,uint32_t,bool=false) override;
    bool wipe_geometry_supported(uint32_t) const override;
    bool wipe_geometry(uint32_t,uint32_t,uint32_t,bool=false) override;
    bool wipe_modifiers() const override { return true; }
    bool wipe_aspect_percentage() const override {return true;}
    bool prepare_wipe_modifier(unsigned,uint32_t,uint32_t=0) override;
    bool keyers() const override { return true; }
    bool native_key_controls() const override;
    bool key_setting(unsigned,unsigned,unsigned,int=-1) override;
    bool set_me(unsigned slot) override;
    bool dsk(bool mix,uint32_t duration,unsigned slot) override;
    bool set_key_source(unsigned slot,int source) override;
    bool toggle_key(unsigned slot) override;
    bool set_dsk_source(unsigned slot,int source) override;
    bool set_next_transition(bool,const std::array<bool,4>&) override;
    bool toggle_next_background() override;
    bool toggle_next_key(unsigned slot) override;
    bool wipe_style(uint32_t multi,uint32_t border,uint32_t aspect_w,uint32_t aspect_h,uint32_t pos_x,uint32_t pos_y,bool cursor,bool persist) override;
    uint32_t default_softness() const override { return 0; }
    uint32_t wipe_preset(unsigned index) const override;
    bool submit(const std::string& body);
    std::string web_status() const;
private:
    bool enqueue(const std::string& body);
};
}
