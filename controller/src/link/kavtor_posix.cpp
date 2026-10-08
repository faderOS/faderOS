#include "kavtor_posix.hpp"
#include <nlohmann/json.hpp>
#include <array>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using Json=nlohmann::json;
namespace bkds::link {
using Clock=std::chrono::steady_clock;
namespace {
bool send_all(int fd,const std::string& text) {
    std::size_t sent=0;
    const auto deadline=Clock::now()+std::chrono::milliseconds(500);
    while(sent<text.size()) {
        const auto n=::send(fd,text.data()+sent,text.size()-sent,MSG_NOSIGNAL);
        if(n<0&&errno==EINTR) continue;
        if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK)) {
            const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-Clock::now()).count();
            if(left<=0) return false;
            pollfd wait{fd,POLLOUT,0};
            if(::poll(&wait,1,int(left))<0&&errno!=EINTR) return false;
            continue;
        }
        if(n<=0) return false;
        sent+=std::size_t(n);
    }
    return true;
}
int connect_timeout(const Endpoint& endpoint,int timeout_ms) {
    const int fd=::socket(AF_INET,SOCK_STREAM,0);
    if(fd<0) return -1;
    const int flags=::fcntl(fd,F_GETFL,0);
    if(flags<0||::fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0) { ::close(fd); return -1; }
    sockaddr_in address{};
    address.sin_family=AF_INET;
    address.sin_port=htons(endpoint.port?endpoint.port:9100);
    address.sin_addr.s_addr=htonl((uint32_t(endpoint.host[0])<<24)|(uint32_t(endpoint.host[1])<<16)|(uint32_t(endpoint.host[2])<<8)|endpoint.host[3]);
    const int rc=::connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof address);
    if(rc<0&&errno!=EINPROGRESS) { ::close(fd); return -1; }
    if(rc<0) {
        pollfd wait{fd,POLLOUT,0};
        const int polled=::poll(&wait,1,timeout_ms);
        int error=0; socklen_t len=sizeof error;
        if(polled<=0||::getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&len)<0||error) { ::close(fd); return -1; }
    }
    return fd;
}
}
struct KavtorAdapter::Impl {
    std::mutex mutex;
    std::atomic<bool> stop{false};
    Config config;std::string server_version;
    bool configured=false;
    unsigned generation=0;
    MixerState view;
    MixerAction command=MixerAction::none;
    unsigned source=0;
    OverlayLayout layout;
    bool layout_dirty=false;
    int mv_bank=0; bool bank_dirty=true;
    uint32_t rate_frames=25;
    bool rate_dirty=false;
    TransitionType auto_type=TransitionType::mix;
    uint32_t auto_code=0,auto_softness=0;
    bool auto_reverse=false;
    bool awaiting_take=false,take_seen=false;
    bool manual_active=false,manual_dirty=false,manual_endpoint=false;
    uint16_t manual_position=0;
    Json manual_message;
    uint32_t preview_softness=101;
    bool preview_soft_dirty=false;
    Clock::time_point awaiting_since{},last_state{};
    std::string raw;
    bool raw_take=false;
    bool next_pending=false,next_waiting=false,next_background=true,next_sent_background=true;
    std::array<bool,4> next_keys{},next_sent_keys{};
    int next_me=0;Clock::time_point next_since{};
    Json catalog=Json::array(),stingers=Json::array(),sony_codes=Json::array(),sony_dmes=Json::array();
    bool geometry_supported=false,broadcast_mix_supported=false;
    bool move_supported=false,cube_supported=false,zoom_supported=false,page_curl_supported=false,page_roll_supported=false;
    bool dme_available(unsigned code)const {
        if(code>=1000)return std::any_of(sony_dmes.begin(),sony_dmes.end(),[code](const Json& v){return v.is_number_integer()&&v==code;});
        if(code==0)return move_supported;
        if(code>=1&&code<=8)return native_transitions_supported;
        if(code==9)return cube_supported;
        if(code==12)return zoom_supported;
        if(code==13)return page_curl_supported;
        if(code==14)return page_roll_supported;
        return false;
    }
    static std::string dme_effect(unsigned code){if(code>=1000)return "sony_"+std::to_string(code);return code>=2601&&code<=2604?"push":code>=1001&&code<=1004?"slide":code==0?"move":code==9?"cube":code==12?"zoom":code==13?"page_curl":code==14?"page_roll":code<=4?"push":"slide";}
    struct Style { uint32_t multi=1,border=0,aw=1,ah=1,px=500,py=500; bool cursor=false,save=false;uint32_t vertices=5,rounding=15,tile_size=10; };
    Style style;
    Json background_scopes=Json::object();std::string background_scope_pending;bool background_scope_expected=false;
    bool background_custom(unsigned code) const{return !view.dme_background_scopes_supported||background_scopes.value(dme_effect(code),false);}
    bool style_dirty=false;
    bool dip_dirty=false,super_dirty=false;uint32_t prepared_dip_rgb=0,prepared_gain_a=100,prepared_gain_b=100;
    std::array<uint32_t,10> presets{{23,5,21,24,18,9,6,1,3,17}};
    std::string wipe_pattern="wipe_horizontal",wipe_dir="fwd",wipe_edge="hard",wipe_color="#ffffff";
    int wipe_amount=8,wipe_shadow=0,wipe_border=0,wipe_multi=1,wipe_aw=1,wipe_ah=1,wipe_px=500,wipe_py=500;
    bool dsk_preview=false,key_processing_supported=false,native_transitions_supported=false,dip_supported=false;
    std::array<Json,4> key_processing;std::array<Json,2> dsk_processing;
    std::thread worker;
    int fd=-1;
    std::string incoming;
    Impl() {
        for(int i=0;i<10;++i) stingers.push_back({{"media",""},{"reverse",""},{"cutFrames",12},{"lengthFrames",50}});
        worker=std::thread([this]{ run(); });
    }
    ~Impl() { stop=true; worker.join(); close_fd(); }
    void close_fd() { if(fd>=0) { ::close(fd); fd=-1; } }
    void status(const char* text,bool connected=false) {
        std::lock_guard<std::mutex> lock(mutex);
        view.connected=connected; view.busy=false; view.studio=connected; view.sources=connected?8:0;
        view.available.reset();
        if(connected) for(unsigned i=0;i<8;i++) view.available.set(i);
        if(!connected) {
            view.preview=view.program=-1; view.me=0; view.transitioning=false;
            view.manual_transition=false; manual_active=manual_dirty=manual_endpoint=false;
            view.transition_preview_known=view.transition_preview=false;
            preview_softness=101; preview_soft_dirty=false;
            view.dsk_known=view.dsk_on=view.dsk_mixing=false;
            view.dsk2_known=view.dsk2_on=view.dsk2_mixing=false;
            view.dme_background_scopes_supported=false;background_scopes=Json::object();background_scope_pending.clear();
            sony_codes=Json::array();sony_dmes=Json::array();geometry_supported=false;key_processing_supported=native_transitions_supported=dip_supported=broadcast_mix_supported=false;key_processing={};dsk_processing={};
            view.key_luma_supported=view.key_invert_supported=false;view.key_invert.fill(-1);view.dsk_invert.fill(-1);
            view.key_kind.fill(-1);view.dsk_kind.fill(-1);view.key_mask.fill(-1);view.dsk_mask.fill(-1);
            view.keyers_known=view.next_known=false; view.next_background=true; view.ftb=false; view.both_sources=false;
            next_pending=next_waiting=false;view.key_available.reset();view.key_on.fill(false); view.next_key.fill(false); view.key_source.fill(-1); view.dsk_source.fill(-1);
            command=MixerAction::none; raw.clear(); raw_take=false; awaiting_take=take_seen=false; layout.known=false;
        }
        std::snprintf(view.message.data(),view.message.size(),"%s",text);
    }
    void pause(unsigned ms) { for(unsigned i=0;i<ms&&!stop;i+=20) std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    bool send_line(const Json& message) { return fd>=0&&send_all(fd,message.dump()+"\n"); }
    void apply(const Json& message) {
        if(!message.is_object()||!message.contains("event")||!message.at("event").is_string()) return;
        const auto event=message.at("event").get<std::string>();
        if(event=="error") {
            std::lock_guard<std::mutex> lock(mutex);
            if(message.value("cmd",std::string{})=="supersource_input"||message.value("cmd",std::string{})=="dme_background") {
                background_scope_pending.clear();
                ++view.command_failures;
                if(message.contains("message")&&message.at("message").is_string()){auto text=message.at("message").get<std::string>();std::snprintf(view.message.data(),view.message.size(),"%s",text.c_str());}
                return;
            }
            awaiting_take=take_seen=false;next_pending=next_waiting=false; view.busy=false;
            manual_active=manual_dirty=manual_endpoint=false;view.manual_transition=false;
            if(message.contains("message")&&message.at("message").is_string()) {
                const auto text=message.at("message").get<std::string>();
                std::snprintf(view.message.data(),view.message.size(),"%s",text.c_str());
            }
            return;
        }
        if(event=="catalog") {
            if(message.contains("patterns")&&message.at("patterns").is_array()) {
                std::lock_guard<std::mutex> lock(mutex);
                catalog=message.at("patterns");
            }
            return;
        }
        if(event!="state"&&event!="tally") return;
        auto number=[&](const char* key,int fallback) {
            if(!message.contains(key)||!message.at(key).is_number_integer()) return fallback;
            return message.at(key).get<int>();
        };
        auto text_field=[&](const char* key,std::string& slot) {
            if(message.contains(key)&&message.at(key).is_string()) slot=message.at(key).get<std::string>();
        };
        std::lock_guard<std::mutex> lock(mutex);
        if(message.contains("serverVersion")&&message.at("serverVersion").is_string())server_version=message.at("serverVersion").get<std::string>();
        last_state=Clock::now();
        view.connected=true; view.studio=true; view.sources=8; view.me_count=4;
        if(!view.transitioning&&!manual_active&&!awaiting_take&&!next_pending&&!next_waiting&&command==MixerAction::none&&raw.empty()) view.busy=false;
        if(event=="state") {
            view.aux_sources.fill(-1);
            if(message.contains("aux")&&message.at("aux").is_array())for(unsigned i=0;i<4&&i<message.at("aux").size();++i)if(message.at("aux").at(i).is_number_integer())view.aux_sources[i]=message.at("aux").at(i).get<int>();
            view.available.reset();
            bool listed=false;
            if(message.contains("assigned")&&message.at("assigned").is_array()) {
                listed=true;
                const auto& rows=message.at("assigned");
                view.sources=unsigned(std::min<std::size_t>(24,rows.size()));
                for(unsigned i=0;i<view.sources;++i)
                    if(rows.at(i).is_boolean()&&rows.at(i).get<bool>()) view.available.set(i);
            }
            if(!listed) for(unsigned i=0;i<8;i++) view.available.set(i);
            if(message.contains("autoFrames")&&message.at("autoFrames").is_number_integer()) {
                const int frames=message.at("autoFrames").get<int>();
                if(frames>=1&&frames<=1000) {
                    const auto next=uint32_t(frames);
                    if(!view.rate_known||view.auto_frames!=next) { view.auto_frames=next; ++view.rate_revision; }
                    view.rate_known=true;
                }
            }
        }
        view.preview=number("preview",view.preview);
        view.program=number("program",view.program);
        const bool me_changed=event=="state"&&number("me",view.me)!=view.me;
        if(event=="state") {
            const int slot=number("me",view.me);
            if(slot>=0&&slot<4) view.me=slot;
        }
        if(message.contains("transitionPreview")&&message.at("transitionPreview").is_boolean()) {
            view.transition_preview_known=true;
            view.transition_preview=message.at("transitionPreview").get<bool>();
        }
        if(me_changed) {
            manual_active=manual_dirty=manual_endpoint=false;awaiting_take=take_seen=false;
        }
        if(event=="state"&&message.contains("position")) view.manual_position=number("position",0);
        if(event=="state"&&message.contains("manual")&&message.at("manual").is_boolean())
            view.manual_transition=message.at("manual").get<bool>();
        if(message.contains("transitioning")&&message.at("transitioning").is_boolean()) {
            const bool now=message.at("transitioning").get<bool>();
            if(now) { if(awaiting_take) take_seen=true; }
            else if(take_seen) {
                if(!view.transition_preview&&(!manual_active||manual_position==4095)) ++view.completed_auto;
                take_seen=false; awaiting_take=false; view.busy=false;
                manual_active=manual_dirty=manual_endpoint=false; view.manual_transition=false;
            }
            view.transitioning=now;
            if(me_changed&&view.manual_transition) {
                manual_active=take_seen=awaiting_take=true;
                manual_position=uint16_t(view.manual_position);
                manual_message={{"cmd","manual"},{"type","mix"},{"position",manual_position}};
            }
            if(event=="state"&&(now||me_changed)) view.busy=now;

        }
        if(event=="state"&&message.contains("ftb")&&message.at("ftb").is_boolean())
            view.ftb=message.at("ftb").get<bool>();
        if(event=="state"&&message.contains("dsk")&&message.at("dsk").is_boolean()) {
            view.dsk_known=true; view.dsk_on=message.at("dsk").get<bool>();
        }
        if(event=="state"&&message.contains("dskPreview")&&message.at("dskPreview").is_boolean())
            dsk_preview=message.at("dskPreview").get<bool>();
        if(event=="state"&&message.contains("capabilities")&&message.at("capabilities").is_object()) {
            const auto& caps=message.at("capabilities");
            view.mix_preparation_known=caps.contains("mixPreparation")&&caps.at("mixPreparation").is_boolean()&&caps.at("mixPreparation").get<bool>();
            broadcast_mix_supported=caps.contains("broadcastMixes")&&caps.at("broadcastMixes").is_boolean()&&caps.at("broadcastMixes").get<bool>();
            geometry_supported=caps.contains("sonyGeometry")&&caps.at("sonyGeometry").is_boolean()&&caps.at("sonyGeometry").get<bool>();
            view.dme_background_scopes_supported=caps.contains("dmeBackgroundScopes")&&caps.at("dmeBackgroundScopes").is_boolean()&&caps.at("dmeBackgroundScopes").get<bool>();
            view.dme_background_supported=caps.contains("dmeBackground")&&caps.at("dmeBackground").is_boolean()&&caps.at("dmeBackground").get<bool>();
            key_processing_supported=caps.contains("keyModes")&&caps.at("keyModes").is_array();
            view.key_luma_supported=key_processing_supported&&std::any_of(caps.at("keyModes").begin(),caps.at("keyModes").end(),[](const Json& mode){return mode=="luma";});
            view.key_invert_supported=caps.contains("keyInversion")&&caps.at("keyInversion").is_boolean()&&caps.at("keyInversion").get<bool>();
            sony_dmes=caps.contains("sonyDmes")&&caps.at("sonyDmes").is_array()?caps.at("sonyDmes"):Json::array();
            if(caps.contains("sonyWipes")&&caps.at("sonyWipes").is_array())sony_codes=caps.at("sonyWipes");
            dip_supported=caps.contains("mixModes")&&caps.at("mixModes").is_array()
                &&std::any_of(caps.at("mixModes").begin(),caps.at("mixModes").end(),[](const Json& mode){return mode.is_string()&&mode=="dip";});
            move_supported=caps.contains("dmeEffects")&&caps.at("dmeEffects").is_array()&&std::any_of(caps.at("dmeEffects").begin(),caps.at("dmeEffects").end(),[](const Json& e){return e=="move";});
            cube_supported=caps.contains("dmeEffects")&&caps.at("dmeEffects").is_array()&&std::any_of(caps.at("dmeEffects").begin(),caps.at("dmeEffects").end(),[](const Json& e){return e=="cube";});
            auto effect=[&](const char* name){return caps.contains("dmeEffects")&&caps.at("dmeEffects").is_array()&&std::any_of(caps.at("dmeEffects").begin(),caps.at("dmeEffects").end(),[&](const Json& e){return e==name;});};
            zoom_supported=effect("zoom");page_curl_supported=effect("page_curl");page_roll_supported=effect("page_roll");
            native_transitions_supported=caps.contains("mixModes")&&caps.at("mixModes").is_array()
                &&caps.contains("dmeEffects")&&caps.at("dmeEffects").is_array();
        }
        if(event=="state"||(event=="tally"&&message.contains("supersources"))) {
            if(event=="state")view.supersources_supported=message.contains("capabilities")&&message.at("capabilities").is_object()&&message.at("capabilities").contains("supersources")&&message.at("capabilities").at("supersources").is_boolean()&&message.at("capabilities").at("supersources").get<bool>();
            view.supersources={};
            if(view.supersources_supported&&message.contains("supersources")&&message.at("supersources").is_array()) {
                for(unsigned side=0;side<2&&side<message.at("supersources").size();++side) {
                    const auto& item=message.at("supersources").at(side);if(!item.is_object()||!item.contains("source")||!item.at("source").is_number_integer())continue;
                    int source=item.at("source").get<int>();if(source<0||source>=24||!item.contains("boxes")||!item.at("boxes").is_array()||item.at("boxes").size()>32)continue;
                    auto& target=view.supersources[side];target.source=source;target.prepared=item.contains("prepared")&&item.at("prepared").is_boolean()&&item.at("prepared").get<bool>();auto name=item.contains("name")&&item.at("name").is_string()?item.at("name").get<std::string>():std::string{};std::snprintf(target.name.data(),target.name.size(),"%s",name.c_str());
                    for(const auto& box:item.at("boxes")) {
                        if(!box.is_object()||!box.contains("button")||!box.at("button").is_number_integer()||!box.contains("id")||!box.at("id").is_string()||!box.contains("input")||!box.at("input").is_number_integer())continue;
                        int button=box.at("button").get<int>(),input=box.at("input").get<int>();auto id=box.at("id").get<std::string>();if(button<0||button>=24||input < -1||(input>=24&&(input<1000||input>1003))||id.empty()||id.size()>80)continue;
                        auto& window=target.windows[button];window.mapped=true;window.input=input;std::snprintf(window.id.data(),window.id.size(),"%s",id.c_str());auto title=box.contains("name")&&box.at("name").is_string()?box.at("name").get<std::string>():std::string{};std::snprintf(window.name.data(),window.name.size(),"%s",title.c_str());
                    }
                }
            }
        }
        if(message.contains("dmeBackgroundScopes")&&message.at("dmeBackgroundScopes").is_object()){
            background_scopes=Json::object();for(auto i=message.at("dmeBackgroundScopes").begin();i!=message.at("dmeBackgroundScopes").end();++i)if(i.value().is_boolean())background_scopes[i.key()]=i.value();
            if(!background_scope_pending.empty()&&background_scopes.value(background_scope_pending,false)==background_scope_expected)background_scope_pending.clear();
        }
        const unsigned background_codes[]={0,9,12,13,14};for(unsigned i=0;i<5;++i)view.dme_background_customs[i]=background_custom(background_codes[i]);
        if(message.contains("dmeBackgrounds")&&message.at("dmeBackgrounds").is_object()){const char* effects[]={"move","cube","zoom","page_curl","page_roll"};view.dme_backgrounds.fill(-1);for(unsigned i=0;i<5;++i){const auto& settings=message.at("dmeBackgrounds");if(settings.contains(effects[i])&&settings.at(effects[i]).is_object()&&settings.at(effects[i]).contains("image")&&settings.at(effects[i]).at("image").is_string()&&!settings.at(effects[i]).at("image").get<std::string>().empty()){view.dme_backgrounds[i]=-3;}else if(settings.contains(effects[i])&&settings.at(effects[i]).is_number_integer()){int input=settings.at(effects[i]).get<int>();if(input==-1||(input>=0&&input<24)||(input>=1000&&input<=1003))view.dme_backgrounds[i]=input;}}}
        if(message.contains("wipeVertices")&&message.at("wipeVertices").is_number_integer()&&message.at("wipeVertices")>=3&&message.at("wipeVertices")<=64)view.wipe_vertices=message.at("wipeVertices").get<unsigned>();
        if(message.contains("wipeRounding")&&message.at("wipeRounding").is_number_integer()&&message.at("wipeRounding")>=0&&message.at("wipeRounding")<=50)view.wipe_rounding=message.at("wipeRounding").get<unsigned>();
        if(event=="state"&&message.contains("capabilities")&&message.at("capabilities").is_object()) {
            const auto& c=message.at("capabilities");view.dme_preset_background_supported=c.contains("sonyDmeBackground")&&c.at("sonyDmeBackground").is_boolean()&&c.at("sonyDmeBackground").get<bool>();
        }
        if(message.contains("dmeBackgrounds")&&message.at("dmeBackgrounds").is_object()){
            view.dme_preset_backgrounds.fill(MixerState::PresetBackground{});unsigned index=0;
            for(const auto& item:sony_dmes){if(index>=view.dme_preset_backgrounds.size()||!item.is_number_integer())break;
                unsigned code=item.get<unsigned>();int source=-1;const auto key="sony_"+std::to_string(code);const auto& settings=message.at("dmeBackgrounds");
                if(settings.contains(key)){const auto& value=settings.at(key);if(value.is_number_integer()){auto n=value.get<int>();if(n==-1||(n>=0&&n<24)||(n>=1000&&n<=1003))source=n;}else if(value.is_object()&&value.contains("image")&&value.at("image").is_string()&&!value.at("image").get<std::string>().empty())source=-3;}
                view.dme_preset_backgrounds[index++]={code,source,background_custom(code)};
            }
        }
        view.broadcast_mix_known=broadcast_mix_supported;
        if(message.contains("dipColor")&&message.at("dipColor").is_string()){const auto text=message.at("dipColor").get<std::string>();if(text.size()==7&&text[0]=='#'&&std::all_of(text.begin()+1,text.end(),[](unsigned char c){return std::isxdigit(c)!=0;})){auto rgb=uint32_t(std::strtoul(text.c_str()+1,nullptr,16));if(rgb!=view.dip_rgb){view.dip_rgb=rgb;++view.mix_preparation_revision;}}}
        if(message.contains("superMixGainA")&&message.contains("superMixGainB")&&message.at("superMixGainA").is_number_integer()&&message.at("superMixGainB").is_number_integer()){
            int a=message.at("superMixGainA").get<int>(),b=message.at("superMixGainB").get<int>();if(a>=0&&a<=100&&b>=0&&b<=100&&(unsigned(a)!=view.super_gain_a||unsigned(b)!=view.super_gain_b)){view.super_gain_a=unsigned(a);view.super_gain_b=unsigned(b);++view.mix_preparation_revision;}}
        view.wipe_geometry_known=geometry_supported;
        if(event=="state"&&message.contains("capabilities")&&message.at("capabilities").is_object()) view.wipe_mosaic_supported=message.at("capabilities").contains("sonyMosaic")&&message.at("capabilities").at("sonyMosaic").is_boolean()&&message.at("capabilities").at("sonyMosaic").get<bool>();
        if(message.contains("wipeTileSize")&&message.at("wipeTileSize").is_number_integer()&&message.at("wipeTileSize")>=2&&message.at("wipeTileSize")<=50)view.wipe_tile_size=message.at("wipeTileSize").get<unsigned>();
        if(message.contains("colorSources")&&message.at("colorSources").is_array()){view.color_sources.reset();for(auto& value:message.at("colorSources"))if(value.is_number_integer()){int input=value.get<int>();if(input>=0&&input<24)view.color_sources.set(unsigned(input));}}
        auto processing=[&](const Json& row,unsigned i,bool dsk) {
            if(!row.contains("processing")||!row.at("processing").is_object())return;
            const auto& params=row.at("processing");
            (dsk?dsk_processing[i]:key_processing[i])=params;
            const auto mode=params.value("mode",std::string("linear"));
            (dsk?view.dsk_kind[i]:view.key_kind[i])=mode=="chroma"?91:mode=="luma"?88:89;
            (dsk?view.dsk_mask[i]:view.key_mask[i])=params.value("mask",false)?1:0;
            (dsk?view.dsk_invert[i]:view.key_invert[i])=params.value("invert",false)?1:0;
        };
        if(event=="state"&&message.contains("keys")&&message.at("keys").is_array()) {
            const auto& rows=message.at("keys");
            view.key_available.reset();
            for(unsigned i=0;i<4&&i<rows.size();++i) {
                if(!rows.at(i).is_object()) continue;
                view.key_available[i]=true;
                const auto& row=rows.at(i);
                processing(row,i,false);
                if(row.contains("source")&&row.at("source").is_number_integer()) view.key_source[i]=row.at("source").get<int>();
                if(row.contains("on")&&row.at("on").is_boolean()) view.key_on[i]=row.at("on").get<bool>();
            }
            view.keyers_known=true;
        }
        if(event=="state"&&message.contains("dsks")&&message.at("dsks").is_array()) {
            const auto& rows=message.at("dsks");
            for(unsigned i=0;i<2&&i<rows.size();++i) {
                if(!rows.at(i).is_object()) continue;
                const auto& row=rows.at(i);
                processing(row,i,true);
                if(row.contains("source")&&row.at("source").is_number_integer()) view.dsk_source[i]=row.at("source").get<int>();
                if(row.contains("on")&&row.at("on").is_boolean()) {
                    view.dsk_on_at(i)=row.at("on").get<bool>();
                    view.dsk_known_at(i)=true;
                }
                if(row.contains("mixing")&&row.at("mixing").is_boolean()) {
                    view.dsk_mixing_at(i)=row.at("mixing").get<bool>();
                }
            }
        }
        if(event=="state"&&message.contains("next")&&message.at("next").is_object()) {
            const auto& next=message.at("next");
            if(next.contains("background")&&next.at("background").is_boolean())
                view.next_background=next.at("background").get<bool>();
            if(next.contains("keys")&&next.at("keys").is_array()) {
                const auto& flags=next.at("keys");
                for(unsigned i=0;i<4&&i<flags.size();++i)
                    if(flags.at(i).is_boolean()) view.next_key[i]=flags.at(i).get<bool>();
            }
            view.next_known=true;
        }
        if(event=="state") {
            std::string take;
            if(message.contains("take")&&message.at("take").is_string()) take=message.at("take").get<std::string>();
            const bool dissolve=take=="mix"||take=="wipe"||take=="dme";
            view.both_sources=view.transitioning&&!view.transition_preview&&!view.ftb&&dissolve&&view.next_background&&view.preview>=0&&view.preview!=view.program;
            text_field("wipePattern",wipe_pattern); text_field("wipeDir",wipe_dir);
            text_field("wipeEdge",wipe_edge); text_field("wipeBorderColor",wipe_color);
            if(message.contains("wipeEdgeAmount")&&message.at("wipeEdgeAmount").is_number_integer())
                wipe_amount=message.at("wipeEdgeAmount").get<int>();
            if(message.contains("stingers")&&message.at("stingers").is_array()) stingers=message.at("stingers");
            if(message.contains("wipePresets")&&message.at("wipePresets").is_array()&&message.at("wipePresets").size()==10) {
                std::array<uint32_t,10> next{};
                bool ok=true;
                for(unsigned i=0;i<10;++i) {
                    const auto& row=message.at("wipePresets").at(i);
                    if(!row.is_number_integer()) { ok=false; break; }
                    const int code=row.get<int>();
                    if(code<0||code>999) { ok=false; break; }
                    next[i]=uint32_t(code);
                }
                if(ok) presets=next;
            }
            auto bounded=[&](const char* key,int& slot,int lo,int hi) {
                if(!message.contains(key)||!message.at(key).is_number_integer()) return;
                const int value=message.at(key).get<int>();
                if(value>=lo&&value<=hi) slot=value;
            };
            bounded("wipeMulti",wipe_multi,1,16);
            bounded("wipeBorder",wipe_border,0,40);
            bounded("wipeShadow",wipe_shadow,0,40);
            bounded("wipeAspectW",wipe_aw,1,1000);
            bounded("wipeAspectH",wipe_ah,1,1000);
            bounded("wipePosX",wipe_px,0,1000);
            bounded("wipePosY",wipe_py,0,1000);
        }
        if(event=="state"&&message.contains("layout")&&message.at("layout").is_object()) {
            const auto& spec=message.at("layout");
            OverlayLayout next=layout;
            auto edge=[&](const char* key,uint8_t& slot) {
                if(!spec.contains(key)||!spec.at(key).is_string()) return;
                const auto text=spec.at(key).get<std::string>();
                if(text=="top") slot=0; else if(text=="bottom") slot=1;
            };
            auto align=[&](const char* key,uint8_t& slot) {
                if(!spec.contains(key)||!spec.at(key).is_string()) return;
                const auto text=spec.at(key).get<std::string>();
                if(text=="left") slot=0; else if(text=="center") slot=1; else if(text=="right") slot=2;
            };
            if(spec.contains("programLeft")&&spec.at("programLeft").is_boolean()) next.program_left=spec.at("programLeft").get<bool>();
            edge("nameEdge",next.name_edge); align("nameAlign",next.name_align);
            edge("clockEdge",next.clock_edge); align("clockAlign",next.clock_align);
            if(spec.contains("safePreview")&&spec.at("safePreview").is_boolean()) next.safe_preview=spec.at("safePreview").get<bool>();
            if(spec.contains("safeProgram")&&spec.at("safeProgram").is_boolean()) next.safe_program=spec.at("safeProgram").get<bool>();
            if(spec.contains("meters")&&spec.at("meters").is_boolean()) next.meters=spec.at("meters").get<bool>();
            for(unsigned i=0;i<6;++i) {
                if(spec.value("safePreviewAspect",std::string("16:9"))==safe_aspect_name(i)) next.safe_preview_aspect=uint8_t(i);
                if(spec.value("safeProgramAspect",std::string("16:9"))==safe_aspect_name(i)) next.safe_program_aspect=uint8_t(i);
            }
            next.safe_preset=spec.value("safePreset",std::string("ebu-r95"))=="legacy"?1:0;
            next.known=true;
            if(!layout.known||next.program_left!=layout.program_left||next.name_edge!=layout.name_edge||next.name_align!=layout.name_align
               ||next.clock_edge!=layout.clock_edge||next.clock_align!=layout.clock_align
               ||next.safe_preview!=layout.safe_preview||next.safe_program!=layout.safe_program
               ||next.safe_preview_aspect!=layout.safe_preview_aspect||next.safe_program_aspect!=layout.safe_program_aspect
               ||next.safe_preset!=layout.safe_preset||next.meters!=layout.meters) ++next.revision;
            else next.revision=layout.revision;
            layout=next;
        }
        std::snprintf(view.message.data(),view.message.size(),"KAVTOR CONNECTED");
    }
    void ingest() {
        for(;;) {
            const auto cut=incoming.find('\n');
            if(cut==std::string::npos) {
                if(incoming.size()>262144) incoming.clear();
                return;
            }
            auto line=incoming.substr(0,cut);
            incoming.erase(0,cut+1);
            if(!line.empty()&&line.back()=='\r') line.pop_back();
            if(line.empty()) continue;
            try { apply(Json::parse(line)); } catch(...) {}
        }
    }
    static const char* edge_name(uint8_t edge) { return edge? "bottom":"top"; }
    static const char* align_name(uint8_t align) { return align==0?"left":align==2?"right":"center"; }
    bool send_bank() {
        int bank; { std::lock_guard<std::mutex> lock(mutex); if(!bank_dirty||!view.connected) return true; bank=mv_bank; bank_dirty=false; }
        return send_line(Json{{"cmd","mv_bank"},{"bank",bank}});
    }
    bool send_layout() {
        OverlayLayout spec; bool dirty;
        { std::lock_guard<std::mutex> lock(mutex); dirty=layout_dirty; spec=layout; if(dirty) layout_dirty=false; }
        if(!dirty) return true;
        const Json line={{"cmd","layout"},{"programLeft",spec.program_left},{"nameEdge",edge_name(spec.name_edge)},
            {"nameAlign",align_name(spec.name_align)},{"clockEdge",edge_name(spec.clock_edge)},{"clockAlign",align_name(spec.clock_align)},
            {"safePreview",spec.safe_preview},{"safeProgram",spec.safe_program},{"meters",spec.meters},
            {"safePreviewAspect",safe_aspect_name(spec.safe_preview_aspect)},{"safeProgramAspect",safe_aspect_name(spec.safe_program_aspect)},
            {"safePreset",spec.safe_preset?"legacy":"ebu-r95"}};
        if(send_line(line)) return true;
        std::lock_guard<std::mutex> lock(mutex); layout_dirty=true; return false;
    }
    bool send_preview_soft() {
        uint32_t soft;
        { std::lock_guard<std::mutex> lock(mutex);
          if(!preview_soft_dirty) return true;
          if(!view.transition_preview) {preview_soft_dirty=false;preview_softness=101;return true;}
          soft=preview_softness;preview_soft_dirty=false;
        }
        const uint32_t amount=(soft*40u+50u)/100u;
        if(send_line(Json{{"cmd","wipe_edge"},{"mode",amount?"soft":"hard"},{"amount",amount}}))return true;
        std::lock_guard<std::mutex> lock(mutex);preview_soft_dirty=true;return false;
    }
    bool send_style() {
        if(!send_preview_soft())return false;
        Style copy; bool dirty,geometry,mosaic;
        { std::lock_guard<std::mutex> lock(mutex); dirty=style_dirty; copy=style;geometry=geometry_supported;mosaic=view.wipe_mosaic_supported; if(dirty) style_dirty=false; }
        if(!dirty) return true;
        Json line={{"cmd","wipe_style"},{"multi",copy.multi},{"border",copy.border},{"aspectW",copy.aw},{"aspectH",copy.ah},
            {"posX",copy.px},{"posY",copy.py},{"cursor",copy.cursor}};
        if(mosaic)line["tileSize"]=copy.tile_size;
        if(geometry){line["vertices"]=copy.vertices;line["rounding"]=copy.rounding;}
        if(copy.save) line["save"]=true;
        if(send_line(line)) return true;
        std::lock_guard<std::mutex> lock(mutex); style_dirty=true; return false;
    }
    bool send_mix_preparation(){
        bool dip,super;uint32_t rgb,a,b;
        {std::lock_guard<std::mutex> lock(mutex);dip=dip_dirty;super=super_dirty;rgb=prepared_dip_rgb;a=prepared_gain_a;b=prepared_gain_b;dip_dirty=super_dirty=false;}
        bool ok=true;if(dip){char colour[8];std::snprintf(colour,sizeof colour,"#%06X",rgb);ok=send_line(Json{{"cmd","dip_color"},{"color",colour}});}
        if(ok&&super)ok=send_line(Json{{"cmd","mix_params"},{"aGain",a},{"bGain",b}});
        if(!ok){std::lock_guard<std::mutex> lock(mutex);dip_dirty|=dip;super_dirty|=super;}return ok;
    }
    bool send_rate() {
        uint32_t frames; bool dirty;
        { std::lock_guard<std::mutex> lock(mutex); dirty=rate_dirty; frames=rate_frames; if(dirty) rate_dirty=false; }
        if(!dirty) return true;
        if(send_line(Json{{"cmd","rate"},{"frames",frames}})) return true;
        std::lock_guard<std::mutex> lock(mutex); rate_dirty=true; return false;
    }
    bool pump(unsigned gen) {
        pollfd wait{fd,POLLIN,0};
        const int polled=::poll(&wait,1,20);
        if(polled<0&&errno==EINTR) return !stop;
        if(polled<0||(wait.revents&(POLLERR|POLLHUP|POLLNVAL))) return false;
        if(polled>0&&(wait.revents&POLLIN)) {
            char data[2048];
            const auto n=::recv(fd,data,sizeof data,0);
            if(n<0&&errno==EINTR) return !stop;
            if(n<=0) return false;
            incoming.append(data,std::size_t(n));
            ingest();
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            if(Clock::now()-last_state>std::chrono::seconds(6)) return false;
            if(awaiting_take&&!take_seen&&Clock::now()-awaiting_since>std::chrono::milliseconds(800)) {
                awaiting_take=false; view.busy=false;
            }
        }
        Json next_batch=Json::array();
        {
            std::lock_guard<std::mutex> lock(mutex);
            if(next_waiting){
                if(view.me!=next_me)return false;
                if(view.next_known&&view.next_background==next_sent_background&&view.next_key==next_sent_keys){next_waiting=false;if(!next_pending)view.busy=false;}
                else if(Clock::now()-next_since>std::chrono::seconds(2))return false;
            }
            if(next_pending&&!next_waiting){
                if(view.me!=next_me||!view.next_known||view.transitioning)return false;
                // Arm new layers before disarming old ones so the server always has a layer.
                for(bool enable:{true,false}){
                    if(view.next_background!=next_background&&next_background==enable)next_batch.push_back(Json{{"cmd","next"},{"target","background"}});
                    for(unsigned i=0;i<4;i++)if(view.next_key[i]!=next_keys[i]&&next_keys[i]==enable)next_batch.push_back(Json{{"cmd","next"},{"target","key"},{"slot",i}});
                }
                next_pending=false;next_sent_background=next_background;next_sent_keys=next_keys;
                next_waiting=!next_batch.empty();next_since=Clock::now();if(!next_waiting)view.busy=false;
            }
        }
        for(const auto& line:next_batch)if(!send_line(line))return false;
        Json manual_update;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if(manual_dirty) { manual_update=manual_message; manual_dirty=false; }
        }
        if(!manual_update.is_null()) {
            if(!send_bank()||!send_style()||!send_mix_preparation()||!send_line(manual_update)) return false;
        }
        MixerAction action=MixerAction::none; unsigned index=0;
        TransitionType kind=TransitionType::mix; uint32_t code=0,softness=0; bool reverse=false,sony=false;
        std::string queued; bool occupy=false;
        { std::lock_guard<std::mutex> lock(mutex);
          if(gen!=generation) return false;
          action=command; index=source; command=MixerAction::none;
          kind=auto_type; code=auto_code; reverse=auto_reverse; softness=auto_softness;sony=!sony_codes.empty();
          queued=std::move(raw); raw.clear(); occupy=action==MixerAction::automatic||raw_take; raw_take=false;
          if(occupy) { awaiting_take=true; take_seen=false; awaiting_since=Clock::now(); view.busy=true; }
        }
        if(action==MixerAction::none&&queued.empty()) return send_bank()&&send_style()&&send_layout()&&send_rate()&&send_mix_preparation()&&!stop;
        Json message;
        if(action==MixerAction::preview) message={{"cmd","pvw"},{"source",index}};
        else if(action==MixerAction::program) message={{"cmd","pgm"},{"source",index}};
        else if(action==MixerAction::cut) message={{"cmd","cut"}};
        else if(action==MixerAction::automatic) {
            if(kind==TransitionType::mix) {
                static const char* modes[]={"mix","mix","vfade","fadecut","cutfade","dip","nam","supermix"};
                message={{"cmd","mix"},{"mode",modes[code]}};
            } else if(kind==TransitionType::dme) {
                static const char* directions[]={"left","right","top","bottom"};
                if(code>=1000)message={{"cmd","dme"},{"sony",code},{"reverse",reverse}};
                else if(code==0||code>=9)message={{"cmd","dme"},{"effect",dme_effect(code)},{"reverse",reverse}};
                else message={{"cmd","dme"},{"effect",code<=4?"push":"slide"},{"direction",directions[((code-1)%4)^(reverse?1u:0u)]}};
            }
            else if(kind==TransitionType::wipe) {
                message={{"cmd","wipe"},{sony?"sony":"smpte",code},{"dir",reverse?"rev":"fwd"}};
                if(!softness) message["edge"]="hard";
                else {
                    unsigned amount=(softness*40u+50u)/100u;
                    if(!amount) amount=1;
                    if(amount>40) amount=40;
                    message["edge"]="soft"; message["amount"]=amount;
                }
            } else if(kind==TransitionType::stinger) message={{"cmd","stinger"},{"slot",code},{"reverse",reverse}};
            else { std::lock_guard<std::mutex> lock(mutex); awaiting_take=false; view.busy=false; return !stop; }
        } else if(!queued.empty()) {
            try { message=Json::parse(queued); } catch(...) {
                std::lock_guard<std::mutex> lock(mutex); view.busy=false; return !stop;
            }
        } else { std::lock_guard<std::mutex> lock(mutex); view.busy=false; return !stop; }
        if(!send_bank()||!send_style()||!send_rate()||!send_mix_preparation()||!send_line(message)) return false;
        if(!occupy) { std::lock_guard<std::mutex> lock(mutex); if(!view.transitioning&&!awaiting_take&&!manual_active) view.busy=false; }
        return send_layout()&&send_rate()&&send_mix_preparation()&&!stop;
    }
    void run() {
        while(!stop) {
            Config current; unsigned gen; bool enabled;
            { std::lock_guard<std::mutex> lock(mutex); current=config; gen=generation; enabled=configured; }
            if(!enabled) { pause(40); continue; }
            if(current.backend!=Backend::kavtor) { status("ADAPTER NOT IMPLEMENTED"); close_fd(); pause(200); continue; }
            status("KAVTOR CONNECTING");
            fd=connect_timeout(current.active(),2000);
            if(fd<0) { status("KAVTOR CONNECTION FAILED"); pause(1000); continue; }
            incoming.clear(); last_state=Clock::now();
            { std::lock_guard<std::mutex> lock(mutex); bank_dirty=true; }
            const auto hello=Json{{"cmd","hello"},{"client","picohost"},{"version",1}};
            if(!send_line(hello)||!send_line(Json{{"cmd","catalog"}})) { close_fd(); status("KAVTOR DISCONNECTED"); pause(1000); continue; }
            auto heartbeat=Clock::now();
            bool session=true;
            while(session&&!stop) {
                { std::lock_guard<std::mutex> lock(mutex); if(gen!=generation) session=false; }
                if(!session) break;
                if(Clock::now()-heartbeat>=std::chrono::seconds(2)) {
                    if(!send_line(Json{{"cmd","state"}})) { session=false; break; }
                    heartbeat=Clock::now();
                }
                session=pump(gen);
            }
            close_fd();
            if(stop) break;
            bool unchanged=false;
            { std::lock_guard<std::mutex> lock(mutex); unchanged=gen==generation; }
            if(unchanged) status("KAVTOR DISCONNECTED");
            pause(1000);
        }
    }
};
KavtorAdapter::KavtorAdapter():impl(new Impl) {}
KavtorAdapter::~KavtorAdapter() { delete impl; }
void KavtorAdapter::deactivate() {
    Config disabled; disabled.backend=Backend::midi;
    configure(disabled); // Invalidates queued commands and makes the worker close its session.
}
void KavtorAdapter::configure(const Config& config) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->server_version.clear();impl->config=config; impl->configured=true; ++impl->generation; impl->command=MixerAction::none; impl->raw.clear(); impl->raw_take=false; impl->awaiting_take=impl->take_seen=false; impl->next_pending=impl->next_waiting=false;impl->view={}; impl->layout={}; impl->layout_dirty=impl->rate_dirty=impl->style_dirty=impl->dip_dirty=impl->super_dirty=false;impl->background_scope_pending.clear();impl->background_scopes=Json::object();
}
MixerState KavtorAdapter::state() {
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->view;
}
bool KavtorAdapter::overlay_layout(OverlayLayout& out) const {
    std::lock_guard<std::mutex> lock(impl->mutex);
    out=impl->layout; return impl->view.connected&&impl->layout.known;
}
bool KavtorAdapter::set_aux_source(unsigned role,unsigned source) {
    if(role<1||role>4) return false;
    {std::lock_guard<std::mutex> lock(impl->mutex);if(impl->view.aux_sources[role-1]<0)return false;}
    return enqueue(Json({{"cmd","aux"},{"role",role},{"source",source}}).dump());
}
bool KavtorAdapter::set_multiview_bank(bool upper) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(impl->mv_bank!=int(upper)) { impl->mv_bank=int(upper); impl->bank_dirty=true; }
    return impl->view.connected;
}
bool KavtorAdapter::set_overlay_layout(const OverlayLayout& layout) {
    try {
        std::lock_guard<std::mutex> lock(impl->mutex);
        if(!impl->view.connected) return false;
        OverlayLayout next=layout; next.known=true; next.revision=impl->layout.revision+1;
        impl->layout=next; impl->layout_dirty=true; return true;
    } catch(...) { return false; }
}
bool KavtorAdapter::set_rate(uint32_t frames) {
    try {
        std::lock_guard<std::mutex> lock(impl->mutex);
        if(!impl->view.connected||frames<1||frames>1000) return false;
        impl->rate_frames=frames; impl->rate_dirty=true;
        if(!impl->view.rate_known||impl->view.auto_frames!=frames) ++impl->view.rate_revision;
        impl->view.auto_frames=frames; impl->view.rate_known=true; return true;
    } catch(...) { return false; }
}
uint32_t KavtorAdapter::wipe_preset(unsigned index) const {
    std::lock_guard<std::mutex> lock(impl->mutex);
    return index<impl->presets.size()?impl->presets[index]:0;
}
bool KavtorAdapter::wipe_style(uint32_t multi,uint32_t border,uint32_t aspect_w,uint32_t aspect_h,uint32_t pos_x,uint32_t pos_y,bool cursor,bool persist) {
    if(multi!=1&&multi!=2&&multi!=4&&multi!=9&&multi!=16) return false;
    if(border>40||pos_x>1000||pos_y>1000) return false;
    const bool aspect=aspect_w>=1&&aspect_w<=1000&&aspect_h>=1&&aspect_h<=1000;
    if(!aspect) return false;
    std::lock_guard<std::mutex> lock(impl->mutex);
    const auto vertices=impl->style.vertices,rounding=impl->style.rounding,tile_size=impl->style.tile_size;
    impl->style={multi,border,aspect_w,aspect_h,pos_x,pos_y,cursor,persist,vertices,rounding,tile_size};
    impl->style_dirty=true;
    return true;
}
bool KavtorAdapter::prepare_wipe_modifier(unsigned id,uint32_t a,uint32_t) {
    if(id!=169||a>100)return false;
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(!impl->view.connected||!impl->view.transition_preview)return false;
    if(impl->preview_softness!=a){impl->preview_softness=a;impl->preview_soft_dirty=true;}
    return true;
}
bool KavtorAdapter::manual(uint16_t position,TransitionType type,uint32_t code,bool reverse,uint32_t softness) {
    try {
        if((type==TransitionType::mix&&(code>7||(code==5&&!impl->dip_supported)||(code>=6&&!impl->broadcast_mix_supported)))||position>4095||softness>100||(type!=TransitionType::mix&&type!=TransitionType::wipe&&type!=TransitionType::dme)) return false;
        std::lock_guard<std::mutex> lock(impl->mutex);
        if(!impl->view.connected) return false;
        if(impl->manual_active) {
            if(impl->manual_endpoint) return true;
            impl->manual_position=position;
            impl->manual_message["position"]=position;
            impl->manual_dirty=true;
            impl->manual_endpoint=position==0||position==4095;
            return true;
        }
        if(impl->view.busy||impl->view.transitioning||impl->command!=MixerAction::none||!impl->raw.empty())return false;
        if(position==0) return true;
        if(type==TransitionType::dme&&(!impl->dme_available(code)||(code>=1&&code<=8&&!impl->move_supported)||!impl->view.next_background||std::any_of(impl->view.next_key.begin(),impl->view.next_key.end(),[](bool on){return on;})))return false;
        if(type==TransitionType::wipe&&code>999)return false;
        if(type==TransitionType::wipe&&!impl->sony_codes.empty()&&std::none_of(impl->sony_codes.begin(),impl->sony_codes.end(),[code](const Json& value){return value.is_number_integer()&&value==code;}))return false;
        if(type==TransitionType::mix&&code>1&&(!impl->native_transitions_supported||code>7||!impl->view.next_background||std::any_of(impl->view.next_key.begin(),impl->view.next_key.end(),[](bool on){return on;})))return false;
        impl->manual_message={{"cmd","manual"},{"type",type==TransitionType::mix?"mix":type==TransitionType::dme?"dme":"wipe"},{"position",position}};
        if(type==TransitionType::mix) {
            static const char* modes[]={"mix","mix","vfade","fadecut","cutfade","dip","nam","supermix"};
            impl->manual_message["mode"]=modes[code];
        }
        if(type==TransitionType::dme){if(code>=1000)impl->manual_message["sony"]=code;else impl->manual_message["effect"]=Impl::dme_effect(code);impl->manual_message["reverse"]=reverse;if(code>=1&&code<=8){static const char* directions[]={"left","right","top","bottom"};impl->manual_message["direction"]=directions[((code-1)%4)^(reverse?1u:0u)];}}
        if(type==TransitionType::wipe) {
            impl->manual_message[impl->sony_codes.empty()?"smpte":"sony"]=code;
            impl->manual_message["dir"]=reverse?"rev":"fwd";
            impl->manual_message["amount"]=(softness*40u+50u)/100u;
        }
        impl->manual_position=position;
        impl->manual_active=impl->manual_dirty=true;
        impl->manual_endpoint=position==4095;
        impl->awaiting_take=true;impl->take_seen=false;impl->awaiting_since=Clock::now();
        impl->view.busy=impl->view.manual_transition=true;
        return true;
    } catch(...) {return false;}
}
void KavtorAdapter::cancel_manual() {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(impl->manual_active&&!impl->manual_endpoint) {
        impl->manual_position=0;impl->manual_message["position"]=0;
        impl->manual_dirty=impl->manual_endpoint=true;
    }
}
bool KavtorAdapter::fade_to_black(uint32_t frames) {
    try {
        std::lock_guard<std::mutex> lock(impl->mutex);
        if(!impl->view.connected||impl->view.busy||impl->view.transitioning||impl->command!=MixerAction::none||!impl->raw.empty()) return false;
        if(frames<1||frames>1000) return false;
        impl->raw=Json({{"cmd","ftb"},{"frames",frames}}).dump();
        impl->raw_take=true;
        impl->view.busy=true;
        return true;
    } catch(...) { return false; }
}
bool KavtorAdapter::automatic(TransitionType type,uint32_t duration,uint32_t code,bool reverse,uint32_t softness) {
    try {
        std::lock_guard<std::mutex> lock(impl->mutex);
        if(!impl->view.connected||impl->view.busy||impl->view.transitioning||impl->command!=MixerAction::none||!impl->raw.empty()) return false;
        if(type!=TransitionType::mix&&type!=TransitionType::wipe&&type!=TransitionType::stinger&&type!=TransitionType::dme) return false;
        if((type==TransitionType::mix&&code>1)||(type==TransitionType::dme)) {
            if(!impl->native_transitions_supported||!impl->view.next_background||std::any_of(impl->view.next_key.begin(),impl->view.next_key.end(),[](bool on){return on;}))return false;
            if(type==TransitionType::mix&&(code>7||(code==5&&!impl->dip_supported)||(code>=6&&!impl->broadcast_mix_supported)))return false;
            if(type==TransitionType::dme&&(!impl->dme_available(code)))return false;
        }
        if(type==TransitionType::wipe&&!impl->sony_codes.empty()&&std::none_of(impl->sony_codes.begin(),impl->sony_codes.end(),[code](const Json& value){return value.is_number_integer()&&value==code;}))return false;
        if(softness>100||(type==TransitionType::wipe&&code>999)||(type==TransitionType::stinger&&code>9)) return false;
        if(duration>=1&&duration<=1000&&impl->rate_frames!=duration) { impl->rate_frames=duration; impl->rate_dirty=true; }
        impl->auto_type=type; impl->auto_code=code; impl->auto_reverse=reverse; impl->auto_softness=softness;
        impl->command=MixerAction::automatic; impl->view.busy=true; return true;
    } catch(...) { return false; }
}
bool KavtorAdapter::submit(const std::string& body) {
    Json message;
    try { message=Json::parse(body); } catch(...) { return false; }
    if(!message.is_object()||!message.contains("cmd")||!message.at("cmd").is_string()) return false;
    const auto cmd=message.at("cmd").get<std::string>();
    if(cmd=="cut") return request(MixerAction::cut,0);
    if(cmd=="pvw"||cmd=="preview"||cmd=="pgm"||cmd=="program") {
        if(!message.contains("source")||!message.at("source").is_number_integer()) return false;
        const int source=message.at("source").get<int>();
        if(source<0||source>23) return false;
        return request(cmd=="pgm"||cmd=="program"?MixerAction::program:MixerAction::preview,unsigned(source));
    }
    if(cmd=="mix"||cmd=="auto") return automatic(TransitionType::mix,0,0,false,0);
    if(cmd=="stinger") {
        if(!message.contains("slot")||!message.at("slot").is_number_integer()) return false;
        const int slot=message.at("slot").get<int>();
        if(slot<0||slot>9) return false;
        const bool reverse=message.contains("reverse")&&message.at("reverse").is_boolean()&&message.at("reverse").get<bool>();
        return automatic(TransitionType::stinger,0,uint32_t(slot),reverse,0);
    }
    if(cmd=="wipe"&&message.contains("smpte")) {
        // Preserve an explicitly named namespace from web/API clients.
        if(message.contains("sony")||!message.at("smpte").is_number_integer())return false;
        const int code=message.at("smpte").get<int>();if(code<0||code>999)return false;
        std::lock_guard<std::mutex> lock(impl->mutex);
        if(!impl->view.connected||impl->view.busy||impl->view.transitioning||impl->command!=MixerAction::none||!impl->raw.empty())return false;
        impl->raw=message.dump();impl->raw_take=true;impl->view.busy=true;return true;
    }
    if(cmd=="wipe"&&message.contains("sony")) {
        {std::lock_guard<std::mutex> lock(impl->mutex);if(impl->sony_codes.empty())return false;}
        if(!message.at("sony").is_number_integer()) return false;
        const int code=message.at("sony").get<int>();
        if(code<0||code>999) return false;
        bool reverse=false;
        if(message.contains("dir")&&message.at("dir").is_string()) {
            const auto dir=message.at("dir").get<std::string>();
            if(dir=="rev"||dir=="reverse") reverse=true;
            else if(dir!="fwd"&&dir!="forward"&&!dir.empty()) return false;
        }
        return automatic(TransitionType::wipe,0,uint32_t(code),reverse,0);
    }
    const bool take=cmd=="wipe"||cmd=="ftb";
    if(!take&&cmd!="key_processing"&&cmd!="wipe_dir"&&cmd!="wipe_direction"&&cmd!="wipe_edge"&&cmd!="wipe_pattern"&&cmd!="wipe_style"&&cmd!="wipe_settings"&&cmd!="wipe_presets"&&cmd!="stingers"
       &&cmd!="dsk"&&cmd!="dsk_preview"&&cmd!="dskpvw"&&cmd!="dsk_source"&&cmd!="key_on"&&cmd!="key_source"&&cmd!="next"&&cmd!="me"&&cmd!="rate"&&cmd!="trans_preview") return false;
    try {
        std::lock_guard<std::mutex> lock(impl->mutex);
        const bool previewEdit=impl->view.transition_preview&&(cmd=="wipe_settings"||cmd=="wipe_style"||cmd=="wipe_edge"||cmd=="wipe_dir"||cmd=="wipe_direction");
        if(!impl->view.connected||((impl->view.busy||impl->view.transitioning)&&!previewEdit)||impl->command!=MixerAction::none||!impl->raw.empty()) return false;
        impl->raw=message.dump(); impl->raw_take=take;
        if(take) impl->view.busy=true;
        return true;
    } catch(...) { return false; }
}
std::string KavtorAdapter::web_status() const {
    std::lock_guard<std::mutex> lock(impl->mutex);
    Json assigned=Json::array();
    for(unsigned i=0;i<24;++i) assigned.push_back(bool(impl->view.available[i]));
    return Json({{"connected",impl->view.connected},{"busy",impl->view.busy},{"transitioning",impl->view.transitioning},
        {"bothSources",impl->view.both_sources},{"preview",impl->view.preview},{"program",impl->view.program},{"me",impl->view.me},{"autoFrames",impl->view.auto_frames},
        {"dskOn",impl->view.dsk_on},{"dskKnown",impl->view.dsk_known},{"dskPreview",impl->dsk_preview},{"ftb",impl->view.ftb},
        {"keys",Json::array({Json{{"source",impl->view.key_source[0]},{"on",impl->view.key_on[0]}},Json{{"source",impl->view.key_source[1]},{"on",impl->view.key_on[1]}},Json{{"source",impl->view.key_source[2]},{"on",impl->view.key_on[2]}},Json{{"source",impl->view.key_source[3]},{"on",impl->view.key_on[3]}}})},
        {"dsks",Json::array({Json{{"source",impl->view.dsk_source[0]},{"on",impl->view.dsk_on}},Json{{"source",impl->view.dsk_source[1]},{"on",impl->view.dsk2_on}}})},
        {"next",Json{{"background",impl->view.next_background},{"keys",Json::array({bool(impl->view.next_key[0]),bool(impl->view.next_key[1]),bool(impl->view.next_key[2]),bool(impl->view.next_key[3])})}}},
        {"transitionPreview",impl->view.transition_preview},{"transitionPreviewSupported",impl->view.transition_preview_known},{"nativeTransitionsSupported",impl->native_transitions_supported},{"keyProcessing",impl->key_processing},{"dskProcessing",impl->dsk_processing},{"keyProcessingSupported",impl->key_processing_supported},
        {"message",impl->view.message.data()},{"assigned",assigned},{"patterns",impl->catalog},{"sonyWipes",impl->sony_codes},{"stingers",impl->stingers},
        {"wipePattern",impl->wipe_pattern},{"wipeDir",impl->wipe_dir},{"wipeEdge",impl->wipe_edge},
        {"wipeEdgeAmount",impl->wipe_amount},{"wipeBorderColor",impl->wipe_color},
        {"wipePresets",impl->presets},{"wipeMulti",impl->wipe_multi},{"wipeBorder",impl->wipe_border},{"wipeShadow",impl->wipe_shadow},
        {"wipeAspectW",impl->wipe_aw},{"wipeAspectH",impl->wipe_ah},{"wipePosX",impl->wipe_px},{"wipePosY",impl->wipe_py},
        {"meters",impl->layout.meters}}).dump();
}
bool KavtorAdapter::request(MixerAction action,unsigned source) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(!impl->view.connected||!impl->view.studio||impl->view.busy||impl->view.transitioning||impl->command!=MixerAction::none||!impl->raw.empty()) return false;
    if(action==MixerAction::cut) { impl->command=action; impl->view.busy=true; return true; }
    if(source==23) source=11; // The cascade key is independent of SHIFT.
    const bool reentry=source==11&&impl->view.me<3&&(impl->view.sources<24||impl->view.available[11]);
    if((action==MixerAction::preview||action==MixerAction::program)&&(reentry||(source<24&&impl->view.available[source]))) {
        impl->command=action; impl->source=source; impl->view.busy=true; return true;
    }
    return false;
}
bool KavtorAdapter::enqueue(const std::string& body) {
    try {
        std::lock_guard<std::mutex> lock(impl->mutex);
        if(!impl->view.connected||impl->view.busy||impl->view.transitioning||impl->command!=MixerAction::none||!impl->raw.empty()) return false;
        impl->raw=body; impl->raw_take=false; return true;
    } catch(...) { return false; }
}
bool KavtorAdapter::dsk(bool mix,uint32_t duration,unsigned slot) {
    if(slot>1) return false;
    return enqueue(Json({{"cmd","dsk"},{"slot",int(slot)},{"mix",mix},{"frames",duration}}).dump());
}
bool KavtorAdapter::set_me(unsigned slot) {
    if(slot>3) return false;
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(!impl->view.connected||(!impl->manual_active&&(impl->view.busy||impl->view.transitioning))
       ||impl->command!=MixerAction::none||!impl->raw.empty())return false;
    impl->raw=Json({{"cmd","me"},{"slot",int(slot)}}).dump();impl->raw_take=false;
    return true;
}
bool KavtorAdapter::set_key_source(unsigned slot,int source) {
    if(slot>3||source<0||source>23||source==11||source==23) return false;
    return enqueue(Json({{"cmd","key_source"},{"slot",int(slot)},{"source",source}}).dump());
}
bool KavtorAdapter::toggle_key(unsigned slot) {
    if(slot>3) return false;
    return enqueue(Json({{"cmd","key_on"},{"slot",int(slot)}}).dump());
}
bool KavtorAdapter::set_dsk_source(unsigned slot,int source) {
    if(slot>1||source<0||source>23||source==11||source==23) return false;
    return enqueue(Json({{"cmd","dsk_source"},{"slot",int(slot)},{"source",source}}).dump());
}
bool KavtorAdapter::supports_transition_preview() const {
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->view.connected&&impl->view.transition_preview_known;
}
bool KavtorAdapter::set_transition_preview(bool on) {
    if(!supports_transition_preview()) return false;
    return submit(Json({{"cmd","trans_preview"},{"on",on}}).dump());
}
bool KavtorAdapter::set_next_transition(bool background,const std::array<bool,4>& keys) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(!impl->view.connected||!impl->view.next_known||impl->view.transitioning||impl->command!=MixerAction::none||!impl->raw.empty())return false;
    if(impl->view.busy&&!impl->next_pending&&!impl->next_waiting)return false;
    bool any=background;
    for(unsigned i=0;i<4;i++)if(keys[i]){if(!impl->view.key_available[i])return false;any=true;}
    if(!any)return false;
    impl->next_background=background;impl->next_keys=keys;impl->next_me=impl->view.me;impl->next_pending=true;impl->view.busy=true;return true;
}
bool KavtorAdapter::toggle_next_background() {
    auto live=state();bool background=!live.next_background;
    if(!background&&!std::any_of(live.next_key.begin(),live.next_key.end(),[](bool on){return on;}))background=true;
    return set_next_transition(background,live.next_key);
}
bool KavtorAdapter::toggle_next_key(unsigned slot) {
    if(slot>3)return false;
    auto live=state();auto keys=live.next_key;keys[slot]=!keys[slot];bool background=live.next_background;
    if(!background&&!std::any_of(keys.begin(),keys.end(),[](bool on){return on;}))background=true;
    return set_next_transition(background,keys);
}
}

