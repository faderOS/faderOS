#include "vmix_posix.hpp"
#include <nlohmann/json.hpp>
#include <libxml/parser.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>
#include <cstdio>
#include <cerrno>
#include <stdexcept>
using Json=nlohmann::json;
namespace bkds::link {
namespace {
using Clock=std::chrono::steady_clock;
struct Input {int number=0;std::string key,title;};
std::string attribute(xmlNode* n,const char* key) {auto* p=xmlGetProp(n,BAD_CAST key);std::string s=p?reinterpret_cast<char*>(p):"";xmlFree(p);return s;}
std::string content(xmlNode* n) {auto* p=xmlNodeGetContent(n);std::string s=p?reinterpret_cast<char*>(p):"";xmlFree(p);return s;}
std::string encode(const std::string& s) {std::string out;for(unsigned char c:s){if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-')out+=char(c);else{char b[4];std::snprintf(b,sizeof b,"%%%02X",c);out+=b;}}return out;}
struct Failure:std::runtime_error {using std::runtime_error::runtime_error;};
}
struct VmixAdapter::Impl {
    std::mutex mutex; std::atomic<bool> stop{false};std::thread worker;
    std::string software_version;
    Config config;unsigned generation=0;bool enabled=false;
    MixerState view;std::vector<Input> inputs;std::array<std::string,24> mappings{};
    std::string transition_one;
    std::array<int,8> overlay_numbers{{-1,-1,-1,-1,-1,-1,-1,-1}};
    unsigned overlay_layer=0;int overlay_expected=-1;TallyBus overlay_destination=TallyBus::none;
    bool manual_active=false,manual_started=false,manual_terminal=false;unsigned manual_position=0;int manual_sent=-1,manual_pg=-1,manual_pv=-1;std::string manual_effect,manual_restore;
    std::string path;unsigned revision=0;bool mapping_error=false;
    Json transitions=default_transitions();
    MixerAction command=MixerAction::none;std::string target,function;uint32_t duration=0;
    int fd=-1;std::string buffer,tally_flags;bool tally_dirty=false;
    bool mixing=false;Clock::time_point mix_end;std::string mix_target;
    int preview_number=-1,program_number=-1;
    explicit Impl(std::string p):path(std::move(p)) {
        std::ifstream in(path);
        if(in) {try {Json j;in>>j;mappings=parse_map(j);transitions=parse_transitions(j);}catch(...){mapping_error=true;}}
        worker=std::thread([this]{run();});
    }
    ~Impl(){cancel_and_wait();stop=true;worker.join();close_socket();}
    static std::array<std::string,24> parse_map(const Json& j) {
        if(!j.is_object()||j.at("version")!=1||!j.at("sources").is_array()||j.at("sources").size()!=24)throw Failure("Invalid mapping profile");
        std::array<std::string,24> m;
        for(unsigned i=0;i<24;i++) {const auto& v=j.at("sources").at(i);if(!v.is_string())throw Failure("Invalid input key");m[i]=v.get<std::string>();if(m[i].size()>128||m[i].find_first_of("\r\n")!=std::string::npos||m[i].find('\0')!=std::string::npos)throw Failure("Invalid input key");
            if(!m[i].empty())for(unsigned k=0;k<i;k++)if(m[k]==m[i])throw Failure("Input assigned more than once");}
        return m;
    }
    static Json default_transitions() {
        Json dme=Json::array();for(const auto& name:{"Merge","Zoom","Slide","Fly","FlyRotate","Cube","CubeZoom","VerticalSlide"})dme.push_back({{"normal",name},{"reverse",std::string(name)=="Slide"?"SlideReverse":std::string(name)=="VerticalSlide"?"VerticalSlideReverse":""}});
        Json stingers=Json::array();for(unsigned i=1;i<=8;i++)stingers.push_back({{"function","Stinger"+std::to_string(i)},{"total_ms",1000}});
        return Json{{"version",3},{"mix_types",{"Fade","AlphaFade","CrossZoom"}},{"dme_types",dme},{"wipes",{{"1",{{"normal","Wipe"},{"reverse","WipeReverse"}}},{"3",{{"normal","VerticalWipe"},{"reverse","VerticalWipeReverse"}}},{"17",{{"normal","BarnDoor"},{"reverse",""}}},{"18",{{"normal","RollerDoor"},{"reverse",""}}}}},{"stingers",stingers}};
    }
    static bool valid_function(const std::string& f) {static const std::vector<std::string> names={"","Fade","Wipe","Slide","Fly","Merge","Zoom","CrossZoom","FlyRotate","Cube","CubeZoom","VerticalWipe","VerticalSlide","WipeReverse","SlideReverse","VerticalWipeReverse","VerticalSlideReverse","BarnDoor","RollerDoor","AlphaFade"};return std::find(names.begin(),names.end(),f)!=names.end();}
    static Json parse_transitions(const Json& profile) {
        Json t=profile.value("transitions",default_transitions());
        if(t.is_object()&&!t.contains("version")) {
            if(t.size()!=5)throw Failure("Invalid legacy transitions");
            auto legacy=t;t=default_transitions();t["mix_types"][0]=legacy.at("mix");
            if(legacy.at("dme")!="Merge")t["dme_types"][1]["normal"]=legacy.at("dme");
            t["dme_types"][2]["normal"]=legacy.at("slide");t["dme_types"][3]["normal"]=legacy.at("swipe");
            if(!legacy.at("wipes").is_object())throw Failure("Invalid wipes");
            t["wipes"].update(legacy.at("wipes"));
        }
        if(t.is_object()&&t.value("version",0)==2) {
            auto& mix=t.at("mix_types");auto& dme=t.at("dme_types");
            if(!mix.is_array()||mix.size()!=4||!dme.is_array()||dme.size()!=7)throw Failure("Invalid old slots");
            Json moved={{"normal",mix[1]},{"reverse",""}};mix.erase(mix.begin()+1);dme.insert(dme.begin(),moved);t["version"]=3;
        }
        if(!t.is_object()||t.size()!=5||t.at("version")!=3)throw Failure("Invalid transitions");
        auto validate=[](const Json& v){if(!v.is_string()||!valid_function(v.get<std::string>()))throw Failure("Invalid transition function");};
        auto binding=[&](const Json& v){if(!v.is_object()||v.size()!=2)throw Failure("Invalid binding");validate(v.at("normal"));validate(v.at("reverse"));};
        const auto& mix=t.at("mix_types");if(!mix.is_array()||mix.size()!=3)throw Failure("Invalid mix slots");for(const auto& v:mix)validate(v);
        const auto& dme=t.at("dme_types");if(!dme.is_array()||dme.size()!=8)throw Failure("Invalid DME slots");for(const auto& v:dme)binding(v);
        const auto& wipes=t.at("wipes");if(!wipes.is_object()||wipes.size()>32)throw Failure("Invalid wipe mappings");
        for(auto i=wipes.begin();i!=wipes.end();++i){auto k=i.key();if(k.empty()||k.size()>3||k.find_first_not_of("0123456789")!=std::string::npos||std::to_string(std::stoi(k))!=k||k=="0")throw Failure("Invalid wipe code");binding(i.value());}
        const auto& st=t.at("stingers");if(!st.is_array()||st.size()!=8)throw Failure("Invalid stinger slots");for(const auto& v:st){if(!v.is_object()||v.size()!=2||!v.at("function").is_string())throw Failure("Invalid stinger");auto name=v.at("function").get<std::string>();if(!name.empty()&&(name.size()!=8||name.rfind("Stinger",0)!=0||name[7]<'1'||name[7]>'8'))throw Failure("Invalid stinger function");if(!v.at("total_ms").is_number_integer()||v.at("total_ms").get<uint64_t>()<50||v.at("total_ms").get<uint64_t>()>60000)throw Failure("Invalid stinger duration");}
        return t;
    }
    void close_socket(){if(fd>=0){::close(fd);fd=-1;}}
    void pause(unsigned ms){for(unsigned n=0;n<ms&&!stop;n+=20)std::this_thread::sleep_for(std::chrono::milliseconds(20));}
    void check(unsigned gen) {std::lock_guard<std::mutex> lock(mutex);if(stop||!enabled||gen!=generation)throw Failure("VMIX SESSION CHANGED");}
    void status(const char* text,unsigned gen) {std::lock_guard<std::mutex> lock(mutex);if(gen!=generation)return;view={};manual_active=manual_started=manual_terminal=false;command=MixerAction::none;std::snprintf(view.message.data(),view.message.size(),"%s",text);}
    void connect_to(const Endpoint& e,unsigned gen) {
        fd=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);if(fd<0)throw Failure("VMIX SOCKET FAILED");
        if(fcntl(fd,F_SETFL,O_NONBLOCK)<0)throw Failure("VMIX SOCKET FLAGS FAILED");
        sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(e.port?e.port:8099);
        a.sin_addr.s_addr=htonl((uint32_t(e.host[0])<<24)|(uint32_t(e.host[1])<<16)|(uint32_t(e.host[2])<<8)|e.host[3]);
        if(connect(fd,reinterpret_cast<sockaddr*>(&a),sizeof a)<0&&errno!=EINPROGRESS)throw Failure("VMIX CONNECTION FAILED");
        auto deadline=Clock::now()+std::chrono::seconds(2);
        for(;;){check(gen);pollfd p{fd,POLLOUT,0};if(poll(&p,1,20)>0){int error=0;socklen_t size=sizeof error;if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size)<0||error)throw Failure("VMIX CONNECTION FAILED");break;}if(Clock::now()>deadline)throw Failure("VMIX CONNECT TIMEOUT");}
    }
    void send(const std::string& s,unsigned gen) {
        size_t at=0;auto deadline=Clock::now()+std::chrono::seconds(2);
        while(at<s.size()){check(gen);auto n=::send(fd,s.data()+at,s.size()-at,MSG_NOSIGNAL);if(n>0){at+=size_t(n);continue;}if(n<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK)){pollfd p{fd,POLLOUT,0};poll(&p,1,20);if(Clock::now()<deadline)continue;}throw Failure("VMIX SEND FAILED");}
    }
    void receive(unsigned gen,Clock::time_point deadline) {
        for(;;){check(gen);if(Clock::now()>deadline)throw Failure("VMIX RESPONSE TIMEOUT");pollfd p{fd,POLLIN,0};int r=poll(&p,1,20);if(r<0&&errno==EINTR)continue;if(r<0||(p.revents&(POLLERR|POLLNVAL)))throw Failure("VMIX DISCONNECTED");if(!r)continue;
            char data[8192];auto n=recv(fd,data,sizeof data,0);if(n<0&&(errno==EINTR||errno==EAGAIN))continue;if(n<=0)throw Failure("VMIX DISCONNECTED");buffer.append(data,size_t(n));if(buffer.size()>4*1024*1024)throw Failure("VMIX RESPONSE TOO LARGE");return;}
    }
    std::string line(unsigned gen,Clock::time_point deadline) {for(;;){auto end=buffer.find("\r\n");if(end!=std::string::npos){auto s=buffer.substr(0,end);buffer.erase(0,end+2);return s;}receive(gen,deadline);}}
    // One outstanding request. TALLY events may precede any command response.
    std::string rpc(const std::string& cmd,const std::string& verb,unsigned gen) {
        send(cmd+"\r\n",gen);auto deadline=Clock::now()+std::chrono::seconds(2);
        for(;;){auto s=line(gen,deadline);if(s.rfind("TALLY OK ",0)==0){tally_flags=s.substr(9);tally_dirty=true;if(verb!="TALLY")continue;}
            if(s.rfind(verb+" ",0)!=0)throw Failure("VMIX UNEXPECTED RESPONSE");
            const auto tail=s.substr(verb.size()+1);
            if(tail.rfind("ER",0)==0)return s;
            if(tail.rfind("OK",0)==0)return s;
            if(tail.empty()||tail.find_first_not_of("0123456789")!=std::string::npos)throw Failure("VMIX INVALID LENGTH");
            auto size=std::stoul(tail);if(size>4*1024*1024)throw Failure("VMIX XML TOO LARGE");while(buffer.size()<size)receive(gen,deadline);
            auto data=buffer.substr(0,size);buffer.erase(0,size);return data;}
    }
    void refresh(unsigned gen) {
        auto text=rpc("XML","XML",gen);
        xmlDoc* raw=xmlReadMemory(text.data(),int(text.size()),"vmix.xml",nullptr,XML_PARSE_NONET|XML_PARSE_NOERROR|XML_PARSE_NOWARNING);
        if(!raw)throw Failure("VMIX INVALID XML");
        std::unique_ptr<xmlDoc,decltype(&xmlFreeDoc)> doc(raw,xmlFreeDoc);
        auto* root=xmlDocGetRootElement(raw);if(!root||xmlStrcmp(root->name,BAD_CAST "vmix")||raw->intSubset||raw->extSubset)throw Failure("VMIX INVALID STATE");
        std::array<int,8> overlay_ids{{-1,-1,-1,-1,-1,-1,-1,-1}};
        std::array<OverlayChannelState,8> overlays{};unsigned overlay_count=0;
        std::string edition;
        std::string server_version,first_effect;std::vector<Input> list;int pv=-1,pg=-1;bool ftb=false,ftb_known=false;
        for(auto* n=root->children;n;n=n->next) {
            if(!xmlStrcmp(n->name,BAD_CAST "edition"))edition=content(n);
            if(!xmlStrcmp(n->name,BAD_CAST "overlays"))for(auto* i=n->children;i;i=i->next)if(!xmlStrcmp(i->name,BAD_CAST "overlay")) {
                const int number=std::stoi(attribute(i,"number"));
                if(number<1||number>8)continue; // first eight are the supported primary channels
                auto& o=overlays[size_t(number-1)];if(o.known)throw Failure("VMIX DUPLICATE OVERLAY");o.known=true;
                overlay_count=std::max(overlay_count,unsigned(number));
                const auto value=content(i),preview=attribute(i,"preview");
                if(!value.empty()) {
                    const int source=std::stoi(value);if(source<1||(preview!=""&&preview!="False"&&preview!="True"))throw Failure("VMIX INVALID OVERLAY STATE");
                    overlay_ids[size_t(number-1)]=source;o.bus=preview=="True"?TallyBus::preview:TallyBus::program;
                }
            }
            if(!xmlStrcmp(n->name,BAD_CAST "fadeToBlack")){auto v=content(n);if(v!="True"&&v!="False")throw Failure("VMIX INVALID FTB STATE");ftb=v=="True";ftb_known=true;}
            if(!xmlStrcmp(n->name,BAD_CAST "transitions"))for(auto* i=n->children;i;i=i->next)if(!xmlStrcmp(i->name,BAD_CAST "transition")&&attribute(i,"number")=="1")first_effect=attribute(i,"effect");
            if(!xmlStrcmp(n->name,BAD_CAST "version"))server_version=content(n);
            if(!xmlStrcmp(n->name,BAD_CAST "preview"))pv=std::stoi(content(n));
            if(!xmlStrcmp(n->name,BAD_CAST "active"))pg=std::stoi(content(n));
            if(!xmlStrcmp(n->name,BAD_CAST "inputs"))for(auto* i=n->children;i;i=i->next)if(!xmlStrcmp(i->name,BAD_CAST "input")){
                if(list.size()>=1000)throw Failure("VMIX TOO MANY INPUTS");
                Input input{std::stoi(attribute(i,"number")),attribute(i,"key"),attribute(i,"title")};
                if(input.number<1||input.key.empty())throw Failure("VMIX INVALID INPUT");
                list.push_back(std::move(input));
            }
        }
        if(pv<0||pg<0)throw Failure("VMIX MISSING BUSES");
        std::lock_guard<std::mutex> lock(mutex);if(gen!=generation)return;
        if(edition=="BasicHD"||edition=="Basic HD"||edition=="Basic")overlay_count=std::min(overlay_count,1u);
        overlay_numbers=overlay_ids;view.overlays=overlays;view.overlay_channels=overlay_count;
        for(unsigned layer=0;layer<overlay_count;layer++)for(const auto& input:list)if(input.number==overlay_ids[layer])for(unsigned slot=0;slot<24;slot++)if(!mapping_error&&!mappings[slot].empty()&&mappings[slot]==input.key){view.overlays[layer].source=int(slot);break;}
        transition_one=first_effect;view.ftb=ftb;view.ftb_known=ftb_known;software_version=server_version;inputs=std::move(list);preview_number=pv;program_number=pg;
        view.available.reset();view.preview=view.program=-1;view.sources=unsigned(inputs.size());
        std::string program_key;
        for(const auto& i:inputs) {if(i.number==pg)program_key=i.key;for(unsigned slot=0;slot<24;slot++)if(!mapping_error&&!mappings[slot].empty()&&mappings[slot]==i.key){view.available.set(slot);if(i.number==pv)view.preview=int(slot);if(i.number==pg)view.program=int(slot);}}
        if(mixing&&Clock::now()>=mix_end) {
            if(program_key==mix_target){mixing=false;++view.completed_auto;view.busy=false;}
            else if(Clock::now()>mix_end+std::chrono::seconds(2))throw Failure("VMIX MIX NOT CONFIRMED");
        }
        view.connected=true;view.studio=true;view.transitioning=mixing||manual_active;view.manual_transition=manual_active;view.both_sources=mixing||(manual_active&&manual_position>0)||(pv>0&&size_t(pv)<=tally_flags.size()&&tally_flags[size_t(pv-1)]=='1');
        std::snprintf(view.message.data(),view.message.size(),"%s",mapping_error?"VMIX INVALID MAPPING FILE":"VMIX CONNECTED");
    }
    std::string effect(TransitionType t,uint32_t code,bool reverse) const {
        if(t==TransitionType::wipe){auto i=transitions["wipes"].find(std::to_string(code));return i==transitions["wipes"].end()?"":i.value()[reverse?"reverse":"normal"].get<std::string>();}
        if(t==TransitionType::mix)return code<=3?transitions["mix_types"][code?code-1:0].get<std::string>():"";
        unsigned slot=t==TransitionType::slide?2:t==TransitionType::swipe?3:t==TransitionType::dme?code:99;
        return slot<=7?transitions["dme_types"][slot][reverse?"reverse":"normal"].get<std::string>():"";
    }
    void cancel_and_wait() {
        {std::lock_guard<std::mutex> lock(mutex);if(manual_active&&!manual_terminal){manual_position=0;manual_terminal=true;}}
        auto until=Clock::now()+std::chrono::milliseconds(2500);
        while(Clock::now()<until){{std::lock_guard<std::mutex> lock(mutex);if(!manual_active)return;}std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    }
    void confirm_manual_effect(const std::string& name,unsigned gen) {
        const auto until=Clock::now()+std::chrono::seconds(2);
        for(;;) {
            refresh(gen);
            {std::lock_guard<std::mutex> lock(mutex);if(transition_one==name)return;}
            if(Clock::now()>until)throw Failure("VMIX MANUAL EFFECT NOT CONFIRMED");
            pause(10);
        }
    }
    void restore_manual_effect(unsigned gen) {
        std::string current,original,pinned;{std::lock_guard<std::mutex> lock(mutex);current=transition_one;original=manual_restore;pinned=manual_effect;}
        // Preserve a concurrent change made by the operator in the vMix UI.
        if(current==pinned&&!original.empty()&&original!=pinned) {
            if(rpc("FUNCTION SetTransitionEffect1 Value="+encode(original),"FUNCTION",gen).rfind("FUNCTION ER",0)==0)throw Failure("VMIX EFFECT RESTORE REJECTED");
            // Do not expose an idle T-bar while restoration is still queued.
            confirm_manual_effect(original,gen);
        }
    }
    void pump_manual(unsigned gen) {
        unsigned pos;bool started;std::string name;int sent,old_pg,old_pv;
        {std::lock_guard<std::mutex> lock(mutex);if(!manual_active)return;started=manual_started;name=manual_effect;pos=manual_position;sent=manual_sent;old_pg=manual_pg;old_pv=manual_pv;}
        if(!started) {
            // Capture the live preference, rather than an old periodic snapshot.
            refresh(gen);
            {std::lock_guard<std::mutex> lock(mutex);manual_restore=transition_one;}
            if(rpc("FUNCTION SetTransitionEffect1 Value="+encode(name),"FUNCTION",gen).rfind("FUNCTION ER",0)==0)throw Failure("VMIX MANUAL EFFECT REJECTED");
            // FUNCTION OK alone does not establish the active effect in readback.
            // Moving the fader before readback can run the previous effect.
            confirm_manual_effect(name,gen);
            std::lock_guard<std::mutex> lock(mutex);manual_started=true;pos=manual_position;
        }
        if(started&&int(pos)==sent)return;
        if(rpc("FUNCTION SetFader Value="+std::to_string(pos),"FUNCTION",gen).rfind("FUNCTION ER",0)==0)throw Failure("VMIX FADER REJECTED");
        {std::lock_guard<std::mutex> lock(mutex);manual_sent=int(pos);}
        if(pos!=0&&pos!=255){refresh(gen);return;}
        auto until=Clock::now()+std::chrono::seconds(2);
        for(;;){refresh(gen);bool done;{std::lock_guard<std::mutex> lock(mutex);done=pos==255?program_number==old_pv&&preview_number==old_pg:program_number==old_pg&&preview_number==old_pv;}
            if(done)break;
            if(Clock::now()>until)throw Failure("VMIX FADER NOT CONFIRMED");
            pause(10);}
        restore_manual_effect(gen);
        {std::lock_guard<std::mutex> lock(mutex);manual_active=manual_started=manual_terminal=false;manual_sent=-1;view.busy=view.transitioning=view.manual_transition=false;if(pos==255)++view.completed_auto;}
        refresh(gen);
    }
    void run() {
        while(!stop) {
            Config c;unsigned gen;bool on;{std::lock_guard<std::mutex> lock(mutex);c=config;gen=generation;on=enabled;}
            if(!on){pause(20);continue;}
            try {
                status("VMIX CONNECTING",gen);connect_to(c.active(),gen);buffer.clear();tally_flags.clear();mixing=false;tally_dirty=false;
                if(line(gen,Clock::now()+std::chrono::seconds(2)).rfind("VERSION OK ",0)!=0)throw Failure("VMIX INVALID GREETING");
                if(rpc("SUBSCRIBE TALLY","SUBSCRIBE",gen).rfind("SUBSCRIBE OK",0)!=0)throw Failure("VMIX TALLY SUBSCRIPTION REJECTED");
                refresh(gen);auto refreshed=Clock::now();
                for(;;){check(gen);pump_manual(gen);MixerAction action;std::string key;uint32_t ms;std::string effect;bool old_ftb;int old_pv,old_pg;unsigned layer;int overlay_input;TallyBus overlay_bus;
                    {std::lock_guard<std::mutex> lock(mutex);action=command;key=target;ms=duration;effect=function;old_ftb=view.ftb;old_pv=preview_number;old_pg=program_number;layer=overlay_layer;overlay_input=overlay_expected;overlay_bus=overlay_destination;command=MixerAction::none;}
                    if(action!=MixerAction::none) {
                        std::string fn=action==MixerAction::preview?"PreviewInput":action==MixerAction::program?"CutDirect":action==MixerAction::cut?"Cut":effect;
                        std::string args=action==MixerAction::output||action==MixerAction::overlay?"":" Mix=0";if(!key.empty())args+=(action==MixerAction::overlay?" Input=":"&Input=")+encode(key);if(action==MixerAction::automatic&&effect.rfind("Stinger",0)!=0)args+="&Duration="+std::to_string(ms);
                        auto reply=rpc("FUNCTION "+fn+args,"FUNCTION",gen);
                        if(reply.rfind("FUNCTION ER",0)==0){std::lock_guard<std::mutex> lock(mutex);if(gen==generation){view.busy=false;if(action==MixerAction::overlay)++view.command_failures;std::snprintf(view.message.data(),view.message.size(),"%s",reply.c_str());}}
                        else {
                            if(action==MixerAction::automatic){mixing=true;mix_end=Clock::now()+std::chrono::milliseconds(ms);mix_target=key;refresh(gen);}
                            else {
                                const auto until=Clock::now()+std::chrono::seconds(action==MixerAction::overlay?20:2);
                                for(;;) {
                                    refresh(gen);bool confirmed=false;
                                    {std::lock_guard<std::mutex> lock(mutex);
                                     if(action==MixerAction::overlay)confirmed=layer<view.overlay_channels&&view.overlays[layer].known&&overlay_numbers[layer]==overlay_input&&view.overlays[layer].bus==overlay_bus;
                                     else if(action==MixerAction::output)confirmed=view.ftb_known&&view.ftb!=old_ftb;
                                     else if(action==MixerAction::cut)confirmed=program_number==old_pv&&preview_number==old_pg;
                                     else for(const auto& input:inputs)if(input.key==key)
                                         confirmed=action==MixerAction::preview?preview_number==input.number:program_number==input.number;
                                     if(confirmed&&gen==generation)view.busy=false;}
                                    if(confirmed)break;
                                    if(Clock::now()>until)throw Failure("VMIX COMMAND NOT CONFIRMED");
                                    pause(10);
                                }
                            }
                        }
                        refreshed=Clock::now();tally_dirty=false;
                    }
                    if(tally_dirty||Clock::now()-refreshed>std::chrono::milliseconds(mixing?40:500)){tally_dirty=false;refresh(gen);refreshed=Clock::now();}
                    // Drain only complete event lines; fragmented frames stay buffered.
                    while(buffer.find("\r\n")!=std::string::npos){auto s=line(gen,Clock::now()+std::chrono::seconds(2));if(s.rfind("TALLY OK ",0)==0){tally_flags=s.substr(9);tally_dirty=true;}else throw Failure("VMIX UNEXPECTED EVENT");}
                    pollfd p{fd,POLLIN,0};if(poll(&p,1,10)>0)receive(gen,Clock::now()+std::chrono::seconds(2));
                }
            } catch(const std::exception& e){
                bool active;{std::lock_guard<std::mutex> lock(mutex);active=manual_active&&manual_started;}
                if(active){try{rpc("FUNCTION SetFader Value=0","FUNCTION",gen);refresh(gen);restore_manual_effect(gen);}catch(...) {}}
                status(e.what(),gen);
            }
            close_socket();mixing=false;pause(300);
        }
    }
};
VmixAdapter::VmixAdapter(const std::string& path):impl(new Impl(path)){}
VmixAdapter::~VmixAdapter(){delete impl;}
void VmixAdapter::configure(const Config& c){impl->cancel_and_wait();std::lock_guard<std::mutex> lock(impl->mutex);impl->software_version.clear();impl->config=c;++impl->generation;impl->enabled=c.backend==Backend::vmix;impl->command=MixerAction::none;impl->view={};}
void VmixAdapter::deactivate(){Config c;c.backend=Backend::midi;configure(c);}
MixerState VmixAdapter::state(){std::lock_guard<std::mutex> lock(impl->mutex);return impl->view;}
bool VmixAdapter::request(MixerAction a,unsigned slot){std::lock_guard<std::mutex> lock(impl->mutex);if(!impl->view.connected||impl->view.busy||impl->view.transitioning||impl->command!=MixerAction::none)return false;
    if(a!=MixerAction::cut&&a!=MixerAction::preview&&a!=MixerAction::program)return false;
    if(a!=MixerAction::cut&&(slot>=24||!impl->view.available[slot]))return false;
    impl->target=a==MixerAction::cut?"":impl->mappings[slot];impl->command=a;impl->view.busy=true;return true;}
