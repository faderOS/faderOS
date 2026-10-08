#include "setup.hpp"
#include "menu.hpp"
#include "home.hpp"
#include "keypad.hpp"
#include <cstdio>
#include <cstring>
namespace bkds::link {
static IPv4& address(Config& c,unsigned f) { return f==0?c.ip:f==1?c.mask:f==2?c.gateway:f==3?c.dns:c.active().host; }
void Setup::render() {
    if(page==Page::closed||page==Page::test) return;
    SoftMenu menu;
    int lit=-1;
    const auto server_label=std::string("SERVER ")+std::to_string(draft.server+1);
    if(page==Page::root) {
        menu.breadcrumb(""); menu.field(0,"NET"); menu.field(1,"SERVER"); menu.field(2,"CLOCK");menu.field(3,"INFO");menu.field(4,"TEST");
    } else if(page==Page::network) {
        menu.breadcrumb("NET");menu.field(0,"DHCP");
        if(draft.dhcp) lit=0;
        else {menu.field(1,"IP");menu.field(2,"MASK");menu.field(3,"GW");menu.field(4,"DNS");}
    } else if(page==Page::mixer) {
        menu.breadcrumb(server_label.c_str());menu.field(0,"PROTO");menu.field(1,"IP");menu.field(2,"PORT");char slot[7];std::snprintf(slot,sizeof slot,"SRV%u",(draft.server%3)+1);menu.field(3,slot);
    } else if(page==Page::protocol) {
        menu.breadcrumb(server_label.c_str(),"PROTOCOL");
        const char* labels[]={"VMIX","ATEM","OBS","MIDI","KAVTOR"};
        for(unsigned i=0;i<5;i++)menu.field(i,labels[i]);
        lit=int(draft.backend);
    } else if(page==Page::edit) {
        static const char* names[]={"IP ADDRESS","MASK","DEFAULT GATEWAY","DNS","IP","PORT"};
        menu.breadcrumb(field>=4?server_label.c_str():"NET",names[field]);
        for(unsigned i=0;i<(field==5?1u:4u);i++) {
            char number[7];std::snprintf(number,sizeof number,"%u",value(i));menu.field(i,number);
        }
        menu.field(5,"UNDO");lit=keypad?int(selected):-1;
    } else if(page==Page::clock) {
        menu.title(clock_text[0]?clock_text.data():"CLOCK: WAITING FOR HOST");
        menu.field(0,"NTP");menu.field(1,"NOT");menu.field(2,"SET UP");
    } else if(page==Page::info) {
        menu.breadcrumb("INFO");
    } else if(page==Page::server_setup) {
        std::string title=std::string(ProtocolLabels[unsigned(configuration.saved().backend)])+" SERVER SETUP ["+std::to_string(configuration.saved().server+1)+"]";
        menu.title(title.c_str());menu.field(5,"INFO");
        if(configuration.saved().backend==Backend::atem){MultiviewSettings settings;if(video&&video->multiview_settings(0,settings))menu.field(0,"MVIEW");OutputRoutes routes;if(video&&video->output_routes(routes))menu.field(1,"OUTPUT");VideoFormats formats;if(video&&video->video_formats(formats))menu.field(2,"FORMAT");}
        if(configuration.saved().backend==Backend::kavtor) {
            menu.field(0,"BUSES"); menu.field(1,"NAMES"); menu.field(2,"TIMERS"); menu.field(3,"SAFE"); menu.field(4,"METERS");
        }
    } else if(page==Page::server_info){
        std::string title=std::string(ProtocolLabels[unsigned(configuration.saved().backend)])+" SERVER SETUP ~ INFO ["+std::to_string(configuration.saved().server+1)+"]";menu.title(title.c_str());
    } else if(page==Page::atem_format){
        VideoFormats formats;const bool known=video&&video->video_formats(formats)&&format_index<formats.supported.size();
        menu.title(format_confirm?"CHANGE FORMAT? MEDIA POOL MAY CLEAR":"ATEM SETUP ~ VIDEO FORMAT");
        if(known){if(!format_confirm){std::string title=std::string("VIDEO FORMAT ~ ")+video_format_name(formats.supported[format_index]);menu.title(title.c_str());}menu.field(2,"FORMAT");menu.field(5,format_confirm?"CONFIRM":"APPLY");}
    } else if(page==Page::atem_outputs){
        OutputRoutes routes;const bool known=video&&video->output_routes(routes);char title[41],label[8];
        if(known&&routes.labels[output_index][0])std::snprintf(title,sizeof title,"ATEM SETUP ~ %s",routes.labels[output_index].data());else std::snprintf(title,sizeof title,"ATEM SETUP ~ OUTPUT %u",output_index+1);menu.title(known?title:"ATEM OUTPUTS OFFLINE");
        std::snprintf(label,sizeof label,"OUT%u",output_index+1);menu.field(0,label);
        if(known&&!routes.choices[output_index].empty())menu.field(1,"SOURCE");
        if(known)for(const auto& source:routes.choices[output_index])if(int(source.id)==routes.sources[output_index]){std::string text=std::string(title)+" ~ "+source.name.data();menu.title(text.c_str());}
    } else if(page==Page::atem_multiview){
        MultiviewSettings settings;const bool known=video&&video->multiview_settings(multiview_index,settings);
        char title[41],label[8];std::snprintf(title,sizeof title,"ATEM SETUP ~ MV%u ~ WINDOW %u",multiview_index+1,multiview_window+1);menu.title(known?title:"ATEM MULTIVIEW OFFLINE");
        std::snprintf(label,sizeof label,"MV%u",multiview_index+1);menu.field(0,label);
        std::snprintf(label,sizeof label,"WIN%u",multiview_window+1);menu.field(1,label);
        if(known&&settings.windows[multiview_window].safe>=0)menu.field(2,"SAFE");
        if(known&&settings.windows[multiview_window].meters>=0)menu.field(3,"METERS");
        if(known){bool safe=false,meters=false;for(const auto& w:settings.windows)if(multiview_input(w)){safe=safe||w.safe>=0;meters=meters||w.meters>=0;}if(safe)menu.field(4,"INSAFE");if(meters)menu.field(5,"IN VU");}
    } else if(page==Page::kavtor_buses) {
        menu.breadcrumb("KAVTOR","BUSES");
        menu.field(0,"LEFT"); menu.field(1,"RIGHT");
        lit=current_layout().program_left?0:1;
    } else if(page==Page::kavtor_names||page==Page::kavtor_clocks) {
        const bool names=page==Page::kavtor_names;
        menu.breadcrumb("KAVTOR",names?"NAMES":"TIMERS");
        menu.field(0,"TOP"); menu.field(1,"BOTTOM"); menu.field(2,"LEFT"); menu.field(3,"CENTER"); menu.field(4,"RIGHT");
    } else if(page==Page::kavtor_safe) {
        menu.breadcrumb("KAVTOR","SAFE");
        menu.field(0,"PVW"); menu.field(1,"PGM");
        const auto layout=current_layout();
        menu.field(2,safe_aspect_name(layout.safe_preview_aspect));
        menu.field(3,safe_aspect_name(layout.safe_program_aspect));
        menu.field(4,layout.safe_preset?"CLASSIC":"EBU"); menu.field(5,"ALL");
    } else if(page==Page::kavtor_meters) {
        menu.breadcrumb("KAVTOR","METERS");
        menu.field(0,"ON"); menu.field(1,"OFF");
        lit=current_layout().meters?0:1;
    }
    if(error) menu.title(error);
    menu.show(panel,lit);
    if(page==Page::kavtor_names||page==Page::kavtor_clocks) {
        const OverlayLayout layout=current_layout();
        const uint8_t edge=page==Page::kavtor_names?layout.name_edge:layout.clock_edge;
        const uint8_t align=page==Page::kavtor_names?layout.name_align:layout.clock_align;
        panel.led(160,edge==0?2:0); panel.led(161,edge==1?2:0);
        panel.led(162,align==0?2:0); panel.led(163,align==1?2:0); panel.led(164,align==2?2:0);
    } else if(page==Page::kavtor_safe) {
        const OverlayLayout layout=current_layout();
        panel.led(160,layout.safe_preview?2:1); panel.led(161,layout.safe_program?2:1);
        for(unsigned id=162;id<=165;++id)panel.led(id,1);
    }
    if(page==Page::atem_multiview){MultiviewSettings settings;if(video&&video->multiview_settings(multiview_index,settings)){panel.led(162,settings.windows[multiview_window].safe==1?2:settings.windows[multiview_window].safe==0?1:0);panel.led(163,settings.windows[multiview_window].meters==1?2:settings.windows[multiview_window].meters==0?1:0);for(unsigned option=0;option<2;option++){bool any=false,all=true;for(const auto& w:settings.windows)if(multiview_input(w)){const int value=option?w.meters:w.safe;if(value>=0){any=true;all=all&&value==1;}}panel.led(164+option,any?(all?2:1):0);}} }
    SoftMenu::navigation(panel,true,page==Page::atem_multiview,page==Page::atem_multiview);
    if(page==Page::server_info){ServerInfo info;if(video&&video->server_info(info)&&!info.fields.empty()){if(info_index>=info.fields.size())info_index=0;const auto& field=info.fields[info_index];const auto line=field.first+": "+field.second;panel.lcd(1,line.c_str());SoftMenu::navigation(panel,true,info_index>0,info_index+1<info.fields.size());}else panel.lcd(1,"NO SERVER INFORMATION");}
    if(page==Page::info) {
        std::string info=std::string(ProductName)+" "+HostVersion+"  FIRMWARE: "+std::string(firmware_version[0]?firmware_version.data():"N/A");
        panel.lcd(1,info.c_str());
    }
    if(page==Page::edit) panel.led(165,((keypad&&!replace_digit)||(field==5?draft.active().port!=original_port:address(draft,field)!=original_ip))?1:0);
}
unsigned Setup::value(unsigned slot) const {
    if(field==5) return draft.active().port;
    const IPv4& ip=field==0?draft.ip:field==1?draft.mask:field==2?draft.gateway:field==3?draft.dns:draft.active().host;
    return ip[slot];
}
void Setup::value(unsigned slot,unsigned v) {
    if(field==5) draft.active().port=uint16_t(v);
    else address(draft,field)[slot]=uint8_t(v);
}
void Setup::begin_edit(unsigned f) {
    field=f; original_port=draft.active().port; original_ip=address(draft,f);
    selected=0; keypad=false; replace_digit=true;numeric_invalid=false; page=Page::edit; render();
}
bool Setup::save() {
    const auto result=configuration.save(draft,draft_revision);
    if(result==SaveResult::saved||result==SaveResult::unchanged) {
        draft_revision=configuration.revision(); if(result==SaveResult::saved)apply_requested=true; return true;
    }
    panel.beep();
    conflict=result==SaveResult::conflict;
    error=result==SaveResult::invalid?"INVALID NETWORK: CHECK IP/MASK/GATEWAY":
          result==SaveResult::conflict?"CONFIG CHANGED - F6 RELOAD":"SAVE FAILED: CONFIG UNCHANGED";
    render(); return false;
}
void Setup::finish_edit(bool accept) {
    keypad=false;
    if(!accept) {
        if(field==5) draft.active().port=original_port;
        else address(draft,field)=original_ip;
    }
    // Accept subfields in one draft. Validate and persist the whole NETWORK or
    // MIXER group on EXIT from its parent, allowing IP/gateway changes together.
    if(field>=4&&!save()) return;
    page=field>=4?Page::mixer:Page::network; render();
}
bool Setup::kavtor_page() const {
    return page==Page::kavtor_buses||page==Page::kavtor_names||page==Page::kavtor_clocks||page==Page::kavtor_safe||page==Page::kavtor_meters;
}
OverlayLayout Setup::current_layout() const {
    OverlayLayout layout;
    if(video) video->overlay_layout(layout);
    return layout;
}
bool Setup::push_layout(OverlayLayout layout) {
    if(!video||!video->set_overlay_layout(layout)) { error="KAVTOR OFFLINE"; panel.beep(); render(); return false; }
    OverlayLayout applied; if(video->overlay_layout(applied)) shown_layout=applied.revision;
    error=nullptr; return true;
}
bool Setup::claims(unsigned id) const {
    if(!active()) return false;
    if(testing()) return control(id)!=nullptr;
    if(id==46||id==47||id==176||id==180||id==181||(id>=160&&id<=165)) return true;
    if(!keypad) return false;
    if(id==140||id==156) return true;
    for(unsigned digit:KeypadDigits) if(id==digit) return true;
    return false;
}
void Setup::adapter_changed() {
    if(draft.server!=configuration.saved().server){draft=configuration.saved();draft_revision=configuration.revision();keypad=false;panel.led(156,0);if(page==Page::edit||page==Page::protocol)page=Page::mixer;}
    shown_formats=shown_routes=shown_layout=shown_multiview=0;format_confirm=false;output_index=multiview_index=multiview_window=0;
    if(kavtor_page()||page==Page::atem_multiview||page==Page::atem_outputs||page==Page::atem_format||page==Page::server_info) page=Page::server_setup;
    if(active()) {panel.led(page==Page::server_setup?46:47,1);render();}
}
void Setup::sync_overlay() {
    if(video&&configuration.saved().backend==Backend::atem&&(page==Page::server_setup||page==Page::atem_format)){VideoFormats formats;if(video->video_formats(formats)&&formats.revision!=shown_formats){shown_formats=formats.revision;if(format_index>=formats.supported.size())format_index=0;format_confirm=false;render();}}
    if(video&&page==Page::atem_outputs){OutputRoutes routes;if(video->output_routes(routes)&&routes.revision!=shown_routes){shown_routes=routes.revision;render();}return;}
    if(video&&configuration.saved().backend==Backend::atem&&(page==Page::server_setup||page==Page::atem_multiview)){
        MultiviewSettings settings;if(video->multiview_settings(multiview_index,settings)&&settings.revision!=shown_multiview){shown_multiview=settings.revision;render();}return;
    }
    if(!video||!kavtor_page()) return;
    OverlayLayout layout;
    if(!video->overlay_layout(layout)||layout.revision==shown_layout) return;
    shown_layout=layout.revision; render();
}
void Setup::cancel() {
    diagnostic.stop();
    keypad=false;replace_digit=true;numeric_invalid=false;panel.led(156,0); page=Page::closed; panel.led(47,0);
    for(unsigned i=0;i<6;i++) panel.led(160+i,0);
    panel.led(46,0);SoftMenu::navigation(panel,false);
}
void Setup::refresh_keypad() {
    if(!keypad) return;
    panel.clear_keypad_lamps();
    uint8_t digits[7]={15,15,15,15,15,15,0};
    unsigned v=keypad_value;
    for(unsigned i=0;i<6;i++) { digits[i]=uint8_t(v%10); v/=10; if(!v) break; }
    panel.digits(digits);
    const bool draft=!replace_digit;
    panel.led(156,draft?1:0,draft?500:0);
}
void Setup::firmware_info(const char* version) {
    std::snprintf(firmware_version.data(),firmware_version.size(),"%s",version);
    if(page==Page::info)render();
}
void Setup::host_clock(const char* utc) {
    std::snprintf(clock_text.data(),clock_text.size(),"%s UTC",utc);
    if(page==Page::clock||page==Page::server_info) render();
}
void Setup::move_multiview(int viewer,int window){
    MultiviewSettings settings;if(!video||!video->multiview_settings(multiview_index,settings))return;
    if(viewer){multiview_index=unsigned((int64_t(multiview_index)+viewer%int(settings.count)+settings.count)%settings.count);multiview_window=0;if(!video->multiview_settings(multiview_index,settings))return;}
    if(window||!settings.windows[multiview_window].present){
        std::array<unsigned,16> choices{};unsigned count=0;for(unsigned i=0;i<16;i++)if(settings.windows[i].present)choices[count++]=i;
        if(count){unsigned current=0;for(unsigned i=0;i<count;i++)if(choices[i]==multiview_window)current=i;multiview_window=choices[unsigned((int64_t(current)+window%int(count)+count)%count)];}
    }
    error=nullptr;render();
}
void Setup::move_output(int delta){OutputRoutes routes;if(!video||!video->output_routes(routes)||output_index>=routes.count)return;const auto& choices=routes.choices[output_index];if(choices.empty())return;int current=-1;for(unsigned i=0;i<choices.size();i++)if(int(choices[i].id)==routes.sources[output_index])current=int(i);const int64_t count=int64_t(choices.size());const unsigned next=unsigned((int64_t(current)+delta%count+count)%count);if(!video->set_output_source(output_index,choices[next].id)){error="OUTPUT SOURCE UNAVAILABLE";panel.beep();}else error=nullptr;render();}
void Setup::rotate(const std::array<int32_t,6>& delta) {
    if(page==Page::atem_outputs){if(delta[1])move_output(delta[1]);return;}
    if(page==Page::atem_format){
        VideoFormats formats;if(delta[2]&&video&&video->video_formats(formats)&&!formats.supported.empty()){
            const int64_t count=int64_t(formats.supported.size());
            format_index=unsigned((int64_t(format_index)+delta[2]%count+count)%count);
            format_confirm=false;format_previous=formats.current;error=nullptr;render();
        }
        return;
    }
    if(page==Page::atem_multiview){if(delta[0]||delta[1])move_multiview(delta[0],delta[1]);return;}
    if(page!=Page::edit) return;
    bool moved=false;
    for(unsigned i=0;i<(field==5?1u:4u);i++)moved=moved||delta[i]!=0;
    if(moved&&keypad) {keypad_value=value(selected);replace_digit=true;numeric_invalid=false;panel.led(156,0);}
    bool changed=false;
    for(unsigned i=0;i<(field==5?1u:4u);i++) if(delta[i]) {
        const int64_t next=int64_t(value(i))+delta[i];
        value(i,unsigned(std::max<int64_t>(0,std::min<int64_t>(field==5?65535:255,next))));
        if(keypad&&selected==i) { keypad_value=value(i); replace_digit=true;numeric_invalid=false; } changed=true;
    }
    if(changed) { error=nullptr; if(field>=4&&!save())return; render(); }
}
void Setup::press(const std::bitset<KeyCount>& keys) {
    if(keys.count()!=1) return;
    unsigned id=0; while(!keys[id]) id++;
    const bool toggle_field=keypad&&id==160+selected;
    if(keypad&&!keypad_edit_input(keys)) {
        keypad=false;replace_digit=true;numeric_invalid=false;keypad_value=value(selected);panel.led(156,0);
        if(toggle_field||id==176) {render();return;}
    }
    if(conflict) {
        if(id==165) { draft=configuration.saved(); draft_revision=configuration.revision(); conflict=false; error=nullptr; render(); }
        return;
    }
    error=nullptr;
    if(id==46) {
        if(page==Page::server_setup||kavtor_page()||page==Page::atem_multiview||page==Page::atem_outputs||page==Page::atem_format||page==Page::server_info) cancel();
        else if(page==Page::closed||save()) {draft=configuration.saved();draft_revision=configuration.revision();page=Page::server_setup;panel.led(47,0);panel.led(46,1);render();}
        return;
    }
    if(id==47) {
        if(page!=Page::closed&&page!=Page::server_setup&&page!=Page::server_info) { if(save()) cancel(); }
        else { draft=configuration.saved(); draft_revision=configuration.revision(); page=Page::root; panel.led(46,0); panel.led(47,1); render(); }
        return;
    }
    if(page==Page::closed) return;
    if(id==176) {
        if(keypad) { keypad=false; render(); return; }
        if(page==Page::root||page==Page::server_setup) cancel();
        else if(kavtor_page()||page==Page::atem_multiview||page==Page::atem_outputs||page==Page::atem_format||page==Page::server_info) { page=Page::server_setup; render(); return; }
        else if(page==Page::edit) finish_edit(true);
        else if(page==Page::protocol) { if(save()) {page=Page::mixer;render();} }
        else if(save()) { page=Page::root; render(); }
        return;
    }
    if(page==Page::root) {
        if(id==160||id==161) {draft=configuration.saved();draft_revision=configuration.revision();page=id==160?Page::network:Page::mixer;}
        if(id==162)page=Page::clock;
        if(id==163)page=Page::info;
        if(id==164){page=Page::test;diagnostic.start();diagnostic.tick(100);return;}
    } else if(page==Page::protocol) {
        if(id>=160&&id<=164) {draft.backend=Backend(id-160);save();}
    } else if(page==Page::mixer) {
        if(id==163){draft.select_server((draft.server+1)%3);save();}
        if(id==160)page=Page::protocol;
        if(id==161||id==162) {begin_edit(id==161?4:5);return;}
    } else if(page==Page::network) {
        if(id==160)draft.dhcp=!draft.dhcp;
        if(!draft.dhcp&&id>=161&&id<=164) {begin_edit(id-161);return;}
    } else if(page==Page::server_setup&&id==165){info_index=0;page=Page::server_info;
    } else if(page==Page::server_info){ServerInfo info;if(video&&video->server_info(info)&&!info.fields.empty()){if(id==181&&info_index+1<info.fields.size())++info_index;if(id==180&&info_index)--info_index;}
    } else if(page==Page::server_setup&&configuration.saved().backend==Backend::atem){
        VideoFormats formats;if(id==162&&video&&video->video_formats(formats)){format_index=0;for(unsigned i=0;i<formats.supported.size();i++)if(int(formats.supported[i])==formats.current)format_index=i;format_previous=formats.current;format_confirm=false;page=Page::atem_format;render();return;}
        OutputRoutes routes;if(id==161&&video&&video->output_routes(routes)){output_index=0;page=Page::atem_outputs;render();return;}
        MultiviewSettings settings;if(id==160&&video&&video->multiview_settings(0,settings)){multiview_index=multiview_window=0;page=Page::atem_multiview;move_multiview(0,0);return;}
    } else if(page==Page::atem_format){
        VideoFormats formats;if(video&&video->video_formats(formats)&&!formats.supported.empty()){
            if(id==162){format_index=(format_index+1)%formats.supported.size();format_confirm=false;format_previous=formats.current;}
            else if(id==165){if(!format_confirm){format_confirm=true;format_previous=formats.current;}else{if(format_index>=formats.supported.size()||!video->set_video_format(formats.supported[format_index],format_previous)){error="FORMAT CHANGE UNAVAILABLE";panel.beep();}format_confirm=false;}}
        }
    } else if(page==Page::atem_outputs){
        OutputRoutes routes;if(video&&video->output_routes(routes)){
            if(id==160)output_index=(output_index+1)%routes.count;
            else if(id==161){move_output(1);return;}
        }
    } else if(page==Page::atem_multiview){
        if(id==160){move_multiview(1,0);return;}if(id==161||id==181||id==180){move_multiview(0,id==180?-1:1);return;}
        if(id==164||id==165){MultiviewSettings settings;if(video&&video->multiview_settings(multiview_index,settings)){bool any=false,all=true;for(const auto& w:settings.windows)if(multiview_input(w)){const int value=id==164?w.safe:w.meters;if(value>=0){any=true;all=all&&value==1;}}if(!any||!video->set_multiview_inputs(multiview_index,id==164,!all)){error="INPUT DISPLAY OPTION UNAVAILABLE";panel.beep();}}}
        if(id==162||id==163){MultiviewSettings settings;if(video&&video->multiview_settings(multiview_index,settings)){const auto& w=settings.windows[multiview_window];const int value=id==162?w.safe:w.meters;if(value<0||!video->set_multiview_window(multiview_index,multiview_window,id==162,value==0)){error="MULTIVIEW OPTION UNAVAILABLE";panel.beep();}}}
    } else if(page==Page::server_setup&&configuration.saved().backend==Backend::kavtor) {
        if(id==160) page=Page::kavtor_buses;
        else if(id==161) page=Page::kavtor_names;
        else if(id==162) page=Page::kavtor_clocks;
        else if(id==163) page=Page::kavtor_safe;
        else if(id==164) page=Page::kavtor_meters;
    } else if(page==Page::kavtor_buses) {
        OverlayLayout layout=current_layout();
        if(id==160||id==161) { layout.program_left=id==160; if(!push_layout(layout)) return; }
    } else if(page==Page::kavtor_names||page==Page::kavtor_clocks) {
        OverlayLayout layout=current_layout();
        uint8_t& edge=page==Page::kavtor_names?layout.name_edge:layout.clock_edge;
        uint8_t& align=page==Page::kavtor_names?layout.name_align:layout.clock_align;
        bool changed=true;
        if(id==160) edge=0; else if(id==161) edge=1;
        else if(id==162) align=0; else if(id==163) align=1; else if(id==164) align=2;
        else changed=false;
        if(changed&&!push_layout(layout)) return;
    } else if(page==Page::kavtor_safe) {
        OverlayLayout layout=current_layout();
        if(id==160) layout.safe_preview=!layout.safe_preview;
        else if(id==161) layout.safe_program=!layout.safe_program;
        else if(id==162) layout.safe_preview_aspect=(layout.safe_preview_aspect+1)%6;
        else if(id==163) layout.safe_program_aspect=(layout.safe_program_aspect+1)%6;
        else if(id==164) layout.safe_preset=layout.safe_preset?0:1;
        else if(id==165){layout.safe_program_aspect=layout.safe_preview_aspect;layout.safe_preview=layout.safe_program=true;}
        else { render(); return; }
        if(!push_layout(layout)) return;
    } else if(page==Page::kavtor_meters) {
        OverlayLayout layout=current_layout();
        if(id==160) layout.meters=true;
        else if(id==161) layout.meters=false;
        else { render(); return; }
        if(!push_layout(layout)) return;
    } else if(page==Page::edit) {
        if(id==165) {
            if(field==5) draft.active().port=original_port; else address(draft,field)=original_ip;
            keypad=false; replace_digit=true;numeric_invalid=false;
            if(field>=4&&!save())return;
        } else if(id>=160&&id<160+(field==5?1u:4u)) {
            if(keypad&&selected==id-160) keypad=false;
            else { selected=id-160; keypad_value=value(selected); keypad=true; replace_digit=true;numeric_invalid=false; }
        }
        else if(keypad&&id==156) { if(numeric_invalid){error=field==5?"PORT RANGE: 0..65535":"OCTET RANGE: 0..255";panel.beep();render();return;}value(selected,keypad_value); keypad=false; replace_digit=true;numeric_invalid=false; if(field>=4&&!save())return; }
        else if(keypad&&id==140) { keypad_value=0; replace_digit=false;numeric_invalid=false; }
        else if(keypad) {
            static const unsigned ids[]={153,145,146,147,137,138,139,129,130,131};
            for(unsigned i=0;i<10;i++) if(id==ids[i]) {
                unsigned v=(replace_digit||numeric_invalid?0:keypad_value)*10+i;
                if(v>999999){numeric_invalid=true;error="MAXIMUM SIX DIGITS";panel.beep();continue;}
                keypad_value=v;replace_digit=false;
                if(v>(field==5?65535u:255u)) {numeric_invalid=true;error=field==5?"PORT RANGE: 0..65535":"OCTET RANGE: 0..255";panel.beep();}
                else { numeric_invalid=false; }
            }
        }
    }
    render();
}
}