namespace bkds::link {
bool KavtorAdapter::server_info(ServerInfo& out)const{std::lock_guard<std::mutex> lock(impl->mutex);out.fields={{"SERVER","kavtor"},{"VERSION",impl->server_version.empty()?"NOT REPORTED":impl->server_version},{"PROTOCOL","JSON panel v1"},{"LINK",impl->view.connected?"CONNECTED":"DISCONNECTED"}};return true;}
}

namespace bkds::link {
bool KavtorAdapter::native_key_controls() const {
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->view.connected&&impl->key_processing_supported;
}
bool KavtorAdapter::key_setting(unsigned delegation,unsigned dsk_slot,unsigned button,int source) {
    if(delegation>4||dsk_slot>1)return false;
    const bool dsk=delegation==4;
    const unsigned slot=dsk?dsk_slot:delegation;
    if(button==67) {
        if(source<0)return true; // Arm the existing KEY BUS source selector.
        return dsk?set_dsk_source(slot,source):set_key_source(slot,source);
    }
    Json settings;
    if(button==88){if(!state().key_luma_supported)return false;settings={{"mode","luma"}};}
    else if(button==89)settings={{"mode","linear"}};
    else if(button==91)settings={{"mode","chroma"}};
    else if(button==100) {
        std::lock_guard<std::mutex> lock(impl->mutex);
        const auto& params=dsk?impl->dsk_processing[slot]:impl->key_processing[slot];
        settings={{"mask",!params.value("mask",false)}};
    } else if(button==102){
        std::lock_guard<std::mutex> lock(impl->mutex);if(!impl->view.key_invert_supported)return false;
        const auto& params=dsk?impl->dsk_processing[slot]:impl->key_processing[slot];settings={{"invert",!params.value("invert",false)}};
    } else return false; // Pattern and separate fill/key are not advertised.
    return submit(Json({{"cmd","key_processing"},{"target",dsk?"dsk":"key"},{"slot",slot},{"settings",settings}}).dump());
}
}