bool VmixAdapter::toggle_overlay(unsigned layer,unsigned source,bool preview) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(!impl->view.connected||impl->view.busy||impl->view.transitioning||impl->command!=MixerAction::none||layer>=8||layer>=impl->view.overlay_channels||!impl->view.overlays[layer].known||source>=24||!impl->view.available[source])return false;
    const auto key=impl->mappings[source];int number=-1;
    for(const auto& input:impl->inputs)if(input.key==key){number=input.number;break;}
    if(number<1)return false;
    const auto bus=preview?TallyBus::preview:TallyBus::program;
    const bool remove=impl->overlay_numbers[layer]==number&&impl->view.overlays[layer].bus==bus;
    impl->function=(preview?"PreviewOverlayInput":"OverlayInput")+std::to_string(layer+1)+(preview?"":remove?"Out":"In");
    impl->target=remove&&!preview?"":key;
    impl->overlay_layer=layer;impl->overlay_expected=remove?-1:number;impl->overlay_destination=remove?TallyBus::none:bus;
    impl->command=MixerAction::overlay;impl->view.busy=true;return true;
}
bool VmixAdapter::automatic(TransitionType t,uint32_t ms,uint32_t code,bool reverse,uint32_t){if(ms<50||ms>20000)return false;std::lock_guard<std::mutex> lock(impl->mutex);
    if(!impl->view.connected||impl->view.busy||impl->view.transitioning||impl->command!=MixerAction::none)return false;
    std::string effect;
    const auto& maps=impl->transitions;
    if(t==TransitionType::wipe){auto i=maps["wipes"].find(std::to_string(code));if(i!=maps["wipes"].end())effect=i.value()[reverse?"reverse":"normal"].get<std::string>();}
    else if(t==TransitionType::mix){if(code<=3)effect=maps["mix_types"][code?code-1:0].get<std::string>();}
    else if(t==TransitionType::stinger){if(code>=1&&code<=8){effect=maps["stingers"][code-1]["function"].get<std::string>();ms=maps["stingers"][code-1]["total_ms"].get<uint32_t>();}}
    else {unsigned slot=t==TransitionType::slide?2:t==TransitionType::swipe?3:t==TransitionType::dme?code:99;if(slot<=7)effect=maps["dme_types"][slot][reverse?"reverse":"normal"].get<std::string>();}
    if(effect.empty())return false;
    impl->target.clear();for(const auto& i:impl->inputs)if(i.number==impl->preview_number)impl->target=i.key;
    if(impl->target.empty()||impl->preview_number==impl->program_number)return false;
    impl->function=effect;impl->duration=ms;impl->command=MixerAction::automatic;impl->view.busy=true;return true;}
