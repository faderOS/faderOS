#pragma once
#include "mixer.hpp"
#include <cstdio>
namespace bkds::link {
// Stable control target. Only the application thread selects adapters.
class AdapterRouter final:public MixerAdapter {
    std::array<std::array<MixerAdapter*,BackendCount>,3> adapters{};
    std::array<MixerAdapter*,3> running{};std::array<Config,3> applied{};std::array<bool,3> configured{};
    MixerAdapter* active=nullptr;
    Backend selected=Backend::obs;
public:
    void attach(Backend b,MixerAdapter& a,unsigned server=0) { if(server<3)adapters[server][unsigned(b)]=&a; }
    bool supported() const { return active!=nullptr; }
    Backend backend() const { return selected; }
    void configure(const Config& c) override {
        if(!valid_config(c)) return;
        const unsigned slot=c.server;auto* next=adapters[slot][unsigned(c.backend)];
        if(!configured[slot]||applied[slot].backend!=c.backend||!(applied[slot].active()==c.active())){
            if(running[slot]&&running[slot]!=next)running[slot]->deactivate();
            if(next)next->configure(c);
            running[slot]=next;applied[slot]=c;configured[slot]=true;
        }
        selected=c.backend;active=next;
    }
    bool server_info(ServerInfo& i) const override {return active&&active->server_info(i);}
    bool video_formats(VideoFormats& r) const override {return active&&active->video_formats(r);}
    bool set_video_format(unsigned m,int previous) override {return active&&active->set_video_format(m,previous);}
    bool output_routes(OutputRoutes& r) const override {return active&&active->output_routes(r);}
    bool set_output_source(unsigned i,unsigned s) override {return active&&active->set_output_source(i,s);}
    bool set_output_route(unsigned i,bool mv) override {return active&&active->set_output_route(i,mv);}
    bool multiview_settings(unsigned i,MultiviewSettings& s) const override {return active&&active->multiview_settings(i,s);}
    bool set_multiview_inputs(unsigned v,bool safe,bool enabled) override {return active&&active->set_multiview_inputs(v,safe,enabled);}
    bool set_multiview_window(unsigned m,unsigned w,bool safe,bool on) override {return active&&active->set_multiview_window(m,w,safe,on);}
    MixerState state() override {
        if(active) return active->state();
        MixerState s; std::snprintf(s.message.data(),s.message.size(),"ADAPTER NOT IMPLEMENTED"); return s;
    }
    bool overlay_layout(OverlayLayout& o) const override { return active?active->overlay_layout(o):false; }
    bool set_aux_source(unsigned role,unsigned source) override { return active?active->set_aux_source(role,source):false; }
    bool set_supersource_input(unsigned side,unsigned window,unsigned input) override { return active&&active->set_supersource_input(side,window,input); }
    bool set_multiview_bank(bool upper) override { return active?active->set_multiview_bank(upper):false; }
    bool set_overlay_layout(const OverlayLayout& o) override { return active?active->set_overlay_layout(o):false; }
    void dme_parameters(DmeParameters p) override { if(active) active->dme_parameters(p); }
    bool manual(uint16_t p,TransitionType t,uint32_t c=0,bool r=false,uint32_t s=3) override { return active?active->manual(p,t,c,r,s):false; }
    void cancel_manual() override { if(active) active->cancel_manual(); }
    bool output(OutputKind k) override { return active?active->output(k):false; }
    unsigned dsk_channels() const override { return active?active->dsk_channels():0; }
    bool set_dsk_rate(uint32_t f,unsigned s=0) override {return active?active->set_dsk_rate(f,s):false;}
    bool dsk(bool m,uint32_t d,unsigned s=0) override { return active?active->dsk(m,d,s):false; }
    bool overlay_delegation() const override {return active&&active->overlay_delegation();}
    bool toggle_overlay(unsigned layer,unsigned source,bool preview) override {return active&&active->toggle_overlay(layer,source,preview);}
    bool keyers() const override { return active?active->keyers():false; }
    bool manual_me_pickup() const override {return active&&active->manual_me_pickup();}
    bool me_delegation() const override { return active?active->me_delegation():false; }
    bool set_me(unsigned s) override { return active?active->set_me(s):false; }
    bool set_key_source(unsigned s,int i) override { return active?active->set_key_source(s,i):false; }
    bool toggle_key(unsigned s) override { return active?active->toggle_key(s):false; }
    bool set_dsk_source(unsigned s,int i) override { return active?active->set_dsk_source(s,i):false; }
    bool set_next_transition(bool b,const std::array<bool,4>& k) override {return active&&active->set_next_transition(b,k);}
    bool native_key_controls() const override {return active&&active->native_key_controls();}
    bool key_setting(unsigned d,unsigned s,unsigned id,int source=-1) override {return active&&active->key_setting(d,s,id,source);}
    bool supports_media_capture() const override {return active&&active->supports_media_capture();}
    bool capture_media_still() override {return active&&active->capture_media_still();}
    bool clear_media_still(unsigned s) override {return active&&active->clear_media_still(s);}
    bool select_media_still(unsigned s) override {return active&&active->select_media_still(s);}
    bool key_source_selection() const override {return active&&active->key_source_selection();}
    bool toggle_next_background() override { return active?active->toggle_next_background():false; }
    bool toggle_next_key(unsigned s) override { return active?active->toggle_next_key(s):false; }
    bool request(MixerAction a,unsigned s) override { return active?active->request(a,s):false; }
    bool supports_transition_preview() const override {return active&&active->supports_transition_preview();}
    bool set_transition_preview(bool on) override {return active&&active->set_transition_preview(on);}
    bool automatic(TransitionType t,uint32_t d,uint32_t c=0,bool r=false,uint32_t s=3) override { return active?active->automatic(t,d,c,r,s):false; }
    bool supports_ftb() const override { return active&&active->supports_ftb(); }
    bool set_ftb_rate(uint32_t f) override { return active&&active->set_ftb_rate(f); }
    bool fade_to_black(uint32_t f) override { return active?active->fade_to_black(f):false; }
    bool live_transition_controls() const override {return active&&active->live_transition_controls();}
    bool live_bus_changes() const override {return active&&active->live_bus_changes();}
    bool supports_mix_preparation(bool super)const override {return active&&active->supports_mix_preparation(super);}
    bool prepare_dust(const std::array<uint32_t,3>& values)override {return active&&active->prepare_dust(values);}
    bool prepare_wipe_border_profile(int side,int inner,int outer)override {return active&&active->prepare_wipe_border_profile(side,inner,outer);}
    bool prepare_mix(bool super,uint32_t a,uint32_t b=0)override {return active&&active->prepare_mix(super,a,b);}
    bool mix_dip() const override {return active&&active->mix_dip();}
    unsigned stinger_slots() const override {return active?active->stinger_slots():0;}
    unsigned first_stinger_slot() const override {return active?active->first_stinger_slot():0;}
    int dme_background(unsigned code) override {return active?active->dme_background(code):-1;}
    bool dme_background_custom(unsigned code) override {return !active||active->dme_background_custom(code);}
    bool set_dme_background_scope(unsigned code,bool custom,bool copy=false) override {return active&&active->set_dme_background_scope(code,custom,copy);}
    bool supports_dme_background(unsigned code) const override {return active&&active->supports_dme_background(code);}
    bool set_dme_background(unsigned code,int source) override {return active&&active->set_dme_background(code,source);}
    unsigned keypad_transition_slots(TransitionType t) const override {return active?active->keypad_transition_slots(t):0;}
    unsigned first_keypad_transition_slot(TransitionType t) const override {return active?active->first_keypad_transition_slot(t):1;}
    bool keypad_transition_available(TransitionType t,unsigned s) const override {return active&&active->keypad_transition_available(t,s);}
    std::string keypad_transition_label(TransitionType t,unsigned s) const override {return active?active->keypad_transition_label(t,s):"";}
    std::string dme_label(unsigned s) const override {return active?active->dme_label(s):"";}
    bool supports_dme_parameters() const override {return active&&active->supports_dme_parameters();}
    bool fixed_stinger() const override {return active&&active->fixed_stinger();}
    bool dme_keypad_grid() const override {return active&&active->dme_keypad_grid();}
    bool supports_dme() const override { return active?active->supports_dme():false; }
    unsigned wipe_border_maximum() const override {return active?active->wipe_border_maximum():40;}
    unsigned wipe_aspect_default() const override {return active?active->wipe_aspect_default():100u;}
    unsigned wipe_aspect_maximum() const override {return active?active->wipe_aspect_maximum():1000u;}
    unsigned wipe_aspect_minimum() const override {return active?active->wipe_aspect_minimum():1u;}
    bool wipe_aspect_percentage() const override {return active&&active->wipe_aspect_percentage();}
    bool native_wipe_modifiers() const override {return active&&active->native_wipe_modifiers();}
    bool wipe_modifier_supported(unsigned id) const override {return active&&active->wipe_modifier_supported(id);}
    bool prepare_wipe_modifier(unsigned id,uint32_t a,uint32_t b=0) override {return active&&active->prepare_wipe_modifier(id,a,b);}
    bool wipe_modifiers() const override { return active?active->wipe_modifiers():false; }
    bool wipe_style(uint32_t m,uint32_t b,uint32_t w,uint32_t h,uint32_t x,uint32_t y,bool c,bool p) override { return active?active->wipe_style(m,b,w,h,x,y,c,p):false; }
    uint32_t default_softness() const override { return active?active->default_softness():0; }
    bool valid_wipe_code(uint32_t code) const override {return active&&active->valid_wipe_code(code);}
    uint32_t first_direct_dme_code() const override {return active?active->first_direct_dme_code():1001;}
    bool valid_dme_code(uint32_t code) const override {return active&&active->valid_dme_code(code);}
    bool wipe_tiles_supported(uint32_t code) const override {return active&&active->wipe_tiles_supported(code);}
    bool wipe_tiles(uint32_t code,uint32_t size,bool save=false) override {return active&&active->wipe_tiles(code,size,save);}
    bool wipe_geometry_supported(uint32_t code) const override {return active&&active->wipe_geometry_supported(code);}
    bool wipe_geometry(uint32_t code,uint32_t vertices,uint32_t rounding,bool save=false) override {return active&&active->wipe_geometry(code,vertices,rounding,save);}
    const char* wipe_name(uint32_t c) const override {return active?active->wipe_name(c):nullptr;}
    uint32_t wipe_preset(unsigned i) const override { return active?active->wipe_preset(i):0; }
    bool frame_rates() const override { return active?active->frame_rates():false; }
    uint32_t maximum_rate() const override { return active?active->maximum_rate():20000; }
    bool set_transition_rate(TransitionType t,uint32_t f) override {return active&&active->set_transition_rate(t,f);}
    bool set_rate(uint32_t f) override { return active?active->set_rate(f):false; }
};
}