namespace bkds::link {
bool KavtorAdapter::supports_dme() const {
    std::lock_guard<std::mutex> lock(impl->mutex);return impl->view.connected&&impl->native_transitions_supported;
}
unsigned KavtorAdapter::keypad_transition_slots(TransitionType type) const {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(!impl->view.connected||!impl->native_transitions_supported)return 0;
    return type==TransitionType::mix?(impl->broadcast_mix_supported?7:impl->dip_supported?5:4):type==TransitionType::dme?(impl->cube_supported?9:8):0;
}
std::string KavtorAdapter::keypad_transition_label(TransitionType type,unsigned code) const {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(type==TransitionType::mix&&code==5&&impl->dip_supported)return "DIP";
    if(type==TransitionType::mix&&code==6&&impl->broadcast_mix_supported)return "NAM";
    if(type==TransitionType::mix&&code==7&&impl->broadcast_mix_supported)return "SUPER MIX";
    static const char* mix[]={"","MIX","VFADE","FADECUT","CUTFADE"};
    static const char* dme[]={"","PUSH LEFT","PUSH RIGHT","PUSH TOP","PUSH BOTTOM","SLIDE LEFT","SLIDE RIGHT","SLIDE TOP","SLIDE BOTTOM"};
    if(type==TransitionType::mix&&code>=1&&code<=4)return mix[code];
    if(type==TransitionType::dme&&code==0&&impl->move_supported)return "MOVE";
    if(type==TransitionType::dme&&code==12&&impl->zoom_supported)return "ZOOM";
    if(type==TransitionType::dme&&code==13&&impl->page_curl_supported)return "PAGE TURN";
    if(type==TransitionType::dme&&code==14&&impl->page_roll_supported)return "PAGE ROLL";
    if(type==TransitionType::dme&&code==9&&impl->cube_supported)return "CUBE";
    if(type==TransitionType::dme&&code>=1001&&code<=1004){static const char* names[]={"SLIDE RIGHT","SLIDE LEFT","SLIDE DOWN","SLIDE UP"};return names[code-1001];}
    if(type==TransitionType::dme&&code>=2601&&code<=2604){static const char* names[]={"PUSH RIGHT","PUSH LEFT","PUSH DOWN","PUSH UP"};return names[code-2601];}
    if(type==TransitionType::dme&&code>=1000)return "SONY "+std::to_string(code);
    if(type==TransitionType::dme&&code>=1&&code<=8)return dme[code];
    return "";
}
}