bool VmixAdapter::manual(uint16_t pos,TransitionType t,uint32_t code,bool reverse,uint32_t) {
    if(pos>4095||t==TransitionType::stinger||t==TransitionType::dip)return false;
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(!impl->view.connected)return false;
    if(impl->manual_active){if(impl->manual_terminal)return true;}
    else {
        if(!pos||impl->view.busy||impl->view.transitioning||impl->command!=MixerAction::none||impl->transition_one.empty()||impl->preview_number==impl->program_number)return false;
        auto name=impl->effect(t,code,reverse);if(name.empty())return false;
        impl->manual_effect=name;impl->manual_pg=impl->program_number;impl->manual_pv=impl->preview_number;impl->manual_active=true;impl->manual_started=false;impl->manual_sent=-1;impl->view.busy=true;impl->view.manual_transition=true;
    }
    impl->manual_position=pos==4095?255:pos==0?0:std::max(1u,std::min(254u,(unsigned(pos)*255u+2047u)/4095u));
    impl->manual_terminal=pos==0||pos==4095;return true;
}
void VmixAdapter::cancel_manual(){std::lock_guard<std::mutex> lock(impl->mutex);if(impl->manual_active&&!impl->manual_terminal){impl->manual_position=0;impl->manual_terminal=true;}}
bool VmixAdapter::keypad_transition_available(TransitionType t,unsigned slot) const {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(t==TransitionType::stinger)return slot>=1&&slot<=8&&!impl->transitions["stingers"][slot-1]["function"].get<std::string>().empty();
    if(t==TransitionType::mix&&(slot<1||slot>3))return false;
    if(t==TransitionType::dme&&slot>7)return false;
    return !impl->effect(t,slot,false).empty();
}
std::string VmixAdapter::keypad_transition_label(TransitionType t,unsigned slot) const {std::lock_guard<std::mutex> lock(impl->mutex);std::string name;if(t==TransitionType::mix&&slot>=1&&slot<=3)name=impl->transitions["mix_types"][slot-1].get<std::string>();if(t==TransitionType::dme&&slot<=7)name=impl->transitions["dme_types"][slot]["normal"].get<std::string>();return name.empty()?"OFF":name;}
std::string VmixAdapter::dme_label(unsigned slot) const {return keypad_transition_label(TransitionType::dme,slot);}
bool VmixAdapter::valid_wipe_code(uint32_t code) const {std::lock_guard<std::mutex> lock(impl->mutex);auto i=impl->transitions["wipes"].find(std::to_string(code));return i!=impl->transitions["wipes"].end()&&!i.value()["normal"].get<std::string>().empty();}
bool VmixAdapter::fade_to_black(uint32_t){std::lock_guard<std::mutex> lock(impl->mutex);if(!impl->view.connected||!impl->view.ftb_known||impl->view.busy||impl->view.transitioning||impl->command!=MixerAction::none)return false;impl->target.clear();impl->function="FadeToBlack";impl->command=MixerAction::output;impl->view.busy=true;return true;}
std::string VmixAdapter::web_status(){std::lock_guard<std::mutex> lock(impl->mutex);Json list=Json::array();for(const auto& i:impl->inputs)list.push_back({{"number",i.number},{"key",i.key},{"title",i.title}});
    Json overlays=Json::array();for(unsigned i=0;i<impl->view.overlay_channels;i++){const auto& o=impl->view.overlays[i];overlays.push_back({{"channel",i+1},{"known",o.known},{"source",o.source},{"bus",o.bus==TallyBus::program?"program":o.bus==TallyBus::preview?"preview":"none"}});}
    return Json({{"overlays",overlays},{"connected",impl->view.connected},{"busy",impl->view.busy},{"transitioning",impl->view.transitioning},{"message",impl->view.message.data()},{"inputs",list},{"preview",impl->view.preview},{"program",impl->view.program},{"revision",impl->revision},{"profile",{{"version",1},{"sources",impl->mappings},{"transitions",impl->transitions}}}}).dump();}
