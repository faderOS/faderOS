#include "obs_posix.hpp"
#include "luma.hpp"
#include <exception>
#include <unistd.h>
#include <curl/curl.h>
#include <curl/websockets.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
using Json=nlohmann::json;
namespace bkds::link {
using Clock=std::chrono::steady_clock;
struct ObsError:std::runtime_error { using std::runtime_error::runtime_error; };
struct ObsRejected:ObsError { using ObsError::ObsError; };
static std::string digest(const std::string& text) {
    unsigned char hash[SHA256_DIGEST_LENGTH]; SHA256(reinterpret_cast<const unsigned char*>(text.data()),text.size(),hash);
    unsigned char out[45]{}; EVP_EncodeBlock(out,hash,sizeof hash); return reinterpret_cast<char*>(out);
}
struct ObsAdapter::Impl {
    std::mutex mutex;
    std::atomic<bool> stop{false};
    Config config;std::string software_version,websocket_version;
    bool configured=false;
    unsigned generation=0;
    MixerState view;
    Json scene_names=Json::array();
    MixerAction command=MixerAction::none;
    unsigned source=0;
    TransitionSettings transition_settings;
    TransitionType auto_type=TransitionType::mix;
    uint32_t auto_duration=300,auto_code=0,auto_softness=3;
    bool auto_reverse=false;
    DmeParameters requested_dme{},auto_dme{};
    bool manual_active=false,manual_abort=false,manual_terminal=false;
    uint16_t manual_position=0;
    Clock::time_point manual_seen{};
    Json transition_list=Json::array();
    std::array<unsigned,3> output_revisions{};
    unsigned video_ends=0,stinger_end_base=0,stinger_video_base=0;
    bool waiting_stinger=false;
    std::string password,credentials_path;
    std::array<std::string,24> assigned,names;
    std::thread worker;
    CURL* socket=nullptr;
    std::string incoming;
    unsigned request_id=0;
    bool dirty=true;
    unsigned preview_revision=0,program_revision=0,studio_revision=0,transition_ends=0;
    Impl(std::string p,std::array<std::string,24> s):password(std::move(p)),assigned(std::move(s)) {
        worker=std::thread([this]{ run(); });
    }
    ~Impl() {
        {std::lock_guard<std::mutex> lock(mutex);manual_abort=true;}
        // Let the worker release a held T-bar before stopping its RPC loop.
        auto deadline=Clock::now()+std::chrono::seconds(4);
        while(Clock::now()<deadline) {
            {std::lock_guard<std::mutex> lock(mutex);if(!manual_active) break;}
            pause(5);
        }
        stop=true;worker.join();
    }
    void status(const char* text,bool connected=false) {
        std::lock_guard<std::mutex> lock(mutex);
        view.connected=connected; view.busy=false;
        if(connected) view.auth_required=false;
        else if(std::string(text).find("AUTH")!=std::string::npos)view.auth_required=true;
        if(!connected) { view.output_known.fill(false);view.output_active.fill(false); }
        if(!connected) { view.preview=view.program=-1; view.transitioning=false; view.dsk_known=view.dsk_on=view.dsk_mixing=view.dsk2_known=view.dsk2_on=view.dsk2_mixing=false; command=MixerAction::none;manual_active=false;manual_abort=true; }
        std::snprintf(view.message.data(),view.message.size(),"%s",text);
    }
    void pause(unsigned ms) { for(unsigned i=0;i<ms&&!stop;i+=5) std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    void send(const Json& j) {
        const auto text=j.dump(); auto end=Clock::now()+std::chrono::seconds(2);
        while(!stop&&Clock::now()<end) {
            size_t sent=0; auto rc=curl_ws_send(socket,text.data(),text.size(),&sent,0,CURLWS_TEXT);
            if(rc==CURLE_OK&&sent==text.size()) return;
            // Never replay an ambiguously partial command (especially CUT).
            if(sent||rc!=CURLE_AGAIN) throw ObsError("OBS SEND FAILED");
            pause(5);
        }
        throw ObsError("OBS SEND TIMEOUT");
    }
    bool receive(Json& j) {
        char data[4096]; size_t n=0; const curl_ws_frame* meta=nullptr;
        auto rc=curl_ws_recv(socket,data,sizeof data,&n,&meta);
        if(rc==CURLE_AGAIN) return false;
        if(rc!=CURLE_OK||!meta) throw ObsError("OBS DISCONNECTED");
        if(meta->flags&CURLWS_CLOSE) {
            size_t sent=0; curl_ws_send(socket,data,n,&sent,0,CURLWS_CLOSE);
            if(n>=2&&static_cast<unsigned char>(data[0])==15&&static_cast<unsigned char>(data[1])==169) throw ObsError("OBS AUTH FAILED: CHECK PASSWORD");
            throw ObsError("OBS DISCONNECTED");
        }
        if(meta->flags&(CURLWS_PING|CURLWS_PONG)) return false;
        if(meta->flags&CURLWS_BINARY) throw ObsError("OBS EXPECTED JSON TEXT");
        incoming.append(data,n);
        if(incoming.size()>65536) throw ObsError("OBS MESSAGE TOO LARGE");
        if(meta->bytesleft||(meta->flags&CURLWS_CONT)) return false;
        j=Json::parse(incoming); incoming.clear(); return true;
    }
    void event(const Json& j) {
        if(j.value("op",0)!=5) return;
        const auto& d=j.at("d");
        const auto type=d.value("eventType",std::string());
        const auto data=d.value("eventData",Json::object());
        std::lock_guard<std::mutex> lock(mutex);
        if(type=="StreamStateChanged"||type=="RecordStateChanged"||type=="VirtualcamStateChanged") {
            const unsigned i=type=="StreamStateChanged"?0:type=="RecordStateChanged"?1:2;
            view.output_known[i]=true;view.output_active[i]=data.at("outputActive").get<bool>();++output_revisions[i];
        } else if(type=="CurrentPreviewSceneChanged") {
            view.preview=slot(data.at("sceneName")); ++preview_revision;
        } else if(type=="CurrentProgramSceneChanged") {
            view.program=slot(data.at("sceneName")); ++program_revision;
        } else if(type=="SceneTransitionStarted") view.transitioning=true;
        else if(type=="SceneTransitionEnded") {
            ++transition_ends;
            if(!waiting_stinger||video_ends!=stinger_video_base) view.transitioning=false;
        } else if(type=="SceneTransitionVideoEnded") {
            ++video_ends;
            if(waiting_stinger&&transition_ends!=stinger_end_base) view.transitioning=false;
        } else if(type=="StudioModeStateChanged") {
            view.studio=data.at("studioModeEnabled").get<bool>();
            if(!view.studio) { view.preview=-1; view.transitioning=false; }
            ++studio_revision; ++preview_revision; dirty=true;
        } else if(type=="SceneListChanged"||type=="SceneNameChanged"||
                  type=="SceneCreated"||type=="SceneRemoved") dirty=true;
    }
    void wait_transition(unsigned before,unsigned timeout_ms) {
        auto end=Clock::now()+std::chrono::milliseconds(timeout_ms);
        while(!stop&&(transition_ends==before||(waiting_stinger&&video_ends==stinger_video_base))&&Clock::now()<end) {
            Json j; if(receive(j)) event(j); else pause(2);
        }
        if(transition_ends==before||(waiting_stinger&&video_ends==stinger_video_base)) throw ObsError("OBS TRANSITION COMPLETION TIMEOUT");
    }
    Json wait_op(int op,const std::string& id="") {
        auto end=Clock::now()+std::chrono::seconds(3);
        while(!stop&&Clock::now()<end) {
            Json j; if(!receive(j)) { pause(5); continue; }
            if(j.at("op")==5) { event(j); continue; }
            if(j.at("op")==op&&(id.empty()||j.at("d").value("requestId",std::string())==id)) return j.at("d");
        }
        throw ObsError("OBS RESPONSE TIMEOUT");
    }
    Json rpc(const char* type,Json data=Json::object()) {
        const auto id=std::to_string(++request_id);
        send({{"op",6},{"d",{{"requestType",type},{"requestId",id},{"requestData",data}}}});
        auto reply=wait_op(7,id);
        if(!reply.at("requestStatus").at("result").get<bool>()) throw ObsRejected("OBS REQUEST REJECTED");
        return reply.value("responseData",Json::object());
    }
    int slot(const Json& name) {
        if(!name.is_string()) return -1;
        for(unsigned i=0;i<24;i++) if(!names[i].empty()&&names[i]==name.get<std::string>()) return int(i);
        return -1;
    }
    void refresh(unsigned gen) {
        dirty=false;
        const auto pr=preview_revision,pg=program_revision,st=studio_revision;
        const bool studio=rpc("GetStudioModeEnabled").at("studioModeEnabled").get<bool>();
        const auto list=rpc("GetSceneList");
        const auto transitions=rpc("GetSceneTransitionList");
        auto scenes=list.at("scenes").get<std::vector<Json>>();
        std::sort(scenes.begin(),scenes.end(),[](const Json& a,const Json& b){return a.at("sceneIndex").get<int>()<b.at("sceneIndex").get<int>();});
        MixerState next; next.connected=true; next.studio=studio;
        for(unsigned i=0;i<24;i++) if(!names[i].empty()) {
            for(const auto& scene:scenes) if(scene.at("sceneName")==names[i]) { next.available.set(i); next.sources++; break; }
        }
        next.program=slot(list.at("currentProgramSceneName"));
        next.preview=studio?slot(list.value("currentPreviewSceneName",Json())):-1;
        std::snprintf(next.message.data(),next.message.size(),"%s",studio?"OBS CONNECTED":"OBS: ENABLE STUDIO MODE");
        std::lock_guard<std::mutex> lock(mutex);
        if(generation==gen) {
            next.output_known=view.output_known;next.output_active=view.output_active;
            next.dsk_known=view.dsk_known; next.dsk_on=view.dsk_on; next.dsk_mixing=view.dsk_mixing;
            next.dsk2_known=view.dsk2_known;next.dsk2_on=view.dsk2_on;next.dsk2_mixing=view.dsk2_mixing;
            next.completed_auto=view.completed_auto; next.busy=view.busy; next.transitioning=view.transitioning;
            if(pr!=preview_revision) next.preview=view.preview;
            if(pg!=program_revision) next.program=view.program;
            if(st!=studio_revision) next.studio=view.studio;
            view=next;transition_list=transitions.at("transitions");scene_names=Json::array();
            for(const auto& scene:scenes) scene_names.push_back(scene.at("sceneName"));
        }
    }
    void refresh_output(unsigned i) {
        static const char* status[]={"GetStreamStatus","GetRecordStatus","GetVirtualCamStatus"};
        unsigned revision;
        { std::lock_guard<std::mutex> lock(mutex);revision=output_revisions[i]; }
        bool known=false,active=false;
        try { active=rpc(status[i]).at("outputActive").get<bool>();known=true; }
        catch(const ObsRejected&) { }
        std::lock_guard<std::mutex> lock(mutex);
        if(revision==output_revisions[i]) {view.output_known[i]=known;view.output_active[i]=active;}
    }
    void refresh_outputs() { for(unsigned i=0;i<3;i++) refresh_output(i); }
    void toggle_output(unsigned i) {
        refresh_output(i);
        bool active;
        { std::lock_guard<std::mutex> lock(mutex);
          if(!view.output_known[i]) throw ObsRejected("OBS OUTPUT STATUS UNAVAILABLE");
          active=view.output_active[i]; }
        static const char* start[]={"StartStream","StartRecord","StartVirtualCam"};
        static const char* stop_output[]={"StopStream","StopRecord","StopVirtualCam"};
        rpc(active?stop_output[i]:start[i]);
        refresh_output(i); // Never claim an active output from a command acknowledgement alone.
    }
    Json dsk_rpc(const char* kind,Json data) {
        auto response=rpc("CallVendorRequest",{{"vendorName","downstream-keyer"},{"requestType",kind},{"requestData",data}}).at("responseData");
        if(!response.value("success",false)) throw ObsRejected("OBS DSK REQUEST REJECTED");
        return response;
    }
    TransitionSettings settings_copy() {
        std::lock_guard<std::mutex> lock(mutex); return transition_settings;
    }
    Json dsk_address(unsigned slot) { const auto s=settings_copy();return {{"dsk_name",slot?s.dsk2_name.data():s.dsk_name.data()}}; }
    void refresh_dsk(unsigned slot) {
        bool known=false,on=false;
        try {
            const auto info=dsk_rpc("get_downstream_keyer",dsk_address(slot));
            known=!info.value("tie",false);
            if(!info.value("exclude_scenes",Json::array()).empty()) {
                const auto pgm=rpc("GetCurrentProgramScene").at("sceneName");
                for(const auto& row:info.at("exclude_scenes")) if(row.value("name",std::string())==pgm) known=false;
            }
            on=known&&!dsk_rpc("dsk_get_scene",dsk_address(slot)).at("scene").get<std::string>().empty();
        } catch(const ObsRejected&) { }
        std::lock_guard<std::mutex> lock(mutex);view.dsk_known_at(slot)=known;view.dsk_on_at(slot)=on;
    }
    void refresh_dsk() {refresh_dsk(0);if(settings_copy().dsk2_name[0])refresh_dsk(1);}
    void take_dsk(bool mix,uint32_t duration,unsigned slot) {
        const auto address=dsk_address(slot);
        const auto old=dsk_rpc("get_downstream_keyer",address);
        if(old.value("tie",false)) throw ObsRejected("OBS DSK: DISABLE TIE");
        const auto settings=settings_copy();
        const bool off=!dsk_rpc("dsk_get_scene",address).at("scene").get<std::string>().empty();
        const std::string target=off?"":(slot?settings.dsk2_scene:settings.dsk_scene).data();
        if(!off) {
            bool found=false;
            for(const auto& scene:old.at("scenes")) if(scene.at("name")==target) found=true;
            if(!found||target.empty()) throw ObsRejected("OBS DSK: SCENE NOT ASSIGNED IN KEYER");
        }
        // Exclusions suppress the output even though the plugin reports a selected scene.
        if(!old.value("exclude_scenes",Json::array()).empty()) {
            const auto pgm=rpc("GetCurrentProgramScene").at("sceneName");
            for(const auto& row:old.at("exclude_scenes")) if(row.value("name",std::string())==pgm)
                throw ObsRejected("OBS DSK: PROGRAM IS EXCLUDED");
        }
        std::string effect;
        const auto transitions=rpc("GetSceneTransitionList").at("transitions");
        for(const auto& t:transitions) if(t.at("transitionKind")== (mix?"fade_transition":"cut_transition")) {
            if(!effect.empty()) throw ObsRejected("OBS DSK: AMBIGUOUS TRANSITION");
            effect=t.at("transitionName").get<std::string>();
        }
        if(effect.empty()) throw ObsRejected("OBS DSK: TRANSITION MISSING");
        const char* mode=off?"hide":"show";
        const std::string prefix=off?"hide_":"show_";
        auto set=[&](const std::string& name,uint32_t ms) {
            auto data=address;data["transition_type"]=mode;data["transition"]=name;data["transition_duration"]=ms;
            dsk_rpc("dsk_set_transition",data);
        };
        try {
            // Keep this effect installed: restoring None can remove the live output
            // because the plugin uses currentItem(), while its API changes selection only.
            if(old.value(prefix+"transition",std::string())!=effect||old.value(prefix+"transition_duration",0u)!=duration)
                set(effect,duration);
            auto data=address;data["scene"]=target;dsk_rpc("dsk_select_scene",data);
            const auto selected=dsk_rpc("dsk_get_scene",address).at("scene");
            if(selected!=target) throw ObsRejected("OBS DSK: SELECTION NOT CONFIRMED");
            { std::lock_guard<std::mutex> lock(mutex);view.dsk_known_at(slot)=true;view.dsk_on_at(slot)=mix||!off;view.dsk_mixing_at(slot)=mix; }
            // The vendor API has no transition-end event. Keep MIX lit for the imposed duration.
            const auto until=Clock::now()+std::chrono::milliseconds(mix?duration:0);
            while(!stop&&Clock::now()<until) { Json j;if(receive(j)) event(j);else pause(2); }
            refresh_dsk();
            { std::lock_guard<std::mutex> lock(mutex);view.dsk_mixing_at(slot)=false; }
        } catch(...) {
            { std::lock_guard<std::mutex> lock(mutex);view.dsk_known_at(slot)=view.dsk_mixing_at(slot)=false; }
            throw;
        }
    }
    bool drive_tbar(bool& release_attempted) {
        unsigned sent=4096;
        auto next=Clock::now();
        for(;;) {
            unsigned value;bool abort;
            { std::lock_guard<std::mutex> lock(mutex);
              value=manual_position;abort=manual_abort||!view.studio||stop||(!manual_terminal&&Clock::now()-manual_seen>std::chrono::seconds(2)); }
            if(abort) {release_attempted=true;rpc("SetTBarPosition",{{"position",0.0},{"release",true}});return false;}
            if(value!=sent && (Clock::now()>=next||value==0||value==4095)) {
                if(value==0||value==4095) release_attempted=true;
                rpc("SetTBarPosition",{{"position",value/4095.0},{"release",value==0||value==4095}});
                sent=value;next=Clock::now()+std::chrono::milliseconds(16);
                if(value==0||value==4095) return value==4095;
            }
            Json j;if(receive(j))event(j);else pause(2);
        }
    }
    void take(TransitionType type,uint32_t duration,uint32_t code,bool reverse,uint32_t softness,bool cut,bool manual=false) {
        const auto current=rpc("GetCurrentPreviewScene").at("sceneName");
        const auto previous=rpc("GetCurrentProgramScene").at("sceneName");
        if(current==previous) return;
        const auto list=rpc("GetSceneTransitionList");
        TransitionSettings settings;DmeParameters dme;
        { std::lock_guard<std::mutex> lock(mutex); dme=auto_dme;settings=transition_settings; transition_list=list.at("transitions"); }
        static const char* kinds[]={"fade_transition","wipe_transition","move_transition","obs_stinger_transition","slide_transition","swipe_transition"};
        const bool luma=!cut&&type==TransitionType::wipe;
        const bool modify=!cut&&(type==TransitionType::slide||type==TransitionType::swipe);
        std::string pattern=luma?(code?default_luma_pattern(code):settings.wipe_pattern.data()):"";
        std::string configured=cut?"":(reverse&&!luma?settings.reverse_names:settings.names)[unsigned(type)].data();
        if(!cut&&type==TransitionType::stinger) {
            if(code>9) {status("OBS: INVALID STINGER SLOT",true);return;}
            if(code!=1) {
                configured=(reverse?settings.stinger_reverse:settings.stingers)[code].data();
                if(configured.empty()) {status("OBS: STINGER SLOT NOT MAPPED",true);return;}
            }
        }
        if(luma&&code) {
            for(const auto& wipe:settings.wipes) if(wipe.code==code) {
                if(wipe.has_pattern) pattern=wipe.pattern.data();
                else if(wipe.name[0]) configured=wipe.name.data(); // read legacy per-effect maps
            }
            if(pattern.empty()) { status("OBS: LUMA PATTERN NOT MAPPED",true); return; }
        }
        if(reverse&&!luma&&configured.empty()) { status("OBS: REVERSE WIPE NOT MAPPED",true); return; }
        std::string name,kind;
        unsigned matches=0;
        for(const auto& item:list.at("transitions")) {
            const auto n=item.at("transitionName").get<std::string>();
            const auto k=item.at("transitionKind").get<std::string>();
            if((!configured.empty()&&n==configured)||(configured.empty()&&k==(cut?"cut_transition":kinds[unsigned(type)]))) {
                name=n; kind=k; ++matches;
            }
        }
        if(matches!=1) {
            status(matches?"OBS: MAP TRANSITION IN WEB":"OBS: TRANSITION NOT FOUND",true); return;
        }
        if(!cut&&((luma&&kind!="wipe_transition")||
                  (type==TransitionType::mix&&kind!="fade_transition")||
                  (is_dme(type)&&kind!=kinds[unsigned(type)])||
                  (type==TransitionType::stinger&&kind!="obs_stinger_transition"))) {
            status("OBS: WRONG TRANSITION KIND",true); return;
        }
        const bool fixed=kind=="obs_stinger_transition";
        const auto old=rpc("GetSceneSceneTransitionOverride",{{"sceneName",current}});
        Json restore={{"sceneName",current},{"transitionName",old.at("transitionName").is_null()?Json(""):old.at("transitionName")}};
        if(old.contains("transitionDuration")&&!old.at("transitionDuration").is_null()) restore["transitionDuration"]=old.at("transitionDuration");
        Json use={{"sceneName",current},{"transitionName",name}};
        if(!fixed&&!cut) use["transitionDuration"]=duration;
        // The standard API edits only the selected transition. Select the effect while
        // preparing/running the take, then restore its full settings and selection.
        Json old_settings;
        std::string old_selected;
        bool have_selection=false,have_settings=false;
        auto restore_transition=[&] {
            std::exception_ptr failure;
            try {
                if(have_settings) {
                    rpc("SetCurrentSceneTransition",{{"transitionName",name}});
                    rpc("SetCurrentSceneTransitionSettings",{{"transitionSettings",old_settings},{"overlay",false}});
                }
            } catch(...) { failure=std::current_exception(); }
            try {
                if(have_selection) rpc("SetCurrentSceneTransition",{{"transitionName",old_selected}});
            } catch(...) { if(!failure) failure=std::current_exception(); }
            if(failure) std::rethrow_exception(failure);
        };
        bool completed=true,manual_started=false,release_attempted=false;
        try {
            if(luma||manual||modify) {
                old_selected=rpc("GetCurrentSceneTransition").at("transitionName").get<std::string>(); have_selection=true;
                rpc("SetCurrentSceneTransition",{{"transitionName",name}});
                const auto info=rpc("GetCurrentSceneTransition");
                if(manual&&info.value("transitionFixed",false)) throw ObsRejected("OBS: TRANSITION HAS NO MANUAL CONTROL");
                if(modify) {
                    if(info.at("transitionName")!=name||info.at("transitionKind")!=kinds[unsigned(type)])
                        throw ObsError("OBS DME SELECTION CHANGED");
                    old_settings=info.at("transitionSettings");
                    if(!old_settings.is_object())throw ObsError("OBS DME SETTINGS UNAVAILABLE");
                    have_settings=true;
                    static const char* directions[]={"right","left","down","up"};
                    Json params={{"direction",directions[dme.entry]}};
                    if(type==TransitionType::swipe)params["swipe_in"]=dme.inward;
                    rpc("SetCurrentSceneTransitionSettings",{{"transitionSettings",params},{"overlay",true}});
                }
                if(luma) {
                if(info.at("transitionName")!=name||info.at("transitionKind")!="wipe_transition") throw ObsError("OBS LUMA SELECTION CHANGED");
                old_settings=info.at("transitionSettings");
                if(!old_settings.is_object()) throw ObsError("OBS LUMA SETTINGS UNAVAILABLE");
                have_settings=true;
                rpc("SetCurrentSceneTransitionSettings",{{"transitionSettings",{{"luma_image",pattern},{"luma_invert",reverse},{"luma_softness",softness/100.0}}},{"overlay",true}});
                }
            }
            rpc("SetSceneSceneTransitionOverride",use);
            const auto ended=transition_ends;
            const auto program_before=program_revision;
            waiting_stinger=fixed; stinger_end_base=ended; stinger_video_base=video_ends;
            if(manual) {
                manual_started=true;completed=drive_tbar(release_attempted);
                if(completed) {
                    wait_transition(ended,5000);
                    // The transition signal comes from the render thread. OBS
                    // queues its Studio Mode scene swap on the UI thread; wait
                    // for that frontend notification before editing preview or
                    // restoring the selected transition.
                    const auto deadline=Clock::now()+std::chrono::seconds(5);
                    while(program_revision==program_before&&!stop&&Clock::now()<deadline) {
                        Json j;if(receive(j))event(j);else pause(2);
                    }
                    if(program_revision==program_before) throw ObsError("OBS MANUAL SCENE COMPLETION TIMEOUT");
                    if(rpc("GetCurrentProgramScene").at("sceneName")!=current)
                        throw ObsError("OBS MANUAL PROGRAM MISMATCH");
                }
            } else {
                rpc("TriggerStudioModeTransition");
                wait_transition(ended,fixed?65000:duration+5000);
            }
            waiting_stinger=false;
            const auto after=rpc("GetCurrentPreviewScene").at("sceneName");
            if(completed&&(manual?after!=previous:after==current)) rpc("SetCurrentPreviewScene",{{"sceneName",previous}});
            rpc("SetSceneSceneTransitionOverride",restore);
            restore_transition();
        } catch(...) {
            waiting_stinger=false;
            if(manual_started&&!release_attempted) {try {rpc("SetTBarPosition",{{"position",0.0},{"release",true}});}catch(...) {}}
            try { rpc("SetSceneSceneTransitionOverride",restore); } catch(...) {}
            try { restore_transition(); } catch(...) {}
            throw;
        }
        if(manual) {std::lock_guard<std::mutex> lock(mutex);view.transitioning=false;}
        if(!cut&&completed) { std::lock_guard<std::mutex> lock(mutex); ++view.completed_auto; }
    }
    void run() {
        while(!stop) {
            Config c; unsigned gen; std::string session_password;
            { std::lock_guard<std::mutex> lock(mutex); c=config; gen=generation; session_password=password; }
            bool enabled; { std::lock_guard<std::mutex> lock(mutex); enabled=configured; }
            if(!enabled) { pause(25); continue; }
            if(c.backend!=Backend::obs) { status("ADAPTER NOT IMPLEMENTED"); pause(200); continue; }
            try {
                status("OBS CONNECTING");
                socket=curl_easy_init(); if(!socket) throw ObsError("OBS TRANSPORT INIT FAILED");
                char url[80]; std::snprintf(url,sizeof url,"ws://%u.%u.%u.%u:%u",c.active().host[0],c.active().host[1],c.active().host[2],c.active().host[3],c.active().port?c.active().port:4455);
                curl_easy_setopt(socket,CURLOPT_URL,url); curl_easy_setopt(socket,CURLOPT_CONNECT_ONLY,2L);
                curl_easy_setopt(socket,CURLOPT_CONNECTTIMEOUT_MS,2000L); curl_easy_setopt(socket,CURLOPT_TIMEOUT_MS,3000L);
                curl_easy_setopt(socket,CURLOPT_NOSIGNAL,1L); curl_easy_setopt(socket,CURLOPT_PROXY,"");
                if(curl_easy_perform(socket)!=CURLE_OK) throw ObsError("OBS CONNECTION FAILED");
                incoming.clear(); { std::lock_guard<std::mutex> lock(mutex); names=assigned; }
                const auto hello=wait_op(0);
                Json identify={{"rpcVersion",1},{"eventSubscriptions",2047}};
                if(hello.contains("authentication")) {
                    if(session_password.empty())throw ObsError("OBS AUTH REQUIRED - CONFIGURE IN WEB");
                    const auto& a=hello.at("authentication");
                    identify["authentication"]=digest(digest(session_password+a.at("salt").get<std::string>())+a.at("challenge").get<std::string>());
                }
                send({{"op",1},{"d",identify}}); wait_op(2);
                try{const auto v=rpc("GetVersion");std::lock_guard<std::mutex> lock(mutex);if(gen!=generation)throw ObsError("OBS CONFIGURATION CHANGED");software_version=v.value("obsVersion",std::string());websocket_version=v.value("obsWebSocketVersion",std::string());}catch(const ObsRejected&){ }
                refresh(gen);refresh_outputs();
                auto refreshed=Clock::now();
                while(!stop) {
                    MixerAction action; unsigned index; bool studio; TransitionType type; uint32_t duration,code,softness; bool reverse;
                    { std::lock_guard<std::mutex> lock(mutex);
                      if(gen!=generation) break;
                      action=command; index=source; studio=view.studio; type=auto_type; duration=auto_duration; code=auto_code; reverse=auto_reverse; softness=auto_softness; command=MixerAction::none; }
                    if(action!=MixerAction::none) {
                        if(action==MixerAction::output) {
                            try { toggle_output(index); }
                            catch(const ObsRejected& e) { status(e.what(),true); }
                            { std::lock_guard<std::mutex> lock(mutex);view.busy=false; }
                            continue;
                        }
                        if(action==MixerAction::dsk_mix||action==MixerAction::dsk_cut) {
                            try { take_dsk(action==MixerAction::dsk_mix,duration,index); }
                            catch(const ObsRejected& e) {
                                status(e.what(),true);
                                std::lock_guard<std::mutex> lock(mutex);view.dsk_known_at(index)=view.dsk_mixing_at(index)=false;
                            }
                            { std::lock_guard<std::mutex> lock(mutex);view.busy=false; }
                            continue;
                        }
                        if(!studio) throw ObsError("OBS: ENABLE STUDIO MODE");
                        if(action==MixerAction::preview) rpc("SetCurrentPreviewScene",{{"sceneName",names.at(index)}});
                        else if(action==MixerAction::program) {
                            // Frontend SetCurrentProgramScene can transition. Force a
                            // per-destination Cut override, then restore it; never stage
                            // the destination through preview and never replay this take.
                            const auto target=names.at(index);
                            const auto old=rpc("GetSceneSceneTransitionOverride",{{"sceneName",target}});
                            const auto transitions=rpc("GetSceneTransitionList");
                            std::string cut;
                            for(const auto& t:transitions.at("transitions"))
                                if(t.at("transitionKind")=="cut_transition") { cut=t.at("transitionName").get<std::string>();break; }
                            if(cut.empty()) throw ObsError("OBS: CUT TRANSITION MISSING");
                            Json restore={{"sceneName",target},{"transitionName",old.at("transitionName").is_null()?Json(""):old.at("transitionName")}};
                            if(old.contains("transitionDuration")&&!old.at("transitionDuration").is_null()) restore["transitionDuration"]=old.at("transitionDuration");
                            try {
                                rpc("SetSceneSceneTransitionOverride",{{"sceneName",target},{"transitionName",cut}});
                                rpc("SetCurrentProgramScene",{{"sceneName",target}});
                            } catch(...) {
                                try { rpc("SetSceneSceneTransitionOverride",restore); } catch(...) {}
                                throw;
                            }
                            rpc("SetSceneSceneTransitionOverride",restore);
                        }
                        else {
                            take(type,duration,code,reverse&&(action==MixerAction::automatic||action==MixerAction::manual),softness,action==MixerAction::cut,action==MixerAction::manual);
                            // A missing/ambiguous mapping is a local refusal, not a disconnect.
                            { std::lock_guard<std::mutex> lock(mutex); if(!view.busy) {manual_active=false;continue;} }
                        }
                        refresh(gen);
                        { std::lock_guard<std::mutex> lock(mutex); view.busy=false;manual_active=false; }
                        refreshed=Clock::now();
                    }
                    Json j; if(receive(j)) event(j);
                    if(dirty||Clock::now()-refreshed>std::chrono::seconds(1)) { refresh(gen); refresh_dsk(); refresh_outputs(); refreshed=Clock::now(); }
                    pause(5);
                }
            } catch(const ObsError& error) {
                status(error.what());
            } catch(const std::exception&) {
                // Do not log server JSON, credentials or authentication material.
                status("OBS DISCONNECTED / CHECK CONNECTION");
            }
            if(socket) {
                const unsigned char close_code[]={3,232}; size_t sent=0;
                curl_ws_send(socket,close_code,sizeof close_code,&sent,0,CURLWS_CLOSE);
                curl_easy_cleanup(socket); socket=nullptr;
            }
            incoming.clear(); pause(1000);
        }
    }
};
ObsAdapter::ObsAdapter(const std::string& p,const std::array<std::string,24>& s,const std::string& path):impl(nullptr) {
    static const auto initialized=curl_global_init(CURL_GLOBAL_DEFAULT);
    if(initialized!=CURLE_OK) throw ObsError("CURL INIT FAILED");
    std::string secret=p;
    if(!path.empty()) {std::ifstream file(path);if(file){try{Json j;file>>j;secret=j.at("obs_password").get<std::string>();}catch(...){secret.clear();}}}
    impl=new Impl(secret,s);impl->credentials_path=path;
}
ObsAdapter::~ObsAdapter() { delete impl; }
void ObsAdapter::deactivate() {
    Config disabled; disabled.backend=Backend::midi;
    configure(disabled); // Invalidates queued commands and makes the worker close its session.
}
void ObsAdapter::configure(const Config& c) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->software_version.clear();impl->websocket_version.clear();
    impl->config=c; impl->configured=true; ++impl->generation; impl->command=MixerAction::none; impl->view={};impl->manual_abort=true;
}
bool ObsAdapter::set_password(const std::string& secret) {
    if(secret.size()>1024||secret.find('\0')!=std::string::npos)return false;
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(impl->view.busy||impl->view.transitioning)return false;
    if(!impl->credentials_path.empty()) {
        std::string pattern=impl->credentials_path+".tmp-XXXXXX";std::vector<char> name(pattern.begin(),pattern.end());name.push_back(0);
        int fd=mkstemp(name.data());if(fd<0)return false;
        auto text=Json({{"obs_password",secret}}).dump()+"\n";size_t at=0;
        while(at<text.size()){auto n=write(fd,text.data()+at,text.size()-at);if(n<0&&errno==EINTR)continue;if(n<=0)break;at+=size_t(n);}
        bool ok=at==text.size()&&fsync(fd)==0;close(fd);
        if(!ok||rename(name.data(),impl->credentials_path.c_str())<0){unlink(name.data());return false;}
    }
    impl->password=secret;++impl->generation;impl->command=MixerAction::none;impl->view={};return true;
}
void ObsAdapter::set_transitions(const TransitionSettings& settings) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(impl->transition_settings.dsk_name!=settings.dsk_name) impl->view.dsk_known=impl->view.dsk_on=false;
    if(impl->transition_settings.dsk2_name!=settings.dsk2_name)impl->view.dsk2_known=impl->view.dsk2_on=impl->view.dsk2_mixing=false;
    impl->transition_settings=settings;
}
bool ObsAdapter::output(OutputKind kind) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(unsigned(kind)>=3||!impl->view.connected||impl->view.busy) return false;
    impl->source=unsigned(kind);impl->command=MixerAction::output;impl->view.busy=true;return true;
}
bool ObsAdapter::dsk(bool mix,uint32_t duration,unsigned slot) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(slot>1||(slot&&!impl->transition_settings.dsk2_name[0])||!impl->view.connected||impl->view.busy||impl->view.transitioning||duration<50||duration>20000) return false;
    impl->source=slot;impl->command=mix?MixerAction::dsk_mix:MixerAction::dsk_cut;
    impl->auto_duration=duration;impl->view.busy=true;return true;
}
bool ObsAdapter::manual(uint16_t position,TransitionType type,uint32_t code,bool reverse,uint32_t softness) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(position>4095) return false;
    if(impl->manual_active) {
        if(!impl->manual_terminal) {impl->manual_position=position;impl->manual_terminal=position==0||position==4095;}
        impl->manual_seen=Clock::now();return true;
    }
    if(type==TransitionType::stinger) {
        std::snprintf(impl->view.message.data(),impl->view.message.size(),"OBS: STINGER REQUIRES AUTO");return false;
    }
    if(unsigned(type)>2||softness>100||code>999999||!impl->view.connected||!impl->view.studio||
       !impl->view.sources||impl->view.busy||impl->view.transitioning) return false;
    impl->manual_active=true;impl->manual_abort=false;impl->manual_terminal=position==0||position==4095;
    impl->manual_position=position;impl->manual_seen=Clock::now();
    impl->auto_type=type;impl->auto_duration=300;impl->auto_code=code;impl->auto_reverse=reverse;impl->auto_softness=softness;
    impl->command=MixerAction::manual;impl->view.busy=true;return true;
}
void ObsAdapter::cancel_manual() {std::lock_guard<std::mutex> lock(impl->mutex);impl->manual_abort=true;}
void ObsAdapter::dme_parameters(DmeParameters p) {
    if(p.entry>3)return;
    std::lock_guard<std::mutex> lock(impl->mutex);impl->requested_dme=p;
}
bool ObsAdapter::automatic(TransitionType type,uint32_t duration,uint32_t code,bool reverse,uint32_t softness) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(unsigned(type)>5||softness>100||code>999999||duration<50||duration>20000||!impl->view.connected||!impl->view.studio||
       !impl->view.sources||impl->view.busy||impl->view.transitioning) return false;
    impl->auto_dme=impl->requested_dme;
    impl->auto_type=type; impl->auto_duration=duration; impl->auto_code=code; impl->auto_reverse=reverse; impl->auto_softness=softness; impl->command=MixerAction::automatic; impl->view.busy=true; return true;
}
void ObsAdapter::set_sources(const std::array<std::string,24>& scenes) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->assigned=scenes;impl->manual_abort=true;++impl->generation;impl->command=MixerAction::none;impl->view={};
}
std::string ObsAdapter::web_status() {
    std::lock_guard<std::mutex> lock(impl->mutex);
    Json patterns=Json::array(),defaults=Json::array();
    for(const auto& p:LumaPatterns) patterns.push_back({{"file",p.file},{"label",p.label}});
    for(unsigned code:{23u,5u,21u,24u,18u,9u,6u,1u,3u,17u}) defaults.push_back({{"code",code},{"pattern",default_luma_pattern(code)}});
    return Json({{"authRequired",impl->view.auth_required},{"passwordSet",!impl->password.empty()},{"lumaPatterns",patterns},{"wipeDefaults",defaults},{"connected",impl->view.connected},{"busy",impl->view.busy},{"transitioning",impl->view.transitioning},
                 {"message",impl->view.message.data()},{"scenes",impl->scene_names},{"transitions",impl->transition_list},
                 {"outputKnown",impl->view.output_known},{"outputActive",impl->view.output_active},{"dskKnown",impl->view.dsk_known},{"dskOn",impl->view.dsk_on},{"dskMixing",impl->view.dsk_mixing},{"dsk2Known",impl->view.dsk2_known},{"dsk2On",impl->view.dsk2_on},{"dsk2Mixing",impl->view.dsk2_mixing},{"program",impl->view.program},{"preview",impl->view.preview}}).dump();
}
MixerState ObsAdapter::state() {
    std::lock_guard<std::mutex> lock(impl->mutex);
    auto state=impl->view;state.manual_transition=impl->manual_active;state.both_sources=state.transitioning;return state;
}
bool ObsAdapter::request(MixerAction a,unsigned s) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(!impl->view.connected||!impl->view.studio||impl->view.busy||impl->view.transitioning||s>=24||(a==MixerAction::none||unsigned(a)>=unsigned(MixerAction::automatic))) return false;
    if((a==MixerAction::preview||a==MixerAction::program)&&!impl->view.available[s]) return false;
    impl->command=a; impl->source=s; impl->view.busy=true; return true;
}
bool load_mappings(const std::string& path,Mappings& mappings,std::array<std::string,24>& scenes) {
    try {
        std::ifstream file(path); if(!file) return false;
        Json j; file>>j; if(!j.is_object()||!j.at("version").is_number_unsigned()||j.at("version")!=1) return false;
        for(auto it=j.begin();it!=j.end();++it) if(it.key()!="version"&&it.key()!="sources"&&it.key()!="buttons"&&it.key()!="tallies"&&it.key()!="transitions") return false;
        auto rows=j.at("sources"); if(!rows.is_array()||rows.size()>24) return false;
        std::array<std::string,24> next{};
        for(const auto& row:rows) {
            if(!row.at("slot").is_number_unsigned()) return false;
            const unsigned index=row.at("slot").get<unsigned>(); const auto name=row.at("scene").get<std::string>();
            if(index>=24||name.empty()||name.size()>512||!next[index].empty()) return false;
            for(const auto& other:next) if(other==name) return false;
            next[index]=name;
        }
        Mappings result;
        if(j.contains("transitions")) {
            const auto& tr=j.at("transitions"); if(!tr.is_object()) return false;
            const char* names[]={"mix","wipe","dme","stinger","dme_slide","dme_swipe"};
            const char* reverse_names[]={"mix_reverse","wipe_reverse","dme_reverse","stinger_reverse"};
            const char* rates[]={"auto_ms","dsk_ms","ftb_ms"};
            for(auto it=tr.begin();it!=tr.end();++it) {
                bool known=false;
                for(unsigned i=0;i<6;i++) if(it.key()==names[i]) {
                    if(!it.value().is_string()) return false;
                    const auto name=it.value().get<std::string>();
                    if(name.size()>512||name.find('\0')!=std::string::npos) return false;
                    std::copy(name.begin(),name.end(),result.transitions.names[i].begin()); known=true;
                }
                for(unsigned i:{1u,3u}) if(it.key()==reverse_names[i]) {
                    if(!it.value().is_string()) return false;
                    const auto name=it.value().get<std::string>();
                    if(name.size()>512||name.find('\0')!=std::string::npos) return false;
                    std::copy(name.begin(),name.end(),result.transitions.reverse_names[i].begin()); known=true;
                }
                for(unsigned i=0;i<3;i++) if(it.key()==rates[i]) {
                    if(!it.value().is_number_unsigned()||it.value()<50||it.value()>20000) return false;
                    result.transitions.rates[i]=it.value().get<uint32_t>(); known=true;
                }
                for(unsigned slot=0;slot<10;slot++) if(slot!=1) {
                    const auto key="stinger_"+std::to_string(slot);
                    if(it.key()==key||it.key()==key+"_reverse") {
                        if(!it.value().is_string())return false;
                        const auto name=it.value().get<std::string>();
                        if(name.size()>512||name.find('\0')!=std::string::npos)return false;
                        auto& dest=(it.key()==key?result.transitions.stingers:result.transitions.stinger_reverse)[slot];
                        std::copy(name.begin(),name.end(),dest.begin());known=true;
                    }
                }
                if(it.key()=="dsk_name"||it.key()=="dsk_scene"||it.key()=="dsk2_name"||it.key()=="dsk2_scene") {
                    if(!it.value().is_string()) return false;
                    const auto text=it.value().get<std::string>();
                    if(text.size()>512||text.find('\0')!=std::string::npos) return false;
                    auto& dest=it.key()=="dsk_name"?result.transitions.dsk_name:it.key()=="dsk_scene"?result.transitions.dsk_scene:it.key()=="dsk2_name"?result.transitions.dsk2_name:result.transitions.dsk2_scene;
                    dest.fill(0);std::copy(text.begin(),text.end(),dest.begin());known=true;
                }
                if(it.key()=="wipe_pattern") {
                    if(!it.value().is_string()) return false;
                    const auto pattern=it.value().get<std::string>();
                    if(!valid_luma_pattern(pattern.c_str())||pattern.find('\0')!=std::string::npos) return false;
                    result.transitions.wipe_pattern.fill(0);std::copy(pattern.begin(),pattern.end(),result.transitions.wipe_pattern.begin());known=true;
                }
                if(it.key()=="softness") {
                    if(!it.value().is_number_unsigned()||it.value()>100) return false;
                    result.transitions.softness=it.value().get<uint32_t>();known=true;
                }
                if(it.key()=="wipe_codes") {
                    if(!it.value().is_array()||it.value().size()>32) return false;
                    unsigned at=0;
                    for(const auto& row:it.value()) {
                        if(!row.is_object()||!row.at("code").is_number_unsigned()||row.at("code")==0||row.at("code")>999999) return false;
                        for(auto field=row.begin();field!=row.end();++field)
                            if(field.key()!="code"&&field.key()!="pattern"&&field.key()!="transition"&&field.key()!="reverse") return false;
                        const auto code=row.at("code").get<uint32_t>();
                        for(unsigned i=0;i<at;i++) if(result.transitions.wipes[i].code==code) return false;
                        auto& wipe=result.transitions.wipes[at++];wipe.code=code;
                        if(row.contains("pattern")) {
                            if(!row.at("pattern").is_string()) return false;
                            const auto pattern=row.at("pattern").get<std::string>();
                            if((!pattern.empty()&&!valid_luma_pattern(pattern.c_str()))||pattern.find('\0')!=std::string::npos) return false;
                            wipe.has_pattern=true;std::copy(pattern.begin(),pattern.end(),wipe.pattern.begin());
                        }
                        for(const auto* field:{"transition","reverse"}) if(row.contains(field)) {
                            if(!row.at(field).is_string()) return false;
                            const auto name=row.at(field).get<std::string>();
                            if(name.size()>512||name.find('\0')!=std::string::npos) return false;
                            auto& dest=std::string(field)=="transition"?wipe.name:wipe.reverse;
                            std::copy(name.begin(),name.end(),dest.begin());
                        }
                        if(!wipe.has_pattern&&!wipe.name[0]) return false;
                    }
                    known=true;
                }
                if(!known) return false;
            }
        }
        if(j.contains("buttons")) {
            if(!j.at("buttons").is_array()) return false;
            result.buttons={}; std::bitset<KeyCount> used;
            for(const auto& row:j.at("buttons")) {
                if(!row.at("id").is_number_unsigned()||(row.contains("source")&&!row.at("source").is_number_unsigned())) return false;
                auto id=row.at("id").get<unsigned>(); auto action=row.at("action").get<std::string>();
                unsigned source=row.value("source",0u);
                if(id>=KeyCount||used[id]||source>=12||(action!="preview"&&action!="cut"&&action!="program")) return false;
                used.set(id); result.buttons[id]={action=="preview"?MixerAction::preview:action=="program"?MixerAction::program:MixerAction::cut,uint8_t(source)};
            }
        }
        if(j.contains("tallies")) {
            if(!j.at("tallies").is_array()) return false;
            result.tallies={}; std::bitset<KeyCount> used;
            for(const auto& row:j.at("tallies")) {
                if(!row.at("led").is_number_unsigned()||!row.at("source").is_number_unsigned()||(row.contains("level")&&!row.at("level").is_number_unsigned())) return false;
                auto id=row.at("led").get<unsigned>(); auto bus=row.at("bus").get<std::string>();
                auto source=row.at("source").get<unsigned>(); auto level=row.value("level",2u);
                if(id>=KeyCount||used[id]||source>=12||level<1||level>2||(bus!="preview"&&bus!="program")) return false;
                used.set(id); result.tallies[id]={bus=="preview"?TallyBus::preview:TallyBus::program,uint8_t(source),uint8_t(level)};
            }
        }
        if(!valid_mappings(result)) return false;
        mappings=result; scenes=next; return true;
    } catch(const std::exception&) { return false; }
}
void print_mapping_schema() {
    std::cout<<R"({"version":1,"sourceSlots":24,"layers":2,"shiftButton":80,"sharedAcrossBuses":true,"actions":[{"id":"program","obsRequest":"SetCurrentProgramScene","parameter":"source","behavior":"hot punch with temporary Cut override"},{"id":"preview","obsRequest":"SetCurrentPreviewScene","parameter":"source"},{"id":"cut","obsRequest":"TriggerStudioModeTransition","requires":"Studio mode; forces Cut with temporary scene override"}],"feedback":["preview","program"],"transitionTypes":["mix","wipe","dme","stinger","slide","swipe"],"transitionSettings":["mix","wipe","dme","stinger","dme_slide","dme_swipe","auto_ms","dsk_ms","ftb_ms","wipe_codes","stinger_reverse","stinger_0","stinger_0_reverse","stinger_2","stinger_2_reverse","stinger_3","stinger_3_reverse","stinger_4","stinger_4_reverse","stinger_5","stinger_5_reverse","stinger_6","stinger_6_reverse","stinger_7","stinger_7_reverse","stinger_8","stinger_8_reverse","stinger_9","stinger_9_reverse","wipe_pattern","softness","dsk_name","dsk_scene","dsk2_name","dsk2_scene"],"durationMs":{"min":50,"max":20000}})"<<'\n';
}
}