namespace bkds::link {
bool KavtorAdapter::valid_wipe_code(uint32_t code) const {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(!impl->sony_codes.empty())return std::any_of(impl->sony_codes.begin(),impl->sony_codes.end(),[code](const Json& value){return value.is_number_integer()&&value==code;});
    if(impl->catalog.empty())return code<=999;
    for(const auto& row:impl->catalog)for(const char* name:{"smpte","smpteReverse"})
        if(row.contains(name)&&row.at(name).is_number_integer()&&row.at(name)==code)return true;
    return false;
}
}

namespace bkds::link {
bool KavtorAdapter::set_supersource_input(unsigned side,unsigned window,unsigned input) {
    auto live=state();if(side>=2||window>=24||(input>=24&&(input<1000||input>1003))||input==11||input==23||!live.connected||!live.supersources_supported)return false;
    const auto& source=live.supersources[side];const auto& box=source.windows[window];if(source.source<0||!box.mapped||int(input)==source.source)return false;
    const auto message=Json{{"cmd","supersource_input"},{"source",source.source},{"box",box.id.data()},{"input",input},{"bus",side?"program":"preview"},{"me",live.me},{"button",window}}.dump();
    std::lock_guard<std::mutex> lock(impl->mutex);
    // Window punches are independent of a running background take.
    if(!impl->view.connected||!impl->raw.empty())return false;
    impl->raw=message;impl->raw_take=false;return true;
}
}

