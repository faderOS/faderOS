#include "link/atem_posix.hpp"
#include <nlohmann/json.hpp>
#include <cassert>
#include <chrono>
#include <thread>
#include <iostream>
using namespace bkds::link;using Json=nlohmann::json;
template<class F> void wait(AtemAdapter& a,F f){auto until=std::chrono::steady_clock::now()+std::chrono::seconds(8);while(!f()&&std::chrono::steady_clock::now()<until)std::this_thread::sleep_for(std::chrono::milliseconds(5));if(!f()){std::cerr<<a.web_status()<<"\nTimed out\n";std::abort();}}
int main(int argc,char** argv){assert(argc>=4);Config c;c.backend=Backend::atem;assert(parse_ip(argv[1],c.active().host));c.active().port=uint16_t(std::stoi(argv[2]));AtemAdapter a(argv[3]);a.configure(c);
    wait(a,[&]{return a.state().connected;});auto j=Json::parse(a.web_status());std::cout<<j.dump(2)<<std::endl;if(argc<5)return 0;
    if(std::string(argv[4])=="controls"){
        VideoFormats formats;assert(a.video_formats(formats)&&formats.current==12&&formats.supported.size()==2);assert(!a.set_video_format(255,12)&&!a.set_video_format(10,10));assert(a.set_video_format(10,12));wait(a,[&]{return !a.state().busy;});assert(a.video_formats(formats)&&formats.current==10);assert(a.set_video_format(12,10));wait(a,[&]{return !a.state().busy;});
        OutputRoutes routes;assert(a.output_routes(routes)&&routes.program==10010&&routes.multiview==9001&&routes.sources[0]==10010&&routes.sources[1]==9001);
        assert(!a.set_output_route(2,true));assert(a.set_output_route(0,true));wait(a,[&]{return !a.state().busy;});assert(a.output_routes(routes)&&routes.sources[0]==9001);
        assert(a.set_output_source(0,1));wait(a,[&]{return !a.state().busy;});assert(!a.set_output_source(1,1));assert(a.set_output_source(1,10011));wait(a,[&]{return !a.state().busy;});assert(a.output_routes(routes)&&routes.sources[0]==1&&routes.sources[1]==10011&&routes.choices[1].size()==3);assert(a.set_output_source(1,9001));wait(a,[&]{return !a.state().busy;});
        assert(a.set_output_route(0,false));wait(a,[&]{return !a.state().busy;});
        assert(!a.output(OutputKind::virtualcam));for(auto kind:{OutputKind::stream,OutputKind::record}){assert(a.output(kind));wait(a,[&]{return !a.state().busy;});assert(a.state().output_active[unsigned(kind)]);assert(a.output(kind));wait(a,[&]{return !a.state().busy;});assert(!a.state().output_active[unsigned(kind)]);}
        MultiviewSettings mv;assert(a.multiview_settings(0,mv)&&mv.count==2&&mv.windows[0].safe==0&&mv.windows[0].meters==0);
        assert(!a.set_multiview_window(2,0,true,true)&&!a.set_multiview_window(0,1,true,true)&&!a.set_multiview_window(1,3,false,true));
        assert(a.set_multiview_window(0,0,true,true));wait(a,[&]{return !a.state().busy;});assert(a.multiview_settings(0,mv)&&mv.windows[0].safe==1);
        assert(a.set_multiview_window(0,0,false,true));wait(a,[&]{return !a.state().busy;});assert(a.multiview_settings(0,mv)&&mv.windows[0].meters==1);
        assert(a.native_key_controls()&&a.key_source_selection());
        assert(a.state().media_known&&a.state().media_still_slots==20);
        assert(a.set_multiview_inputs(0,true,true));wait(a,[&]{return !a.state().busy;});assert(a.multiview_settings(0,mv)&&mv.windows[0].safe==1&&mv.windows[4].safe==1&&mv.windows[2].safe==0&&mv.windows[3].safe==0);
        assert(a.set_multiview_inputs(0,false,true));wait(a,[&]{return !a.state().busy;});assert(a.multiview_settings(0,mv)&&mv.windows[4].meters==1&&mv.windows[2].meters==0&&mv.windows[3].meters==0);
        assert(!a.set_multiview_inputs(2,true,true));
        for(unsigned index:{0u,9u,10u,19u}){assert(a.select_media_still(index));wait(a,[&]{return !a.state().busy;});assert(a.state().media_still==int(index));}
        assert(!a.select_media_still(7)&&!a.select_media_still(20));
        assert(a.supports_media_capture());const auto before_capture=a.state();
        assert(a.capture_media_still());assert(!a.capture_media_still());wait(a,[&]{return a.state().media_capture_status!=1;});
        assert(a.state().media_capture_status==2&&a.state().media_capture_slot==7&&a.state().media_still==before_capture.media_still);
        assert(a.state().program==before_capture.program&&a.state().preview==before_capture.preview);
        const auto captured=Json::parse(a.web_status());for(const char* bus:{"program","preview"})assert(captured["mixEffects"][2][bus]==j["mixEffects"][2][bus]);
        assert(a.capture_media_still());wait(a,[&]{return !a.state().busy;});assert(a.state().media_capture_status==1);
        assert(a.set_rate(26));wait(a,[&]{return !a.state().busy;}); // capture waiting does not block other controls
        wait(a,[&]{return a.state().media_capture_status!=1;});assert(a.state().connected&&a.state().media_capture_status==3&&a.state().media_capture_slot==-1);
        assert(a.set_rate(25));wait(a,[&]{return !a.state().busy;});
        assert(a.capture_media_still());wait(a,[&]{return a.state().media_capture_status!=1;});assert(a.state().media_capture_status==3&&a.state().media_capture_slot==-1);

        assert(!a.clear_media_still(20)); // bounds protected
        assert(a.clear_media_still(0));assert(!a.clear_media_still(1)&&!a.capture_media_still());
        wait(a,[&]{return a.state().media_clear_status!=1;});assert(a.state().media_clear_status==2&&a.state().media_clear_slot==0);
        assert(!a.select_media_still(0)&&!a.clear_media_still(0));
        assert(a.state().media_still==before_capture.media_still&&a.state().program==before_capture.program&&a.state().preview==before_capture.preview);
        assert(a.clear_media_still(1));wait(a,[&]{return !a.state().busy;});assert(a.state().media_clear_status==1);
        assert(a.set_rate(25));wait(a,[&]{return !a.state().busy;});wait(a,[&]{return a.state().media_clear_status!=1;});assert(a.state().media_clear_status==3&&a.state().connected);
        auto control=[&](unsigned id){assert(a.key_setting(0,0,id));wait(a,[&]{return !a.state().busy;});};
        assert(a.key_setting(0,0,67,0));wait(a,[&]{return !a.state().busy;});control(69);assert(a.state().key_source[0]==0&&a.state().key_cut_source[0]==1);
        control(68);assert(a.state().key_cut_source[0]==0);assert(a.key_setting(0,0,70,1));wait(a,[&]{return !a.state().busy;});assert(a.state().key_cut_source[0]==1);
        assert(!a.key_setting(0,0,67,10));
        control(88);assert(a.state().key_kind[0]==88);control(90);assert(a.state().key_premult[0]==0);control(90);assert(a.state().key_premult[0]==1);control(102);assert(a.state().key_invert[0]==1);
        control(100);assert(a.state().key_mask[0]==1);control(100);assert(a.state().key_mask[0]==0);
        control(91);assert(a.state().key_kind[0]==91);assert(!a.key_setting(0,0,102));
        control(92);assert(a.state().key_kind[0]==92);control(102);assert(a.state().key_invert[0]==1);control(102);assert(a.state().key_invert[0]==0);control(106);assert(a.state().key_kind[0]==106);
        control(96);assert(a.state().key_border[0]==1);control(98);assert(a.state().key_shadow[0]==1);
        control(101);assert(a.state().key_submask[0]==1);control(101);assert(a.state().key_submask[0]==0);
        assert(!a.key_setting(0,0,97));
        assert(a.toggle_key(0));wait(a,[&]{return !a.state().busy;});assert(!a.key_setting(0,0,100));assert(a.state().media_program_known&&a.state().media_on_program&&!a.clear_media_still(19));
        assert(a.toggle_key(0));wait(a,[&]{return !a.state().busy;});
        assert(a.request(MixerAction::program,0));wait(a,[&]{return !a.state().busy;});assert(a.state().media_on_program&&!a.clear_media_still(19));
        assert(a.request(MixerAction::program,2));wait(a,[&]{return !a.state().busy;});assert(a.request(MixerAction::preview,0));wait(a,[&]{return !a.state().busy;});
        assert(a.state().media_program_known&&!a.state().media_on_program);assert(a.clear_media_still(19));
        wait(a,[&]{return a.state().media_clear_status!=1;});assert(a.state().media_clear_status==2&&a.state().media_clear_slot==19);
        const auto before_reject=a.state();assert(a.key_setting(0,0,100));
        wait(a,[&]{return !a.state().busy;});assert(a.state().connected&&a.state().command_failures==1);
        assert(a.state().key_mask[0]==before_reject.key_mask[0]&&a.state().program==before_reject.program&&a.state().preview==before_reject.preview);
        constexpr unsigned remaining[]={4,10,11,12,13,14,15,17};for(unsigned pattern:remaining){
            auto code=800+pattern;assert(a.wipe_name(code)&&a.valid_wipe_code(code));
            assert(a.automatic(TransitionType::wipe,25,code,true,30));wait(a,[&]{return a.state().transitioning;});wait(a,[&]{return !a.state().busy;});
            assert(Json::parse(a.web_status())["mixEffects"][2]["wipePattern"]==pattern);
        }
        std::cout<<"ATEM CONTROL PASS: media banks, key types, mask/invert, DVE edge controls, off-air guards and eight extra wipes\n";return 0;
    }
    if(std::string(argv[4])=="pause-cut"){
        assert(a.live_transition_controls());
        auto start=[&]{assert(a.automatic(TransitionType::wipe,200,23));wait(a,[&]{return a.state().transitioning;});};
        start();assert(a.automatic(TransitionType::wipe,0,999)); // active AUTO never reconfigures the take
        std::this_thread::sleep_for(std::chrono::milliseconds(450));assert(a.state().transitioning&&a.state().busy);
        assert(a.automatic(TransitionType::mix,0));wait(a,[&]{return !a.state().busy;});
        start();assert(a.request(MixerAction::cut,0));wait(a,[&]{return !a.state().busy&&!a.state().transitioning;});
        start();assert(a.automatic(TransitionType::mix,0));std::this_thread::sleep_for(std::chrono::milliseconds(450));assert(a.state().transitioning);
        assert(a.request(MixerAction::cut,0));wait(a,[&]{return !a.state().busy&&!a.state().transitioning;});
        assert(a.manual(2048,TransitionType::wipe,23));wait(a,[&]{return a.state().transitioning;});
        const auto count=a.state().completed_auto;assert(a.request(MixerAction::cut,0));wait(a,[&]{return !a.state().busy;});assert(a.state().completed_auto==count+1);
        std::cout<<"ATEM AUTO/CUT PASS: native pause/resume, CUT while running and paused, no parameter replacement\n";return 0;
    }
    if(std::string(argv[4])=="live-buses"){
        assert(a.live_bus_changes());
        auto profile=j["profile"];for(unsigned i=0;i<8;i++)profile["sources"][i]=std::to_string(i+1);std::string error;
        assert(a.save_mappings(Json({{"revision",j["revision"]},{"profile",profile}}).dump(),error));
        auto select=[&](MixerAction action,unsigned slot){assert(a.request(action,slot));wait(a,[&]{return (action==MixerAction::program?a.state().program:a.state().preview)==int(slot);});};
        assert(a.automatic(TransitionType::wipe,200,23));wait(a,[&]{return a.state().transitioning;});
        select(MixerAction::preview,0);select(MixerAction::program,1);
        wait(a,[&]{return !a.state().busy;});assert(a.state().program==0&&a.state().preview==1);
        assert(a.manual(2048,TransitionType::wipe,23));wait(a,[&]{return a.state().transitioning;});
        select(MixerAction::preview,2);select(MixerAction::program,3);
        assert(a.manual(4095,TransitionType::wipe,23));wait(a,[&]{return !a.state().busy;});assert(a.state().program==2&&a.state().preview==3);
        assert(a.set_transition_preview(true));wait(a,[&]{return !a.state().busy;});
        assert(a.manual(2048,TransitionType::wipe,23));wait(a,[&]{return a.state().transitioning;});
        select(MixerAction::preview,4);select(MixerAction::program,5);
        assert(a.manual(4095,TransitionType::wipe,23));wait(a,[&]{return !a.state().busy;});assert(a.state().program==5&&a.state().preview==4);
        assert(a.set_transition_preview(false));wait(a,[&]{return !a.state().busy;});
        assert(a.manual(1234,TransitionType::wipe,23));wait(a,[&]{return a.state().program==6&&a.state().preview==7;}); // external peer source change
        assert(a.manual(4095,TransitionType::wipe,23));wait(a,[&]{return !a.state().busy;});assert(a.state().program==7&&a.state().preview==6);
        std::cout<<"ATEM LIVE BUS PASS: AUTO, T-bar and rehearsal keep source changes and finish without blocking\n";return 0;
    }
    if(std::string(argv[4])=="modifiers"){
        assert(a.wipe_modifier_supported(168)&&a.wipe_modifier_supported(170)&&a.wipe_modifier_supported(174)&&!a.wipe_modifier_supported(173)&&!a.wipe_modifier_supported(171)&&!a.wipe_modifier_supported(172));
        assert(!a.prepare_wipe_modifier(168,101)&&!a.prepare_wipe_modifier(170,101)&&!a.prepare_wipe_modifier(174,1001,500));
        assert(a.prepare_wipe_modifier(168,25)&&a.prepare_wipe_modifier(170,70)&&a.prepare_wipe_modifier(174,100,800));
        assert(a.automatic(TransitionType::wipe,25,23));wait(a,[&]{return a.state().transitioning;});wait(a,[&]{return !a.state().busy;});
        assert(a.automatic(TransitionType::wipe,25,23));wait(a,[&]{return a.state().transitioning;});wait(a,[&]{return !a.state().busy;});
        std::cout<<"ATEM MODIFIER PASS: border, symmetry, origin and capability guards\n";return 0;
    }
    if(std::string(argv[4])=="dip"){
        assert(a.mix_dip()&&a.state().dip_rate_known&&a.state().dip_frames==45);
        assert(a.set_transition_rate(TransitionType::dip,35));wait(a,[&]{return !a.state().busy;});assert(a.state().dip_frames==35&&a.state().auto_frames==25);
        for(unsigned i=0;i<2;i++){assert(a.automatic(TransitionType::dip,35));wait(a,[&]{return a.state().transitioning;});wait(a,[&]{return !a.state().busy;});}
        assert(a.set_transition_preview(true));wait(a,[&]{return !a.state().busy;});assert(a.manual(2048,TransitionType::dip));wait(a,[&]{return a.state().transitioning;});assert(a.manual(4095,TransitionType::dip));wait(a,[&]{return !a.state().busy;});
        auto current=Json::parse(a.web_status());assert(current["mixEffects"][2]["dipInput"]==2001);for(const char* key:{"program","preview","rate","wipeFrames","dveFrames"})assert(current["mixEffects"][2][key]==j["mixEffects"][2][key]);
        std::cout<<"ATEM DIP PASS: COL1, separate frames, AUTO and manual preview\n";return 0;
    }
    if(std::string(argv[4])=="no-dve"){
        assert(!a.supports_dme()&&!a.stinger_slots());assert(!a.automatic(TransitionType::dme,25,4)&&!a.manual(1000,TransitionType::dme,4));assert(!a.set_transition_rate(TransitionType::dme,25)&&!a.automatic(TransitionType::stinger,25));std::cout<<"ATEM CAPABILITY PASS: absent DVE/stinger controls rejected\n";return 0;
    }
    if(std::string(argv[4])=="sting"){
        assert(a.fixed_stinger()&&a.stinger_slots()==1);assert(!a.manual(1000,TransitionType::stinger));
        for(unsigned i=0;i<2;i++){assert(a.automatic(TransitionType::stinger,0));wait(a,[&]{return a.state().transitioning;});wait(a,[&]{return !a.state().busy;});}
        const auto current=Json::parse(a.web_status());for(const char* key:{"program","preview","rate","wipeFrames","dveFrames"})assert(current["mixEffects"][2][key]==j["mixEffects"][2][key]);
        std::cout<<"ATEM STINGER PASS: preloaded AUTO, timings retained, manual rejected\n";return 0;
    }
    if(std::string(argv[4])=="dve"){
        const unsigned me=unsigned(a.state().me);assert(a.supports_dme()&&a.dme_keypad_grid()&&a.state().dme_rate_known&&a.state().dme_frames==37);
        assert(!a.automatic(TransitionType::dme,25,5));assert(a.set_transition_rate(TransitionType::dme,40));wait(a,[&]{return !a.state().busy;});assert(a.state().dme_frames==40&&a.state().wipe_frames==31&&a.state().auto_frames==25);
        const unsigned keys[]={7,8,9,4,6,1,2,3};for(unsigned family=0;family<2;family++)for(unsigned i=0;i<8;i++){
            assert(a.automatic(TransitionType::dme,40,keys[i]+10*family));wait(a,[&]{return a.state().transitioning;});wait(a,[&]{return !a.state().busy;});
            const auto row=Json::parse(a.web_status())["mixEffects"][me];assert(row["dveStyle"]==i+(family?16:24)&&row["dveFlipFlop"]==false);
        }
        assert(a.set_transition_preview(true));wait(a,[&]{return !a.state().busy;});assert(a.manual(1024,TransitionType::dme,19,true));wait(a,[&]{return a.state().transitioning;});assert(a.manual(4095,TransitionType::dme,19,true));wait(a,[&]{return !a.state().busy;});
        const auto row=Json::parse(a.web_status())["mixEffects"][me];assert(row["dveStyle"]==18&&row["dveReverse"]==true);for(const char* key:{"program","preview"})assert(row[key]==j["mixEffects"][me][key]);
        std::cout<<"ATEM DVE PASS: 16 directions/families, separate rate, manual rehearsal, parameter masks\n";return 0;
    }
    if(std::string(argv[4])=="wipe") {
        const unsigned me=unsigned(a.state().me);assert(a.state().wipe_rate_known&&a.state().wipe_frames==31);
        assert(!a.automatic(TransitionType::wipe,25,999));assert(!a.automatic(TransitionType::wipe,25,23,false,101));assert(!a.set_transition_rate(TransitionType::wipe,251));
        assert(a.set_transition_rate(TransitionType::wipe,40));wait(a,[&]{return !a.state().busy;});assert(a.state().wipe_frames==40&&a.state().auto_frames==25);
        const uint32_t codes[]={23,5,21,24,18,9,6,1,3,17};const unsigned patterns[]={6,8,5,7,3,16,9,0,1,2};
        for(unsigned i=0;i<10;i++)for(unsigned reverse=0;reverse<2;reverse++){
            assert(a.automatic(TransitionType::wipe,40,codes[i],reverse,25));wait(a,[&]{return a.state().transitioning;});wait(a,[&]{return !a.state().busy;});
            const auto row=Json::parse(a.web_status())["mixEffects"][me];assert(row["wipePattern"]==patterns[i]&&row["wipeSoftness"]==2500&&row["wipeReverse"]==bool(reverse)&&row["wipeFlipFlop"]==false);
        }
        assert(a.set_transition_preview(true));wait(a,[&]{return !a.state().busy;});assert(!a.automatic(TransitionType::wipe,40,23));
        assert(a.manual(1024,TransitionType::wipe,18,true,80));wait(a,[&]{return a.state().transitioning;});assert(a.manual(4095,TransitionType::wipe,18,true,80));wait(a,[&]{return !a.state().busy;});
        const auto row=Json::parse(a.web_status())["mixEffects"][me];assert(row["wipePattern"]==3&&row["wipeSoftness"]==8000&&row["wipeReverse"]==true);
        for(const char* key:{"program","preview"})assert(row[key]==j["mixEffects"][me][key]);
        assert(a.set_transition_preview(false));wait(a,[&]{return !a.state().busy;});
        std::cout<<"ATEM WIPE PASS: ten presets, reverse/softness, separate rates, manual rehearsal, unsupported codes rejected\n";return 0;
    }
    if(std::string(argv[4])=="manual"||std::string(argv[4])=="manual-physical") {
        const auto before=a.state();const unsigned me=unsigned(before.me);
        assert(before.transition_preview_known&&!before.transitioning&&!before.busy);
        assert(a.set_next_transition(true,{}));wait(a,[&]{return !a.state().busy;});
        assert(a.set_transition_preview(true));wait(a,[&]{return !a.state().busy;});assert(a.state().transition_preview);
        assert(!a.automatic(TransitionType::mix,25));
        assert(a.manual(1024,TransitionType::mix));wait(a,[&]{return Json::parse(a.web_status())["mixEffects"][me]["position"].get<unsigned>()>0;});
        assert(a.state().manual_transition);assert(!a.set_me(me));assert(!a.set_transition_preview(false));
        assert(a.manual(3000,TransitionType::mix));assert(a.manual(4095,TransitionType::mix));wait(a,[&]{return !a.state().busy;});
        auto current=Json::parse(a.web_status());for(const char* key:{"program","preview"})assert(current["mixEffects"][me][key]==j["mixEffects"][me][key]);assert(a.state().key_on==before.key_on);
        assert(a.set_transition_preview(false));wait(a,[&]{return !a.state().busy;});
        // Two full strokes restore the original buses; reversing before the
        // endpoint cancels without a take.
        for(unsigned stroke=0;stroke<2;stroke++){
            assert(a.manual(1800,TransitionType::mix));wait(a,[&]{return a.state().transitioning;});assert(a.state().manual_transition);
            assert(a.manual(4095,TransitionType::mix));wait(a,[&]{return !a.state().busy;});
        }
        assert(a.manual(1000,TransitionType::mix));wait(a,[&]{return a.state().transitioning;});assert(a.manual(0,TransitionType::mix));wait(a,[&]{return !a.state().busy;});
        if(before.key_available[0]){
            std::array<bool,4> arms{};arms[0]=true;assert(a.set_next_transition(false,arms));wait(a,[&]{return !a.state().busy;});
            for(unsigned stroke=0;stroke<2;stroke++){
                assert(a.manual(2000,TransitionType::mix));wait(a,[&]{return a.state().transitioning;});
                assert(a.manual(4095,TransitionType::mix));wait(a,[&]{return !a.state().busy;});
                assert(a.state().key_on[0]==(stroke==0?!before.key_on[0]:before.key_on[0]));
            }
        }
        assert(a.set_transition_preview(true));assert(a.set_transition_preview(false));wait(a,[&]{return !a.state().busy;});assert(!a.state().transition_preview);
        assert(a.set_next_transition(before.next_background,before.next_key));wait(a,[&]{return !a.state().busy;});
        assert(a.set_transition_preview(before.transition_preview));wait(a,[&]{return !a.state().busy;});
        current=Json::parse(a.web_status());for(const char* key:{"program","preview","nextLayers","rate","transitionPreview"})assert(current["mixEffects"][me][key]==j["mixEffects"][me][key]);assert(a.state().key_on==before.key_on);
        std::cout<<"ATEM MANUAL PASS: preview isolation, two full strokes, cancellation, reset coalescing, state restored\n";return 0;
    }
    if(std::string(argv[4])=="keys"||std::string(argv[4])=="keys-physical") {
        const auto me=unsigned(a.state().me);assert(a.keyers()&&a.state().key_available[0]&&a.state().next_known);
        const auto before=a.state();auto original=j["keys"][0];for(const auto& row:j["keys"])if(row["me"]==me&&row["index"]==0)original=row;
        assert(!original["onAir"].get<bool>());std::string error;
        auto settings=original;
        if(std::string(argv[4])=="keys"){settings["fill"]=3;settings["key"]=4;settings["clip"]=100;settings["gain"]=800;auto bad=settings;bad["revision"]=99999;assert(!a.configure_key(bad.dump(),error));bad=settings;bad["key"]=999;assert(!a.configure_key(bad.dump(),error));}
        assert(a.configure_key(settings.dump(),error));wait(a,[&]{return !a.state().busy;});
        assert(!a.toggle_key(255));assert(a.toggle_key(0));wait(a,[&]{return !a.state().busy;});assert(a.state().key_on[0]);
        auto current=original;auto current_meta=Json::parse(a.web_status());
        for(const auto& row:current_meta["keys"])if(row["me"]==me&&row["index"]==0)current=row;
        assert(!a.configure_key(current.dump(),error));assert(a.toggle_key(0));wait(a,[&]{return !a.state().busy;});assert(!a.state().key_on[0]);
        std::array<bool,4> next{};next[0]=true;assert(a.set_next_transition(false,next));wait(a,[&]{return !a.state().busy;});assert(!a.state().next_background&&a.state().next_key[0]);
        assert(a.request(MixerAction::cut,0));wait(a,[&]{return !a.state().busy;});assert(a.state().key_on[0]&&a.state().program==before.program&&a.state().preview==before.preview);
        assert(a.automatic(TransitionType::mix,25));wait(a,[&]{return a.state().transitioning;});assert(!a.state().both_sources);wait(a,[&]{return !a.state().busy;});assert(!a.state().key_on[0]);
        assert(a.set_next_transition(true,next));wait(a,[&]{return !a.state().busy;});
        assert(a.request(MixerAction::cut,0));wait(a,[&]{return !a.state().busy;});assert(a.state().key_on[0]);
        assert(a.request(MixerAction::cut,0));wait(a,[&]{return !a.state().busy;});assert(!a.state().key_on[0]);
        // The reset must supersede a first-click order that is still awaiting ACK/state.
        assert(a.set_next_transition(false,next));assert(a.set_next_transition(true,{}));wait(a,[&]{return !a.state().busy;});assert(a.state().next_background&&!a.state().next_key[0]);
        if(a.state().key_available[3]){
            std::array<bool,4> all{true,true,true,true};assert(a.set_next_transition(true,all));wait(a,[&]{return !a.state().busy;});assert(a.state().next_key==all);
            assert(a.set_next_transition(true,{}));wait(a,[&]{return !a.state().busy;});assert((a.state().next_key==std::array<bool,4>{}&&a.state().key_on==before.key_on));
        }
        current_meta=Json::parse(a.web_status());
        for(const auto& row:current_meta["keys"])if(row["me"]==me&&row["index"]==0)original["revision"]=row["revision"];
        assert(a.configure_key(original.dump(),error));wait(a,[&]{return !a.state().busy;});
        assert(a.set_next_transition(before.next_background,before.next_key));wait(a,[&]{return !a.state().busy;});
        assert(a.set_rate(before.auto_frames));wait(a,[&]{return !a.state().busy;});
        auto final=a.state();assert(final.program==before.program&&final.preview==before.preview&&final.key_on==before.key_on);
        auto final_meta=Json::parse(a.web_status());
        for(const char* field:{"program","preview","nextLayers","rate"})assert(final_meta["mixEffects"][me][field]==j["mixEffects"][me][field]);
        for(const auto& row:final_meta["keys"])if(row["me"]==me&&row["index"]==0)for(const char* field:{"onAir","type","fill","key","clip","gain","premultiplied","invert"})assert(row[field]==original[field]);
        std::cout<<"ATEM KEY PASS: on-air, configuration, key-only and combined CUT/MIX, reset coalescing, original state restored\n";return 0;
    }
    if(std::string(argv[4])=="ftb") {
        assert(a.supports_ftb());const auto initial=a.state();assert(initial.ftb_known&&!initial.ftb_transitioning);
        assert(!a.fade_to_black(0)&&!a.fade_to_black(251)&&!a.set_ftb_rate(251));
        assert(a.set_ftb_rate(30));wait(a,[&]{return !a.state().busy;});assert(a.state().ftb_frames==30);
        if(initial.me_count>1){assert(a.set_me(1));assert(a.state().ftb_frames==25);assert(a.set_me(unsigned(initial.me)));assert(a.state().ftb_frames==30);}
        for(unsigned i=0;i<2;i++){
            assert(a.fade_to_black(25));wait(a,[&]{return a.state().ftb_transitioning;});
            assert(a.state().ftb&&!a.state().transitioning);assert(!a.fade_to_black(25));
            wait(a,[&]{return !a.state().busy;});wait(a,[&]{return !a.state().ftb_transitioning;});
            assert(a.state().ftb==(i==0?!initial.ftb:initial.ftb));
        }
        assert(a.set_ftb_rate(initial.ftb_frames));wait(a,[&]{return !a.state().busy;});
        auto final=Json::parse(a.web_status());assert(final["mixEffects"]==j["mixEffects"]);
        std::cout<<"ATEM FTB PASS: fade in/out, real state, rate sync, M/E isolation, original state restored\n";return 0;
    }
    if(std::string(argv[4])=="dsk-physical") {
        const auto original=j["dsks"][0];assert(original["known"].get<bool>()&&!original["onAir"].get<bool>()&&!original["transitioning"].get<bool>());
        const unsigned rate=original["frames"];std::string error;
        assert(a.configure_dsk(original.dump(),error));wait(a,[&]{return !a.state().busy;});
        assert(a.dsk(false,rate));wait(a,[&]{return !a.state().busy;});assert(a.state().dsk_on);
        assert(a.dsk(false,rate));wait(a,[&]{return !a.state().busy;});assert(!a.state().dsk_on);
        for(bool on:{true,false}){assert(a.dsk(true,rate));wait(a,[&]{return a.state().dsk_mixing;});wait(a,[&]{return !a.state().busy&&!a.state().dsk_mixing;});assert(a.state().dsk_on==on);}
        auto final=Json::parse(a.web_status());for(const char* k:{"fill","key","frames","clip","gain","tie","premultiplied","invert","onAir"})assert(final["dsks"][0][k]==original[k]);
        for(const char* k:{"program","preview"})assert(final["mixEffects"][0][k]==j["mixEffects"][0][k]);
        std::cout<<"ATEM PHYSICAL DSK PASS: CUT and AUTO both directions, configuration and buses preserved\n";return 0;
    }
    if(std::string(argv[4])=="dsk") {
        const auto count=a.dsk_channels();assert(count==1||count==2);assert(a.state().dsk_known_at(0));
        assert(!a.dsk(false,25,count)&&!a.set_dsk_rate(251)&&!a.dsk(true,0));
        std::string error;
        auto settings=Json::parse(a.web_status())["dsks"][0];settings["fill"]=2002;settings["key"]=2001;settings["frames"]=35;
        auto bad=settings;bad["revision"]=99999;assert(!a.configure_dsk(bad.dump(),error));bad=settings;bad["key"]=999;assert(!a.configure_dsk(bad.dump(),error));
        assert(a.configure_dsk(settings.dump(),error));wait(a,[&]{return !a.state().busy;});assert(a.state().dsk_frames[0]==35);
        assert(a.set_dsk_rate(40));wait(a,[&]{return !a.state().busy;});assert(a.state().dsk_frames[0]==40);
        for(unsigned slot=0;slot<count;slot++){
            assert(!a.key_setting(4,slot,91)&&!a.key_setting(4,slot,92)&&!a.key_setting(4,slot,106));
            assert(a.key_setting(4,slot,100));wait(a,[&]{return !a.state().busy;});assert(a.state().dsk_mask[slot]==1);
            assert(a.key_setting(4,slot,100));wait(a,[&]{return !a.state().busy;});assert(a.state().dsk_mask[slot]==0);
            assert(a.key_setting(4,slot,102));wait(a,[&]{return !a.state().busy;});assert(a.state().dsk_invert[slot]==1);
            assert(a.key_setting(4,slot,102));wait(a,[&]{return !a.state().busy;});assert(a.state().dsk_invert[slot]==0);
            assert(a.dsk(false,25,slot));wait(a,[&]{return !a.state().busy;});assert(a.state().dsk_on_at(slot));
            auto current=Json::parse(a.web_status())["dsks"][slot];assert(!a.configure_dsk(current.dump(),error));
            assert(a.dsk(true,30,slot));wait(a,[&]{return a.state().dsk_mixing_at(slot);});assert(!a.dsk(false,25,slot));
            wait(a,[&]{return !a.state().busy;});assert(a.state().dsk_mixing_at(slot)); // buses free while DSK runs
            wait(a,[&]{return !a.state().dsk_mixing_at(slot);});assert(!a.state().dsk_on_at(slot));
            assert(a.dsk(true,25,slot));wait(a,[&]{return a.state().dsk_mixing_at(slot);});wait(a,[&]{return !a.state().busy&&!a.state().dsk_mixing_at(slot);});assert(a.state().dsk_on_at(slot));
            assert(a.dsk(false,25,slot));wait(a,[&]{return !a.state().busy;});assert(!a.state().dsk_on_at(slot));
        }
        std::cout<<"ATEM DSK PASS: configuration, rate, CUT, AUTO both directions, capabilities and confirmed tallies\n";return 0;
    }
    const auto me=j["profile"]["me"].get<unsigned>();auto pg=j["mixEffects"][me]["program"].get<int>(),pv=j["mixEffects"][me]["preview"].get<int>();assert(pg!=pv);
    auto profile=j["profile"];profile["sources"]=std::array<std::string,24>{};profile["sources"][0]=std::to_string(pg);profile["sources"][1]=std::to_string(pv);std::string error;
    assert(a.save_mappings(Json({{"revision",j["revision"]},{"profile",profile}}).dump(),error));assert(!a.save_mappings(Json({{"revision",j["revision"]},{"profile",profile}}).dump(),error));
    assert(a.state().program==0&&a.state().preview==1);assert(!a.request(MixerAction::program,22));assert(!a.automatic(TransitionType::wipe,25,999));assert(!a.set_rate(251));
    if(std::string(argv[4])=="disconnect"){
        assert(a.request(MixerAction::cut,0));wait(a,[&]{return !a.state().connected;});wait(a,[&]{return a.state().connected;});assert(a.state().program==1&&a.state().preview==0);std::this_thread::sleep_for(std::chrono::milliseconds(500));assert(a.state().program==1);std::cout<<"ATEM PASS: no replay after lost session\n";return 0;
    }
    if(a.state().me_count>1){
        assert(a.state().me_count==4);assert(!a.set_me(4));
        assert(a.set_me(3));assert(a.state().me==3&&!a.state().available[11]&&!a.state().available[23]);
        assert(!a.request(MixerAction::preview,11)&&!a.request(MixerAction::preview,23));
        assert(a.set_me(me));assert(a.state().available[11]&&a.state().available[23]);
        assert(a.request(MixerAction::preview,23));wait(a,[&]{return !a.state().busy;});assert(a.state().preview==11);
        assert(a.request(MixerAction::preview,1));wait(a,[&]{return !a.state().busy;});assert(a.state().preview==1);
        // Per-M/E mapping edits do not delegate the desk or take a source.
        auto meta=Json::parse(a.web_status());auto custom=meta["profile"];auto row=custom["sources"];std::swap(row[0],row[1]);custom["meSources"]["1"]=row;
        assert(a.save_mappings(Json({{"revision",meta["revision"]},{"profile",custom}}).dump(),error));assert(a.state().me==int(me));
        assert(a.set_me(1));assert(a.state().program==1&&a.state().preview==0);assert(a.set_me(me));
    }else {assert(!a.set_me(1)&&a.set_me(0));assert(!a.request(MixerAction::preview,11)&&!a.request(MixerAction::preview,23));}
    assert(a.request(MixerAction::program,1));wait(a,[&]{return !a.state().busy;});assert(a.state().program==1&&a.state().preview==1);
    assert(a.request(MixerAction::preview,0));wait(a,[&]{return !a.state().busy;});assert(a.state().program==1&&a.state().preview==0);
    assert(a.request(MixerAction::cut,0));wait(a,[&]{return !a.state().busy;});assert(a.state().program==0&&a.state().preview==1);
    auto completed=a.state().completed_auto;assert(a.automatic(TransitionType::mix,25));wait(a,[&]{return a.state().transitioning;});wait(a,[&]{return a.state().both_sources;});
    wait(a,[&]{return !a.state().busy;});assert(a.state().connected&&a.state().program==1&&a.state().preview==0&&a.state().completed_auto==completed+1);
    assert(a.request(MixerAction::cut,0));wait(a,[&]{return !a.state().busy;});assert(a.state().program==0&&a.state().preview==1);
    auto rate=j["mixEffects"][me]["rate"].get<unsigned>();assert(a.set_rate(rate));wait(a,[&]{return !a.state().busy;});
    a.deactivate();assert(!a.state().connected&&!a.request(MixerAction::cut,0));std::cout<<"ATEM PASS: preview, hot punch, CUT, MIX/tallies, original buses and rate restored\n";
}
