#pragma once
#include "panel.hpp"
#include "config.hpp"
#include "mixer.hpp"
#include "panel_test.hpp"
namespace bkds::link {
class Setup {
    enum class Page { closed,root,test,network,mixer,protocol,edit,clock,info,server_setup,kavtor_buses,kavtor_names,kavtor_clocks,kavtor_safe,kavtor_meters,atem_multiview,atem_outputs,atem_format,server_info } page=Page::closed;
    Panel& panel;
    PanelTest diagnostic;
    Configuration& configuration;
    MixerAdapter* video=nullptr;
    unsigned info_index=0;
    unsigned format_index=0;int format_previous=-1;bool format_confirm=false;
    unsigned shown_formats=0;
    unsigned shown_layout=0,shown_multiview=0,shown_routes=0,output_index=0,multiview_index=0,multiview_window=0;
    void move_multiview(int,int);
    void move_output(int);
    Config draft;
    uint32_t draft_revision=0;
    unsigned field=0;
    IPv4 original_ip{};
    uint16_t original_port=0;
    unsigned selected=0;
    bool apply_requested=false,conflict=false;
    bool replace_digit=true,keypad=false,numeric_invalid=false;
    unsigned keypad_value=0;
    const char* error=nullptr;
    std::array<char,32> clock_text{};
    std::array<char,16> firmware_version{};
    void render();
    void begin_edit(unsigned);
    unsigned value(unsigned) const;
    void value(unsigned,unsigned);
    void finish_edit(bool);
    bool save();
    bool kavtor_page() const;
    OverlayLayout current_layout() const;
    bool push_layout(OverlayLayout layout);
public:
    Setup(Panel& p,Configuration& c):panel(p),diagnostic(p),configuration(c),draft(c.saved()) {}
    // Cumulative encoder movement predates a simultaneous navigation edge.
    void handle(const Changes& c) { rotate(c.rotary); press(c.pressed); }
    void press(const std::bitset<KeyCount>&);
    void rotate(const std::array<int32_t,6>&);
    void firmware_info(const char* version);
    void host_clock(const char* utc);
    const Config& settings() const { return configuration.saved(); }
    bool take_apply_request() { bool result=apply_requested; apply_requested=false; return result; }
    bool testing() const { return diagnostic.active(); }
    void test_tick(uint32_t now) { diagnostic.tick(now); }
    void test_analog(unsigned axis,uint16_t value) { diagnostic.analog_sample(axis,value); }
    void test_inputs(const Snapshot& s,const Changes& c) {
        diagnostic.input(s,c);
        if(!diagnostic.active()){page=Page::root;render();}
    }
    bool active() const { return page!=Page::closed; }
    void attach_mixer(MixerAdapter* adapter) { video=adapter; }
    bool claims(unsigned id) const;
    bool claims_encoders() const { return page==Page::edit||page==Page::atem_multiview||page==Page::atem_format||page==Page::atem_outputs; }
    void sync_overlay();
    void adapter_changed();
    void cancel();
    void close_menu() { std::bitset<KeyCount> keys; keys.set(kavtor_page()||(page==Page::atem_multiview||page==Page::atem_outputs||page==Page::atem_format)||page==Page::server_setup||page==Page::server_info?46:47); if(active())press(keys); }
    bool owns_keypad() const { return keypad; }
    void refresh_keypad();
};
}