namespace bkds::link {
unsigned KavtorAdapter::first_keypad_transition_slot(TransitionType t) const {
    std::lock_guard<std::mutex> lock(impl->mutex);return t==TransitionType::dme&&impl->move_supported?0:1;
}
bool KavtorAdapter::keypad_transition_available(TransitionType t,unsigned slot) const {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(!impl->view.connected)return false;
    if(t==TransitionType::stinger)return slot<impl->stingers.size()&&impl->stingers[slot].is_object()&&impl->stingers[slot].contains("media")&&impl->stingers[slot]["media"].is_string()&&!impl->stingers[slot]["media"].get<std::string>().empty();
    if(!impl->native_transitions_supported)return false;
    if(t==TransitionType::dme)return impl->dme_available(slot);
    return t==TransitionType::mix&&slot>=1&&(slot<=4||(slot==5&&impl->dip_supported)||(slot<=7&&impl->broadcast_mix_supported));
}
}


namespace bkds::link {
bool KavtorAdapter::supports_dme_background(unsigned code) const {std::lock_guard<std::mutex> lock(impl->mutex);return impl->view.connected&&impl->view.dme_background_supported&&(dme_background_slot(code)>=0||(code>=1000&&impl->view.dme_preset_background_supported))&&impl->dme_available(code);}
bool KavtorAdapter::set_dme_background(unsigned code,int input) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(!impl->view.connected||!impl->view.dme_background_supported||(dme_background_slot(code)<0&&!(code>=1000&&impl->view.dme_preset_background_supported))||!impl->dme_available(code)||!impl->raw.empty()||!impl->background_scope_pending.empty()||(input!=-1&&(input<0||(input>=24&&(input<1000||input>1003))))||input==11||input==23)return false;
    impl->raw=Json{{"cmd","dme_background"},{"effect",impl->background_custom(code)?Impl::dme_effect(code):"global"},{"source",input},{"me",impl->view.me}}.dump();impl->raw_take=false;return true;
}
}

