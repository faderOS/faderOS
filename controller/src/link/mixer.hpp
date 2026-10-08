#pragma once
#include "config.hpp"
#include "panel.hpp"
#include <vector>
#include <string>
namespace bkds::link {
enum class MixerAction : uint8_t { none,preview,cut,program,automatic,dsk_mix,dsk_cut,output,manual,overlay };
enum class OutputKind : uint8_t { stream,record,virtualcam };
enum class TransitionType : uint8_t { mix,wipe,dme,stinger,slide,swipe,dip };
inline bool is_dme(TransitionType t) { return t==TransitionType::dme||t==TransitionType::slide||t==TransitionType::swipe; }
struct WipeBinding { uint32_t code=0; std::array<char,513> name{},reverse{}; std::array<char,65> pattern{}; bool has_pattern=false; };
// Physical entry side: left, right, top, bottom. OBS default movement is left,
// i.e. entry from the right; swipe_in defaults to false.
struct DmeParameters { unsigned entry=1; bool inward=false; };
struct TransitionSettings {
    std::array<char,513> dsk2_name{},dsk2_scene{};
    std::array<char,513> dsk_name{"DSK 1"},dsk_scene{"DSK1"};
    std::array<WipeBinding,32> wipes{};
    // Slot 1 retains legacy names[stinger]; the other keypad slots use these maps.
    std::array<std::array<char,513>,10> stingers{},stinger_reverse{};
    std::array<char,65> wipe_pattern{"linear-h.png"};
    uint32_t softness=3;
    // Empty = discover by kind; never assume translated OBS names.
    std::array<std::array<char,513>,6> names{},reverse_names{};
    std::array<uint32_t,3> rates{300,300,300};
};
enum class TallyBus : uint8_t { none,preview,program };
struct ButtonBinding { MixerAction action=MixerAction::none; uint8_t source=0; };
struct TallyBinding { TallyBus bus=TallyBus::none; uint8_t source=0,level=2; };
struct Mappings {
    TransitionSettings transitions;
    std::array<ButtonBinding,KeyCount> buttons{};
    std::array<TallyBinding,KeyCount> tallies{};
    Mappings();
};
bool valid_mappings(const Mappings&);
struct OverlayChannelState {bool known=false;int source=-1;TallyBus bus=TallyBus::none;};
inline int dme_background_slot(unsigned code){return code==0?0:code==9?1:code==12?2:code==13?3:code==14?4:-1;}
struct SuperSourceWindowState { bool mapped=false;int input=-1;std::array<char,81> id{};std::array<char,49> name{}; };
struct SuperSourceBusState { int source=-1;bool prepared=false;std::array<char,65> name{};std::array<SuperSourceWindowState,24> windows{}; };
struct MixerState {
    struct PresetBackground {uint32_t code=0;int source=-1;bool custom=true;};
    std::array<PresetBackground,288> dme_preset_backgrounds{};
    bool dme_preset_background_supported=false;
    bool mix_preparation_known=false,broadcast_mix_known=false;
    bool dust_mix_known=false;
    std::array<uint32_t,3> dust_values{50,2,0};
    uint32_t dip_rgb=0,mix_preparation_revision=0,super_gain_a=100,super_gain_b=100;
    bool dme_background_scopes_supported=false;std::array<bool,5> dme_background_customs{{true,true,true,true,true}};
    bool dme_background_supported=false;std::array<int,5> dme_backgrounds{{-1,-1,-1,-1,-1}};std::bitset<24> color_sources;
    bool supersources_supported=false;
    std::array<SuperSourceBusState,2> supersources{};
    bool connected=false,studio=false,busy=false,transitioning=false;
    bool auth_required=false;
    bool manual_transition=false;
    int manual_position=-1;
    bool transition_preview_known=false,transition_preview=false;
    bool dsk_known=false,dsk_on=false,dsk_mixing=false;
    bool dsk2_known=false,dsk2_on=false,dsk2_mixing=false;
    std::array<uint32_t,2> dsk_frames{},dsk_rate_revision{};
    bool ftb=false,ftb_known=false,ftb_transitioning=false;
    uint32_t ftb_frames=0,ftb_rate_revision=0;
    bool both_sources=false;
    bool& dsk_known_at(unsigned slot) {return slot?dsk2_known:dsk_known;}
    bool& dsk_on_at(unsigned slot) {return slot?dsk2_on:dsk_on;}
    bool& dsk_mixing_at(unsigned slot) {return slot?dsk2_mixing:dsk_mixing;}
    std::array<bool,3> output_known{},output_active{};
    int preview=-1,program=-1,me=0;
    unsigned sources=0,me_count=0,overlay_channels=0;
    std::array<OverlayChannelState,8> overlays{};
    uint32_t completed_auto=0;
    bool dip_rate_known=false;uint32_t dip_frames=25,dip_rate_revision=0;
    bool dme_rate_known=false;uint32_t dme_frames=25,dme_rate_revision=0;
    uint32_t wipe_vertices=5,wipe_rounding=15;
    bool wipe_geometry_known=false,wipe_mosaic_supported=false;
    uint32_t wipe_tile_size=10;
    bool wipe_rate_known=false;uint32_t wipe_frames=25,wipe_rate_revision=0;
    bool rate_known=false;
    uint32_t auto_frames=25;
    unsigned rate_revision=0;
    std::bitset<24> available;
    std::array<int,4> aux_sources{{-1,-1,-1,-1}};
    std::array<char,80> message{};
    std::bitset<4> key_available;
    bool keyers_known=false,next_known=false,next_background=true;
    std::array<int,4> key_premult{{-1,-1,-1,-1}};std::array<int,2> dsk_premult{{-1,-1}};
    bool key_luma_supported=false,key_invert_supported=false;
    std::array<int,4> key_mask{{-1,-1,-1,-1}},key_submask{{-1,-1,-1,-1}},key_border{{-1,-1,-1,-1}},key_shadow{{-1,-1,-1,-1}},key_invert{{-1,-1,-1,-1}};
    std::array<int,2> dsk_mask{{-1,-1}},dsk_invert{{-1,-1}};
    uint32_t command_failures=0;
    uint8_t media_clear_status=0; // idle, pending, confirmed, unconfirmed
    int media_clear_slot=-1;
    uint8_t media_capture_status=0; // idle, pending, confirmed, unconfirmed
    int media_capture_slot=-1;
    bool media_program_known=false,media_on_program=false;
    bool media_known=false;unsigned media_still_slots=0;int media_still=-1;
    std::array<int,4> key_kind{{-1,-1,-1,-1}},key_cut_source{{-1,-1,-1,-1}};
    std::array<int,2> dsk_kind{{-1,-1}},dsk_cut_source{{-1,-1}};
    std::array<int,4> key_source{{-1,-1,-1,-1}};
    std::array<bool,4> key_on{};
    std::array<bool,4> next_key{};
    std::array<int,2> dsk_source{{-1,-1}};
};
struct OverlayLayout {
    bool program_left=true;
    uint8_t name_edge=1,name_align=1; // edge: 0 top, 1 bottom. align: 0 left, 1 center, 2 right.
    uint8_t clock_edge=0,clock_align=1;
    bool safe_preview=true,safe_program=false,meters=false,known=false;
    uint8_t safe_preview_aspect=0,safe_program_aspect=0,safe_preset=0;
    unsigned revision=0;
};
inline const char* safe_aspect_name(unsigned value) {
    static const char* names[]={"16:9","4:3","9:16","14:9","1:1","4:5"};
    return value<6?names[value]:names[0];
}
struct VideoFormats {
    int current=-1;unsigned revision=0;std::vector<unsigned> supported;
};
inline const char* video_format_name(unsigned mode) {
    static const char* names[]={"525i59.94","625i50","525i59.94 16:9","625i50 16:9","720p50","720p59.94","1080i50","1080i59.94","1080p23.98","1080p24","1080p25","1080p29.97","1080p50","1080p59.94","2160p23.98","2160p24","2160p25","2160p29.97","2160p50","2160p59.94","4320p23.98","4320p24","4320p25","4320p29.97","4320p50","4320p59.94","1080p30","1080p60"};
    return mode<28?names[mode]:"UNKNOWN FORMAT";
}
struct OutputSource {unsigned id=0;std::array<char,41> name{};};
struct OutputRoutes {
    std::array<std::vector<OutputSource>,16> choices;
    unsigned count=0,revision=0;int program=-1,multiview=-1;
    std::array<int,16> sources{};
    std::array<std::array<char,8>,16> labels{};
    OutputRoutes(){sources.fill(-1);}
};
struct MultiviewSettings {
    struct Window {int source=-1;bool present=false,safe_supported=false,meters_supported=false;int safe=-1,meters=-1;};
    unsigned count=0,revision=0;
    std::array<Window,16> windows{};
};
inline bool multiview_input(const MultiviewSettings::Window& w) {return w.present&&w.source>=0&&!(w.source>=10010&&w.source<=10081);}
struct ServerInfo {std::vector<std::pair<std::string,std::string>> fields;};
struct MixerAdapter {
    virtual bool server_info(ServerInfo&) const {return false;}
    virtual bool video_formats(VideoFormats&) const {return false;}
    virtual bool set_video_format(unsigned,int) {return false;}
    virtual bool output_routes(OutputRoutes&) const {return false;}
    virtual bool set_output_source(unsigned,unsigned) {return false;}
    virtual bool set_output_route(unsigned,bool) {return false;}
    virtual bool multiview_settings(unsigned,MultiviewSettings&) const {return false;}
    virtual bool set_multiview_inputs(unsigned,bool,bool) {return false;}
    virtual bool set_multiview_window(unsigned,unsigned,bool,bool) {return false;}
    virtual void configure(const Config&) {}
    virtual void deactivate() {}
    virtual MixerState state()=0;
    virtual bool set_aux_source(unsigned,unsigned) { return false; }
    virtual bool set_multiview_bank(bool) { return false; }
    virtual bool overlay_layout(OverlayLayout&) const { return false; }
    virtual bool set_overlay_layout(const OverlayLayout&) { return false; }
    virtual void dme_parameters(DmeParameters) {}
    virtual bool manual(uint16_t,TransitionType,uint32_t=0,bool=false,uint32_t=3) {return false;}
    virtual void cancel_manual() {}
    virtual bool manual_me_pickup() const {return false;}
    virtual bool supports_transition_preview() const { return false; }
    virtual bool set_transition_preview(bool) { return false; }
    virtual bool output(OutputKind) { return false; }
    virtual unsigned dsk_channels() const { return 2; }
    virtual bool set_dsk_rate(uint32_t,unsigned=0) { return false; }
    virtual bool dsk(bool,uint32_t,unsigned=0) { return false; }
    virtual bool overlay_delegation() const {return false;}
    virtual bool toggle_overlay(unsigned,unsigned,bool) {return false;}
    virtual bool set_supersource_input(unsigned,unsigned,unsigned) {return false;}
    virtual bool keyers() const { return false; }
    virtual bool me_delegation() const { return keyers(); }
    virtual bool set_me(unsigned) { return false; }
    virtual bool set_key_source(unsigned,int) { return false; }
    virtual bool toggle_key(unsigned) { return false; }
    virtual bool set_dsk_source(unsigned,int) { return false; }
    virtual bool set_next_transition(bool,const std::array<bool,4>&) { return false; }
    virtual bool native_key_controls() const {return false;}
    // Delegation: upstream slots 0..3, downstream group 4 with separate DSK slot.
    virtual bool key_setting(unsigned,unsigned,unsigned,int=-1) {return false;}
    virtual bool supports_media_capture() const {return false;}
    virtual bool capture_media_still() {return false;}
    virtual bool clear_media_still(unsigned) {return false;}
    virtual bool select_media_still(unsigned) {return false;}
    virtual bool key_source_selection() const { return keyers(); }
    virtual bool toggle_next_background() { return false; }
    virtual bool toggle_next_key(unsigned) { return false; }
    // Accepted for processing, never an optimistic tally confirmation.
    virtual bool request(MixerAction,unsigned source)=0;
    virtual bool automatic(TransitionType,uint32_t,uint32_t=0,bool=false,uint32_t=3) { return false; }
    virtual bool supports_ftb() const { return keyers(); }
    virtual bool set_ftb_rate(uint32_t) { return false; }
    virtual bool fade_to_black(uint32_t) { return false; }
    virtual bool live_transition_controls() const {return false;}
    virtual bool live_bus_changes() const {return false;}
    virtual bool supports_mix_preparation(bool) const {return false;}
    virtual bool prepare_dust(const std::array<uint32_t,3>&){return false;}
    virtual bool prepare_mix(bool,uint32_t,uint32_t=0) {return false;}
    virtual bool mix_dip() const {return false;}
    virtual unsigned stinger_slots() const {return 10;}
    virtual unsigned first_stinger_slot() const {return 0;}
    virtual bool fixed_stinger() const {return false;}
    virtual bool dme_keypad_grid() const {return false;}
    virtual bool supports_dme() const { return true; }
    virtual int dme_background(unsigned code) {auto view=state();const int slot=dme_background_slot(code);if(slot>=0)return view.dme_backgrounds[unsigned(slot)];for(const auto& entry:view.dme_preset_backgrounds)if(entry.code==code)return entry.source;return -1;}
    virtual bool dme_background_custom(unsigned code){auto view=state();int slot=dme_background_slot(code);if(slot>=0)return view.dme_background_customs[unsigned(slot)];for(const auto& entry:view.dme_preset_backgrounds)if(entry.code==code)return entry.custom;return true;}
    virtual bool set_dme_background_scope(unsigned,bool,bool=false){return false;}
    virtual bool supports_dme_background(unsigned) const {return false;}
    virtual bool set_dme_background(unsigned,int) {return false;}
    virtual unsigned keypad_transition_slots(TransitionType) const {return 0;}
    virtual unsigned first_keypad_transition_slot(TransitionType) const {return 1;}
    virtual bool keypad_transition_available(TransitionType,unsigned) const {return true;}
    virtual std::string keypad_transition_label(TransitionType,unsigned) const {return "";}
    virtual std::string dme_label(unsigned slot) const {return slot==0?"MOVE":slot==1?"SLIDE":"SWIPE";}
    virtual bool supports_dme_parameters() const {return true;}
    virtual unsigned wipe_border_maximum() const {return 40;}
    virtual bool native_wipe_modifiers() const {return false;}
    virtual bool wipe_aspect_percentage() const {return native_wipe_modifiers();}
    virtual unsigned wipe_aspect_default() const {return native_wipe_modifiers()?50u:100u;}
    virtual unsigned wipe_aspect_maximum() const {return native_wipe_modifiers()?100u:1000u;}
    virtual unsigned wipe_aspect_minimum() const {return native_wipe_modifiers()?0u:1u;}
    virtual bool wipe_modifier_supported(unsigned) const {return wipe_modifiers();}
    virtual bool prepare_wipe_modifier(unsigned,uint32_t,uint32_t=0) {return false;}
    virtual bool wipe_modifiers() const { return false; }
    virtual bool wipe_style(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,bool,bool) { return false; }
    virtual uint32_t default_softness() const { return 3; }
    virtual bool valid_wipe_code(uint32_t) const {return true;}
    virtual uint32_t first_direct_dme_code() const {return 1001;}
    virtual bool valid_dme_code(uint32_t) const {return false;}
    virtual bool wipe_tiles_supported(uint32_t) const {return false;}
    virtual bool wipe_tiles(uint32_t,uint32_t,bool=false) {return false;}
    virtual bool wipe_geometry_supported(uint32_t) const {return false;}
    virtual bool wipe_geometry(uint32_t,uint32_t,uint32_t,bool=false) {return false;}
    virtual const char* wipe_name(uint32_t) const {return nullptr;}
    virtual uint32_t wipe_preset(unsigned index) const {
        static const uint32_t codes[]={23,5,21,24,18,9,6,1,3,17};
        return index<10?codes[index]:0;
    }
    virtual bool frame_rates() const { return false; }
    virtual uint32_t maximum_rate() const { return frame_rates()?1000:20000; }
    virtual bool set_rate(uint32_t) { return false; }
    virtual bool set_transition_rate(TransitionType,uint32_t frames) {return set_rate(frames);}
    virtual ~MixerAdapter()=default;
};
class MixerControl {
    Panel& panel;
    MixerAdapter& adapter;
    const Mappings& mappings;
    bool shifted=false,shift_locked=false;
    bool shift_lamp_grace=false;
    uint32_t shift_pressed_at=0;
public:
    MixerControl(Panel& p,MixerAdapter& a,const Mappings& m):panel(p),adapter(a),mappings(m) {}
    bool press(const std::bitset<KeyCount>&);
    bool press_output(const std::bitset<KeyCount>&);
    void refresh(uint32_t now=0);
    void refresh_shift(uint32_t now);
    void shift(const Changes& change,uint32_t now=0);
    void sync_shift(bool held) { shift_locked=false; shifted=held; shift_lamp_grace=false; }
    bool second_layer() const { return shifted; }
};
}