bool VmixAdapter::save_mappings(const std::string& body,std::string& error){try{auto j=Json::parse(body);auto m=Impl::parse_map(j.at("profile"));auto t=Impl::parse_transitions(j.at("profile"));std::lock_guard<std::mutex> lock(impl->mutex);
    if(!j.at("revision").is_number_unsigned()||j.at("revision")!=impl->revision){error="Mapping changed; reload";return false;}
    if(impl->view.busy||impl->view.transitioning){error="Mixer busy";return false;}
    std::string pattern=impl->path+".tmp-XXXXXX";std::vector<char> name(pattern.begin(),pattern.end());name.push_back(0);int fd=mkstemp(name.data());if(fd<0){error="Cannot save mappings";return false;}
    auto saved=j.at("profile");saved["transitions"]=t;auto text=saved.dump(2)+"\n";size_t at=0;while(at<text.size()){auto n=write(fd,text.data()+at,text.size()-at);if(n<0&&errno==EINTR)continue;if(n<=0)break;at+=size_t(n);}bool ok=at==text.size()&&fsync(fd)==0;close(fd);
    if(!ok||rename(name.data(),impl->path.c_str())<0){unlink(name.data());error="Cannot save mappings";return false;}
    impl->mappings=m;impl->transitions=t;impl->mapping_error=false;impl->view.available.reset();impl->view.preview=impl->view.program=-1;++impl->revision;return true;
}catch(const std::exception&){error="Invalid mappings";return false;}}
}

namespace bkds::link {
bool VmixAdapter::server_info(ServerInfo& out)const{std::lock_guard<std::mutex> lock(impl->mutex);out.fields={{"SERVER","vMix"},{"VERSION",impl->software_version.empty()?"NOT REPORTED":impl->software_version},{"INPUTS",std::to_string(impl->inputs.size())},{"LINK",impl->view.connected?"CONNECTED":"DISCONNECTED"}};return true;}
}