namespace bkds::link {
bool KavtorAdapter::valid_dme_code(uint32_t code) const {
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->dme_available(code);
}
}

namespace bkds::link {
bool KavtorAdapter::wipe_geometry_supported(uint32_t code) const {std::lock_guard<std::mutex> lock(impl->mutex);return impl->view.connected&&impl->geometry_supported&&(code==49||(code>=300&&code<=304));}
bool KavtorAdapter::wipe_geometry(uint32_t code,uint32_t vertices,uint32_t rounding,bool save){if(vertices<3||vertices>64||rounding>50)return false;std::lock_guard<std::mutex> lock(impl->mutex);if(!impl->geometry_supported)return false;impl->style.vertices=vertices;impl->style.rounding=rounding;impl->style.save=save;impl->style_dirty=true;(void)code;return true;}
}

namespace bkds::link {
bool KavtorAdapter::wipe_tiles_supported(uint32_t code) const {std::lock_guard<std::mutex> lock(impl->mutex);return impl->view.connected&&impl->view.wipe_mosaic_supported&&((code>=200&&code<=203)||(code>=206&&code<=213)||(code>=250&&code<=257)||(code>=260&&code<=269));}
bool KavtorAdapter::wipe_tiles(uint32_t code,uint32_t size,bool save){if(size<2||size>50)return false;std::lock_guard<std::mutex> lock(impl->mutex);if(!impl->view.wipe_mosaic_supported)return false;impl->style.tile_size=size;impl->style.save=save;impl->style_dirty=true;(void)code;return true;}
}