namespace bkds::link {
bool ObsAdapter::server_info(ServerInfo& out)const{std::lock_guard<std::mutex> lock(impl->mutex);out.fields={{"SERVER","OBS Studio"},{"VERSION",impl->software_version.empty()?"NOT REPORTED":impl->software_version},{"WEBSOCKET",impl->websocket_version.empty()?"NOT REPORTED":impl->websocket_version},{"LINK",impl->view.connected?"CONNECTED":"DISCONNECTED"}};return true;}
}

namespace bkds::link {
bool ObsAdapter::valid_dme_code(uint32_t code)const {return code<=2&&keypad_transition_available(TransitionType::dme,code);}
bool ObsAdapter::keypad_transition_available(TransitionType type,unsigned slot)const {
    std::lock_guard<std::mutex> lock(impl->mutex);if(!impl->view.connected)return false;
    unsigned kindIndex=type==TransitionType::dme?(slot==0?2:slot==1?4:5):3;
    if(type==TransitionType::dme&&slot>2)return false;
    if(type!=TransitionType::dme&&type!=TransitionType::stinger)return true;
    std::string configured=impl->transition_settings.names[kindIndex].data();
    if(type==TransitionType::stinger){if(slot>9)return false;if(slot!=1)configured=impl->transition_settings.stingers[slot].data();if(slot!=1&&configured.empty())return false;}
    static const char* kinds[]={"fade_transition","wipe_transition","move_transition","obs_stinger_transition","slide_transition","swipe_transition"};
    unsigned count=0;for(const auto& item:impl->transition_list)if(item.is_object()&&item.contains("transitionName")&&item.contains("transitionKind")&&((!configured.empty()&&item["transitionName"]==configured)||(configured.empty()&&item["transitionKind"]==kinds[kindIndex])))++count;
    return count==1;
}
}
