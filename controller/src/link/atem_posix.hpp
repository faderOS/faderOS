#pragma once
#include "mixer.hpp"
#include <string>
namespace bkds::link {
class AtemAdapter final:public MixerAdapter {
    struct Impl;Impl* impl;
public:
    explicit AtemAdapter(const std::string& path="atem-mappings.json");
    ~AtemAdapter() override;
    void configure(const Config&) override;
    void deactivate() override;
    MixerState state() override;
    bool server_info(ServerInfo&) const override;
    bool output(OutputKind) override;
    bool video_formats(VideoFormats&) const override;
    bool set_video_format(unsigned,int) override;
    bool output_routes(OutputRoutes&) const override;
    bool set_output_source(unsigned,unsigned) override;
    bool set_output_route(unsigned,bool) override;
    bool multiview_settings(unsigned,MultiviewSettings&) const override;
    bool set_multiview_inputs(unsigned,bool,bool) override;
    bool set_multiview_window(unsigned,unsigned,bool,bool) override;
    bool request(MixerAction,unsigned) override;
    bool manual(uint16_t,TransitionType,uint32_t=0,bool=false,uint32_t=0) override;
    void cancel_manual() override;
    bool supports_transition_preview() const override {return true;}
    bool set_transition_preview(bool) override;
    bool automatic(TransitionType,uint32_t,uint32_t=0,bool=false,uint32_t=0) override;
    unsigned wipe_border_maximum() const override {return 100;}
    bool native_wipe_modifiers() const override {return true;}
    bool wipe_modifiers() const override {return true;}
    bool wipe_modifier_supported(unsigned id) const override {return id==168||id==170||id==174;}
    bool prepare_wipe_modifier(unsigned,uint32_t,uint32_t=0) override;
    bool live_transition_controls() const override {return true;}
    bool live_bus_changes() const override {return true;}
    bool mix_dip() const override {return true;}
    unsigned stinger_slots() const override;
    bool fixed_stinger() const override {return true;}
    bool supports_dme() const override;
    bool dme_keypad_grid() const override {return true;}
    bool me_delegation() const override {return true;}
    bool set_me(unsigned) override;
    bool dsk(bool,uint32_t,unsigned=0) override;
    unsigned dsk_channels() const override;
    bool set_dsk_rate(uint32_t,unsigned=0) override;
    bool configure_dsk(const std::string&,std::string&);
    bool keyers() const override {return true;}
    bool key_source_selection() const override {return true;}
    bool native_key_controls() const override {return true;}
    bool key_setting(unsigned,unsigned,unsigned,int=-1) override;
    bool supports_media_capture() const override;
    bool capture_media_still() override;
    bool clear_media_still(unsigned) override;
    bool select_media_still(unsigned) override;
    bool toggle_key(unsigned) override;
    bool toggle_next_background() override;
    bool toggle_next_key(unsigned) override;
    bool set_next_transition(bool,const std::array<bool,4>&) override;
    bool configure_key(const std::string&,std::string&);
    bool supports_ftb() const override { return true; }
    bool fade_to_black(uint32_t) override;
    bool set_ftb_rate(uint32_t) override;
    bool frame_rates() const override {return true;}
    uint32_t maximum_rate() const override {return 250;}
    uint32_t wipe_preset(unsigned) const override;
    const char* wipe_name(uint32_t) const override;
    uint32_t first_direct_dme_code() const override {return 2601;}
    bool valid_dme_code(uint32_t) const override;
    bool keypad_transition_available(TransitionType,unsigned) const override;
    bool valid_wipe_code(uint32_t) const override;
    uint32_t default_softness() const override {return 0;}
    bool set_transition_rate(TransitionType,uint32_t) override;
    bool set_rate(uint32_t) override;
    std::string web_status();
    bool save_mappings(const std::string&,std::string&);
};
}