namespace bkds::link {
unsigned KavtorAdapter::stinger_slots() const {std::lock_guard<std::mutex> lock(impl->mutex);return unsigned(impl->stingers.size());}
}

namespace bkds::link {
bool KavtorAdapter::supports_mix_preparation(bool super)const {std::lock_guard<std::mutex> lock(impl->mutex);return impl->view.connected&&impl->view.mix_preparation_known&&(super?impl->broadcast_mix_supported:impl->dip_supported);}
bool KavtorAdapter::prepare_mix(bool super,uint32_t a,uint32_t b){std::lock_guard<std::mutex> lock(impl->mutex);if(!impl->view.connected||!impl->view.mix_preparation_known||(super?!impl->broadcast_mix_supported:!impl->dip_supported)||a>(super?100u:0xffffffu)||(super&&b>100))return false;if(super){impl->prepared_gain_a=a;impl->prepared_gain_b=b;impl->super_dirty=true;}else{impl->prepared_dip_rgb=a;impl->dip_dirty=true;}return true;}
}

namespace bkds::link {
bool KavtorAdapter::set_dme_background_scope(unsigned code,bool custom,bool copy){
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(!impl->view.connected||!impl->view.dme_background_scopes_supported||!impl->dme_available(code)||!impl->raw.empty()||!impl->background_scope_pending.empty())return false;
    const auto effect=Impl::dme_effect(code);impl->raw=Json{{"cmd","dme_background"},{"effect",effect},{"custom",custom},{"copyGlobal",copy},{"me",impl->view.me}}.dump();impl->raw_take=false;impl->background_scope_pending=effect;impl->background_scope_expected=custom;return true;
}
}
