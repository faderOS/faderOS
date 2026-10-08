#include "web_posix.hpp"
#include "kavtor_posix.hpp"
#include "vmix_posix.hpp"
#include "atem_posix.hpp"
#include <web_page.hpp>
#include "home.hpp"
#include <nlohmann/json.hpp>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <atomic>
#include <fstream>
#include <mutex>
#include <thread>
#include <iostream>
#include <fcntl.h>
using Json=nlohmann::json;
namespace bkds::link {
struct WebConfig::Impl {
    int listener=-1;unsigned port;std::string path;
    ObsAdapter* obs;KavtorAdapter* kavtor=nullptr;VmixAdapter* vmix=nullptr;AtemAdapter* atem=nullptr;std::atomic<bool> stop{false};std::thread worker;std::mutex mutex,context_mutex;
    unsigned profile_slot=0;unsigned revision=0;bool pending=false;Mappings next;std::array<std::string,24> scenes{};
    Json profile,system=Json::object();
    int pending_server=-1;
    Config system_next;uint32_t system_expected=0;bool system_pending=false,system_full=false;
    std::string system_error;
    static const char* edge_text(uint8_t edge) { return edge?"bottom":"top"; }
    static const char* align_text(uint8_t align) { return align==0?"left":align==2?"right":"center"; }
    static bool take_edge(const Json& body,const char* key,uint8_t& slot) {
        if(!body.contains(key)) return true;
        if(!body.at(key).is_string()) return false;
        const auto text=body.at(key).get<std::string>();
        if(text=="top") slot=0; else if(text=="bottom") slot=1; else return false;
        return true;
    }
    static bool take_align(const Json& body,const char* key,uint8_t& slot) {
        if(!body.contains(key)) return true;
        if(!body.at(key).is_string()) return false;
        const auto text=body.at(key).get<std::string>();
        if(text=="left") slot=0; else if(text=="center") slot=1; else if(text=="right") slot=2; else return false;
        return true;
    }
    Json layout_json(const OverlayLayout& layout,bool connected) const {
        return {{"programLeft",layout.program_left},{"nameEdge",edge_text(layout.name_edge)},{"nameAlign",align_text(layout.name_align)},
            {"clockEdge",edge_text(layout.clock_edge)},{"clockAlign",align_text(layout.clock_align)},
            {"safePreview",layout.safe_preview},{"safeProgram",layout.safe_program},{"meters",layout.meters},
            {"safePreviewAspect",safe_aspect_name(layout.safe_preview_aspect)},{"safeProgramAspect",safe_aspect_name(layout.safe_program_aspect)},{"safePreset",layout.safe_preset?"legacy":"ebu-r95"},{"known",layout.known},{"revision",layout.revision},{"connected",connected}};
    }
    Impl(unsigned p,std::string file,ObsAdapter& o,KavtorAdapter* video,VmixAdapter* v,AtemAdapter* a,unsigned slot):port(p),path(std::move(file)),obs(&o),kavtor(video),vmix(v),atem(a),profile_slot(slot) {
        try {
            std::ifstream in(path);
            if(in) { in>>profile;Mappings m;std::array<std::string,24> s;if(!load_mappings(path,m,s)) throw std::runtime_error("Invalid mapping file"); }
            else profile={{"version",1},{"sources",Json::array()}};
            listener=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);if(listener<0) return;
            int reuse=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof reuse);
            sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(port);addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
            if(bind(listener,reinterpret_cast<sockaddr*>(&addr),sizeof addr)<0||listen(listener,4)<0) { close(listener);listener=-1;return; }
            worker=std::thread([this]{run();});
        } catch(const std::exception& e) { std::cerr<<"WEB: "<<e.what()<<'\n'; }
    }
    ~Impl(){stop=true;if(worker.joinable())worker.join();if(listener>=0)close(listener);}
    void response(int fd,int code,const std::string& body,const char* type="application/json") {
        const auto out="HTTP/1.1 "+std::to_string(code)+" Result\r\nContent-Type: "+type+"\r\nContent-Length: "+std::to_string(body.size())+"\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n\r\n"+body;
        size_t at=0;while(at<out.size()){auto n=send(fd,out.data()+at,out.size()-at,MSG_NOSIGNAL);if(n<=0)break;at+=size_t(n);}
    }
    void handle(int fd) {
        std::lock_guard<std::mutex> context(context_mutex);
        std::string data;char bytes[2048];size_t end;
        while((end=data.find("\r\n\r\n"))==std::string::npos) {auto n=recv(fd,bytes,sizeof bytes,0);if(n<=0)return;data.append(bytes,size_t(n));if(data.size()>32768)return;}
        std::istringstream head(data.substr(0,end));std::string method,url,version;head>>method>>url>>version;
        std::string line,host,origin,content_type,request_slot;size_t length=0;bool has_length=false,bad=false;
        std::getline(head,line);
        while(std::getline(head,line)) {if(!line.empty()&&line.back()=='\r')line.pop_back();auto sep=line.find(':');if(sep==std::string::npos)continue;auto key=line.substr(0,sep),value=line.substr(sep+1);while(!value.empty()&&value.front()==' ')value.erase(0,1);for(auto& c:key)c=char(std::tolower(static_cast<unsigned char>(c)));
            if(key=="host")host=value;
            if(key=="x-server-slot")request_slot=value;
            if(key=="origin")origin=value;
            if(key=="content-type")content_type=value;
            if(key=="transfer-encoding")bad=true;
            if(key=="content-length") {if(has_length||value.empty()||value.find_first_not_of("0123456789")!=std::string::npos)bad=true;length=std::stoul(value);has_length=true;}
        }
        const auto local="127.0.0.1:"+std::to_string(port),named="localhost:"+std::to_string(port);
        if(bad||(host!=local&&host!=named)||(!origin.empty()&&origin!="http://"+host)){response(fd,403,R"({"error":"Origin not allowed"})");return;}
        if(method=="POST"){std::lock_guard<std::mutex> lock(mutex);if(request_slot!=std::to_string(profile_slot)||system.value("serverSlot",profile_slot)!=profile_slot){response(fd,409,R"({"error":"Server profile is changing; reload"})");return;}}
        if(length>65536){response(fd,413,R"({"error":"Request too large"})");return;}
        if(method=="GET"&&url=="/"){response(fd,200,web_page,"text/html; charset=utf-8");return;}
        if(method=="GET"&&url=="/app.js"){response(fd,200,web_js,"text/javascript; charset=utf-8");return;}
        if(method=="GET"&&url=="/styles.css"){response(fd,200,web_css,"text/css; charset=utf-8");return;}
        if(method=="GET"&&url=="/api/system") {std::lock_guard<std::mutex> lock(mutex);auto j=system;j["profileSlot"]=profile_slot;j["pending"]=system_pending;j["error"]=system_error;response(fd,200,j.dump());return;}
        if(method=="GET"&&url=="/api/info") {
            std::string protocol;{std::lock_guard<std::mutex> lock(mutex);protocol=system.value("activeProtocol",std::string("OBS"));}
            MixerAdapter* adapter=protocol=="ATEM"?static_cast<MixerAdapter*>(atem):protocol=="VMIX"?static_cast<MixerAdapter*>(vmix):protocol=="KAVTOR"?static_cast<MixerAdapter*>(kavtor):protocol=="OBS"?static_cast<MixerAdapter*>(obs):nullptr;
            ServerInfo info;Json fields=Json::array();if(adapter&&adapter->server_info(info))for(const auto& field:info.fields)fields.push_back({{"label",field.first},{"value",field.second}});
            response(fd,200,Json({{"serverSlot",profile_slot},{"protocol",protocol},{"fields",fields}}).dump());return;
        }
        if(method=="GET"&&url=="/api/status") {auto j=Json::parse(obs->web_status());{std::lock_guard<std::mutex> lock(mutex);j["pending"]=pending;}response(fd,200,j.dump());return;}
        if(method=="GET"&&url=="/api/mappings") {std::lock_guard<std::mutex> lock(mutex);response(fd,200,Json({{"revision",revision},{"profile",profile}}).dump());return;}
        if(method=="POST"&&url.rfind("/api/kavtor/",0)==0) {
            std::lock_guard<std::mutex> lock(mutex);
            if(!system.contains("server")||system.value("activeProtocol",std::string())!="KAVTOR"||system_pending) {
                response(fd,409,R"({"error":"Select kavtor and wait for it to apply before sending commands"})");return;
            }
        }
        if(url=="/api/obs/auth"&&method=="POST") {
            if(!has_length||content_type!="application/json"){response(fd,400,R"({"error":"JSON required"})");return;}
            while(data.size()<end+4+length){auto n=recv(fd,bytes,sizeof bytes,0);if(n<=0)return;data.append(bytes,size_t(n));}
            auto j=Json::parse(data.substr(end+4,length));
            if(!j.is_object()||j.size()!=1||!j.at("password").is_string()){response(fd,400,R"({"error":"Invalid credentials"})");return;}
            if(!obs->set_password(j.at("password").get<std::string>())){response(fd,409,R"({"error":"Mixer busy or credentials could not be saved"})");return;}
            response(fd,200,R"({"ok":true})");return;
        }
        if(url=="/api/vmix/status"&&method=="GET") {
            if(!vmix){response(fd,404,R"({"error":"vMix unavailable"})");return;}
            response(fd,200,vmix->web_status());return;
        }
        if(url=="/api/vmix/mappings"&&method=="POST") {
            if(!vmix){response(fd,404,R"({"error":"vMix unavailable"})");return;}
            if(!has_length||content_type!="application/json"){response(fd,400,R"({"error":"JSON required"})");return;}
            while(data.size()<end+4+length){auto n=recv(fd,bytes,sizeof bytes,0);if(n<=0)return;data.append(bytes,size_t(n));}
            std::string error;if(!vmix->save_mappings(data.substr(end+4,length),error)){response(fd,409,Json({{"error",error}}).dump());return;}
            response(fd,200,R"({"ok":true})");return;
        }
        if(url=="/api/server-slot"&&method=="POST"){
            if(!has_length||content_type!="application/json"){response(fd,400,R"({"error":"JSON required"})");return;}
            while(data.size()<end+4+length){auto n=recv(fd,bytes,sizeof bytes,0);if(n<=0)return;data.append(bytes,size_t(n));}
            try{const auto j=Json::parse(data.substr(end+4,length));std::lock_guard<std::mutex> lock(mutex);if(!j.at("slot").is_number_unsigned()||j.at("slot")>=3||system_pending||pending){response(fd,409,R"({"error":"Profile unavailable or edits pending"})");return;}pending_server=j.at("slot").get<int>();response(fd,202,R"({"pending":true})");}catch(...){response(fd,400,R"({"error":"Invalid server slot"})");}return;
        }
        if(url=="/api/atem/status"&&method=="GET") {
            if(!atem){response(fd,404,R"({"error":"ATEM unavailable"})");return;}
            response(fd,200,atem->web_status());return;
        }
        if(url=="/api/atem/output"&&method=="POST") {
            {std::lock_guard<std::mutex> lock(mutex);
                if(system.value("activeProtocol",std::string())!="ATEM"||system_pending){response(fd,409,R"({"error":"Select ATEM before sending commands"})");return;}}
            if(!atem){response(fd,404,R"({"error":"ATEM unavailable"})");return;}
            if(!has_length||content_type!="application/json"){response(fd,400,R"({"error":"JSON required"})");return;}
            while(data.size()<end+4+length){auto n=recv(fd,bytes,sizeof bytes,0);if(n<=0)return;data.append(bytes,size_t(n));}
            try{auto j=Json::parse(data.substr(end+4,length));if(!j.at("output").is_number_unsigned()||!j.at("source").is_number_unsigned()||!atem->set_output_source(j.at("output").get<unsigned>(),j.at("source").get<unsigned>())){response(fd,409,R"({"error":"Output unavailable or mixer busy"})");return;}}catch(...){response(fd,400,R"({"error":"Invalid output request"})");return;}
            response(fd,202,R"({"pending":true})");return;
        }
        if(url=="/api/atem/multiview"&&method=="POST") {
            {std::lock_guard<std::mutex> lock(mutex);if(system.value("activeProtocol",std::string())!="ATEM"||system_pending){response(fd,409,R"({"error":"Select ATEM before sending commands"})");return;}}
            if(!atem){response(fd,404,R"({"error":"ATEM unavailable"})");return;}
            if(!has_length||content_type!="application/json"){response(fd,400,R"({"error":"JSON required"})");return;}
            while(data.size()<end+4+length){auto n=recv(fd,bytes,sizeof bytes,0);if(n<=0)return;data.append(bytes,size_t(n));}
            try{const auto j=Json::parse(data.substr(end+4,length));
                if(!j.at("viewer").is_number_unsigned()||!j.at("enabled").is_boolean()||!j.at("property").is_string()||(j.at("property")!="safe"&&j.at("property")!="meters")||(!j.contains("inputs")&&!j.contains("window"))||(j.contains("inputs")&&(!j.at("inputs").is_boolean()||!j.at("inputs").get<bool>()||j.contains("window")))||(j.contains("window")&&!j.at("window").is_number_unsigned())){response(fd,400,R"({"error":"Invalid multiview request"})");return;}
                const unsigned viewer=j.at("viewer").get<unsigned>();const bool safe=j.at("property")=="safe",enabled=j.at("enabled").get<bool>();
                const bool accepted=j.contains("inputs")?atem->set_multiview_inputs(viewer,safe,enabled):atem->set_multiview_window(viewer,j.at("window").get<unsigned>(),safe,enabled);
                if(!accepted){response(fd,409,R"({"error":"Multiview control unavailable or mixer busy"})");return;}
            }catch(...){response(fd,400,R"({"error":"Invalid multiview request"})");return;}
            response(fd,202,R"({"pending":true})");return;
        }
        if(url=="/api/atem/video-format"&&method=="POST") {
            {std::lock_guard<std::mutex> lock(mutex);
                if(system.value("activeProtocol",std::string())!="ATEM"||system_pending){response(fd,409,R"({"error":"Select ATEM before sending commands"})");return;}}
            if(!atem){response(fd,404,R"({"error":"ATEM unavailable"})");return;}
            if(!has_length||content_type!="application/json"){response(fd,400,R"({"error":"JSON required"})");return;}
            while(data.size()<end+4+length){auto n=recv(fd,bytes,sizeof bytes,0);if(n<=0)return;data.append(bytes,size_t(n));}
            try{auto j=Json::parse(data.substr(end+4,length));if(!j.at("confirm").is_boolean()||!j.at("confirm").get<bool>()||!j.at("mode").is_number_unsigned()||!j.at("previous").is_number_integer()||!atem->set_video_format(j.at("mode").get<unsigned>(),j.at("previous").get<int>())){response(fd,409,R"({"error":"Format changed, unsupported or mixer busy"})");return;}}catch(...){response(fd,400,R"({"error":"Invalid format request"})");return;}
            response(fd,202,R"({"pending":true})");return;
        }
        if(url=="/api/atem/dsk"&&method=="POST") {
            {std::lock_guard<std::mutex> lock(mutex);
                if(system.value("activeProtocol",std::string())!="ATEM"||system_pending){response(fd,409,R"({"error":"Select ATEM before sending commands"})");return;}}
            if(!atem){response(fd,404,R"({"error":"ATEM unavailable"})");return;}
            if(!has_length||content_type!="application/json"){response(fd,400,R"({"error":"JSON required"})");return;}
            while(data.size()<end+4+length){auto n=recv(fd,bytes,sizeof bytes,0);if(n<=0)return;data.append(bytes,size_t(n));}
            std::string error;if(!atem->configure_dsk(data.substr(end+4,length),error)){response(fd,409,Json({{"error",error}}).dump());return;}
            response(fd,202,R"({"pending":true})");return;
        }
        if(url=="/api/atem/keyers"&&method=="POST") {
            {std::lock_guard<std::mutex> lock(mutex);
                if(system.value("activeProtocol",std::string())!="ATEM"||system_pending){response(fd,409,R"({"error":"Select ATEM before sending commands"})");return;}}
            if(!atem){response(fd,404,R"({"error":"ATEM unavailable"})");return;}
            if(!has_length||content_type!="application/json"){response(fd,400,R"({"error":"JSON required"})");return;}
            while(data.size()<end+4+length){auto n=recv(fd,bytes,sizeof bytes,0);if(n<=0)return;data.append(bytes,size_t(n));}
            std::string error;if(!atem->configure_key(data.substr(end+4,length),error)){response(fd,409,Json({{"error",error}}).dump());return;}
            response(fd,202,R"({"pending":true})");return;
        }
        if(url=="/api/atem/mappings"&&method=="POST") {
            if(!atem){response(fd,404,R"({"error":"ATEM unavailable"})");return;}
            if(!has_length||content_type!="application/json"){response(fd,400,R"({"error":"JSON required"})");return;}
            while(data.size()<end+4+length){auto n=recv(fd,bytes,sizeof bytes,0);if(n<=0)return;data.append(bytes,size_t(n));}
            std::string error;if(!atem->save_mappings(data.substr(end+4,length),error)){response(fd,409,Json({{"error",error}}).dump());return;}
            response(fd,200,R"({"ok":true})");return;
        }
        if(url=="/api/kavtor/layout") {
            if(!kavtor){response(fd,404,R"({"error":"kavtor is unavailable in this process"})");return;}
            if(method=="GET") {
                OverlayLayout layout; const bool connected=kavtor->overlay_layout(layout);
                response(fd,200,layout_json(layout,connected).dump()); return;
            }
            if(method!="POST"){response(fd,404,R"({"error":"Route not found"})");return;}
            if(!has_length||content_type!="application/json"){response(fd,400,R"({"error":"JSON required"})");return;}
            while(data.size()<end+4+length){auto n=recv(fd,bytes,sizeof bytes,0);if(n<=0)return;data.append(bytes,size_t(n));}
            const auto body=Json::parse(data.substr(end+4,length));
            OverlayLayout layout; kavtor->overlay_layout(layout);
            if(body.contains("programLeft")) { if(!body.at("programLeft").is_boolean()){response(fd,400,R"({"error":"programLeft invalid"})");return;} layout.program_left=body.at("programLeft").get<bool>(); }
            if(body.contains("safePreview")) { if(!body.at("safePreview").is_boolean()){response(fd,400,R"({"error":"safePreview invalid"})");return;} layout.safe_preview=body.at("safePreview").get<bool>(); }
            if(body.contains("safeProgram")) { if(!body.at("safeProgram").is_boolean()){response(fd,400,R"({"error":"safeProgram invalid"})");return;} layout.safe_program=body.at("safeProgram").get<bool>(); }
            for(const char* name:{"safePreviewAspect","safeProgramAspect"}) if(body.contains(name)) {
                if(!body.at(name).is_string()){response(fd,400,R"({"error":"Invalid framing guide"})");return;}
                unsigned value=6;for(unsigned i=0;i<6;++i)if(body.at(name)==safe_aspect_name(i))value=i;
                if(value==6){response(fd,400,R"({"error":"Invalid framing guide"})");return;}
                if(std::string(name)=="safePreviewAspect")layout.safe_preview_aspect=uint8_t(value);else layout.safe_program_aspect=uint8_t(value);
            }
            if(body.contains("safePreset")) {
                if(body.at("safePreset")!="ebu-r95"&&body.at("safePreset")!="legacy"){response(fd,400,R"({"error":"Invalid safe preset"})");return;}
                layout.safe_preset=body.at("safePreset")=="legacy"?1:0;
            }
            if(body.contains("meters")) { if(!body.at("meters").is_boolean()){response(fd,400,R"({"error":"meters invalid"})");return;} layout.meters=body.at("meters").get<bool>(); }
            if(!take_edge(body,"nameEdge",layout.name_edge)||!take_align(body,"nameAlign",layout.name_align)
               ||!take_edge(body,"clockEdge",layout.clock_edge)||!take_align(body,"clockAlign",layout.clock_align)) {
                response(fd,400,R"({"error":"Invalid label position"})"); return;
            }
            if(!kavtor->set_overlay_layout(layout)){response(fd,503,R"({"error":"kavtor disconnected"})");return;}
            OverlayLayout applied; const bool connected=kavtor->overlay_layout(applied);
            response(fd,202,layout_json(applied,connected).dump()); return;
        }
        if(url=="/api/kavtor/status") {
            if(!kavtor){response(fd,404,R"({"error":"kavtor is unavailable in this process"})");return;}
            if(method!="GET"){response(fd,404,R"({"error":"Route not found"})");return;}
            response(fd,200,kavtor->web_status()); return;
        }
        if(url=="/api/kavtor/command") {
            if(!kavtor){response(fd,404,R"({"error":"kavtor is unavailable in this process"})");return;}
            if(method!="POST"){response(fd,404,R"({"error":"Route not found"})");return;}
            if(!has_length||content_type!="application/json"){response(fd,400,R"({"error":"JSON required"})");return;}
            while(data.size()<end+4+length){auto n=recv(fd,bytes,sizeof bytes,0);if(n<=0)return;data.append(bytes,size_t(n));}
            if(!kavtor->submit(data.substr(end+4,length))){response(fd,409,R"({"error":"kavtor cannot accept the command"})");return;}
            response(fd,202,R"({"ok":true})"); return;
        }
        if(method!="POST"||(url!="/api/mappings"&&url!="/api/system")){response(fd,404,R"({"error":"Route not found"})");return;}
        if(!has_length||content_type!="application/json"){response(fd,400,R"({"error":"JSON required"})");return;}
        while(data.size()<end+4+length){auto n=recv(fd,bytes,sizeof bytes,0);if(n<=0)return;data.append(bytes,size_t(n));}
        const auto j=Json::parse(data.substr(end+4,length));
        std::lock_guard<std::mutex> lock(mutex);
        if(url=="/api/system") {
            if(!system.contains("revision")){response(fd,503,R"({"error":"Host state unavailable"})");return;}
            if(!j.at("revision").is_number_unsigned()||j.at("revision")!=system.at("revision")||system_pending) {response(fd,409,R"({"error":"Server changed; reload its configuration"})");return;}
            const auto& server=j.at("server");
            Backend backend=Backend::obs;
            bool known=false;
            if(server.contains("protocol")&&server.at("protocol").is_string()) {
                const auto label=server.at("protocol").get<std::string>();
                for(unsigned i=0;i<BackendCount;i++) if(label==ProtocolLabels[i]) { backend=Backend(i); known=true; }
            }
            if(!known){response(fd,400,R"({"error":"Unknown protocol"})");return;}
            Config posted;
            if(server.contains("endpoints")&&server.at("endpoints").is_object()) {
                const auto& all=server.at("endpoints");
                if(all.size()!=BackendCount){response(fd,400,R"({"error":"Missing protocol servers"})");return;}
                for(unsigned i=0;i<BackendCount;i++) {
                    const auto& one=all.at(ProtocolLabels[i]);
                    if(!one.is_object()||!one.at("ip").is_string()||!one.at("port").is_number_unsigned()||one.at("port")>65535){response(fd,400,R"({"error":"Invalid protocol server"})");return;}
                    IPv4 ip;if(!parse_ip(one.at("ip").get<std::string>().c_str(),ip)){response(fd,400,R"({"error":"Invalid IPv4 address"})");return;}
                    posted.endpoints[i].host=ip; posted.endpoints[i].port=one.at("port").get<uint16_t>();
                }
                system_full=true;
            } else if(server.at("ip").is_string()&&server.at("port").is_number_unsigned()&&server.at("port")<=65535) {
                IPv4 ip;if(!parse_ip(server.at("ip").get<std::string>().c_str(),ip)){response(fd,400,R"({"error":"Invalid IPv4 address"})");return;}
                posted.endpoints[unsigned(backend)].host=ip; posted.endpoints[unsigned(backend)].port=server.at("port").get<uint16_t>();
                system_full=false;
            } else {response(fd,400,R"({"error":"Invalid server"})");return;}
            system_next.backend=backend; system_next.endpoints=posted.endpoints;
            system_expected=j.at("revision").get<uint32_t>();system_pending=true;system_error.clear();
            response(fd,202,R"({"pending":true})");return;
        }
        if(!j.at("revision").is_number_unsigned()||j.at("revision")!=revision||pending){response(fd,409,R"({"error":"Configuration changed or pending; reload before saving"})");return;}
        const auto text=j.at("profile").dump(2)+"\n";
        const auto temp=path+".web-XXXXXX";std::vector<char> name(temp.begin(),temp.end());name.push_back(0);
        int out=mkstemp(name.data());if(out<0){response(fd,500,R"({"error":"Cannot save configuration"})");return;}
        size_t at=0;
        while(at<text.size()){auto n=write(out,text.data()+at,text.size()-at);if(n<=0)break;at+=size_t(n);}
        bool saved=at==text.size()&&fsync(out)==0;close(out);
        Mappings m;std::array<std::string,24> s;
        if(!saved||!load_mappings(name.data(),m,s)){unlink(name.data());response(fd,400,R"({"error":"Invalid mapping or write error"})");return;}
        if(rename(name.data(),path.c_str())<0){unlink(name.data());response(fd,500,R"({"error":"Cannot replace the file"})");return;}
        profile=j.at("profile");next=m;scenes=s;pending=true;++revision;
        response(fd,202,Json({{"revision",revision}}).dump());
    }
    void run(){while(!stop){pollfd p{listener,POLLIN,0};if(poll(&p,1,100)<=0)continue;int fd=accept4(listener,nullptr,nullptr,SOCK_CLOEXEC);if(fd<0)continue;timeval t{1,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&t,sizeof t);setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&t,sizeof t);try{handle(fd);}catch(const std::exception&){response(fd,400,R"({"error":"Invalid request"})");}close(fd);}}
};
WebConfig::WebConfig(unsigned p,const std::string& path,ObsAdapter& obs,KavtorAdapter* kavtor,VmixAdapter* vmix,AtemAdapter* atem,unsigned slot):impl(new Impl(p,path,obs,kavtor,vmix,atem,slot)){}
WebConfig::~WebConfig(){delete impl;}
bool WebConfig::valid()const{return impl->listener>=0&&impl->worker.joinable();}
bool WebConfig::take(Mappings& m,std::array<std::string,24>& s){std::unique_lock<std::mutex> lock(impl->mutex,std::try_to_lock);if(!lock.owns_lock()||!impl->pending)return false;m=impl->next;s=impl->scenes;impl->pending=false;return true;}
}

namespace bkds::link {
bool WebConfig::sync_system(Configuration& config,const char* firmware,const char* date,uint32_t baud,bool connected,bool enabled,bool idle,const char* active_protocol) {
    std::unique_lock<std::mutex> lock(impl->mutex,std::try_to_lock);if(!lock.owns_lock())return false;
    bool changed=false;
    if(impl->system_pending&&idle) {
        auto next=config.saved();
        const auto backend=impl->system_next.backend;
        if(unsigned(backend)>=BackendCount) impl->system_error="Unknown protocol";
        else {
            next.backend=backend;
            if(impl->system_full) next.endpoints=impl->system_next.endpoints;
            else next.endpoints[unsigned(backend)]=impl->system_next.endpoints[unsigned(backend)];
            const auto result=config.save(next,impl->system_expected);
            changed=result==SaveResult::saved;
            impl->system_error=result==SaveResult::saved||result==SaveResult::unchanged?"":result==SaveResult::conflict?"Configuration changed from panel; reload before saving":"Cannot save server";
        }
        impl->system_pending=false;
    }
    if(impl->pending_server>=0&&idle){auto next=config.saved();next.select_server(unsigned(impl->pending_server));const auto result=config.save(next,config.revision());impl->system_error=result==SaveResult::saved||result==SaveResult::unchanged?"":"Cannot select server profile";changed=changed||result==SaveResult::saved;impl->pending_server=-1;}
    const auto& c=config.saved();
    Json endpoints=Json::object();
    for(unsigned i=0;i<BackendCount;i++) {
        const auto& ip=c.endpoints[i].host;
        endpoints[ProtocolLabels[i]]={{"ip",std::to_string(ip[0])+"."+std::to_string(ip[1])+"."+std::to_string(ip[2])+"."+std::to_string(ip[3])},{"port",c.endpoints[i].port}};
    }
    const char* running=active_protocol?active_protocol:ProtocolLabels[unsigned(c.backend)];
    const auto& active=endpoints.at(ProtocolLabels[unsigned(c.backend)]);
    impl->system={{"serverSlot",c.server},{"revision",config.revision()},{"hostVersion",HostVersion},{"firmwareVersion",firmware},{"firmwareDate",date},{"baud",baud},{"panelConnected",connected},{"adapterEnabled",enabled},{"activeProtocol",running},{"adapterPending",std::string(running)!=ProtocolLabels[unsigned(c.backend)]},{"kavtor",impl->kavtor!=nullptr&&std::string(running)=="KAVTOR"},{"server",{{"protocol",ProtocolLabels[unsigned(c.backend)]},{"ip",active.at("ip")},{"port",active.at("port")},{"endpoints",endpoints}}}};
    return changed;
}
}

namespace bkds::link {
bool WebConfig::bind_profile(const std::string& file,ObsAdapter& obs,KavtorAdapter* kavtor,VmixAdapter* vmix,AtemAdapter* atem,unsigned slot){
    std::unique_lock<std::mutex> context(impl->context_mutex,std::try_to_lock);if(!context.owns_lock())return false;
    std::unique_lock<std::mutex> lock(impl->mutex,std::try_to_lock);if(!lock.owns_lock()||slot>=3)return false;
    if(impl->pending||impl->system_pending)return false;
    try{Json profile;std::ifstream in(file);if(in){in>>profile;Mappings m;std::array<std::string,24> scenes;if(!load_mappings(file,m,scenes))return false;}else profile={{"version",1},{"sources",Json::array()}};
        impl->profile_slot=slot;impl->system=Json::object();impl->system_error.clear();impl->path=file;impl->profile=std::move(profile);++impl->revision;impl->obs=&obs;impl->kavtor=kavtor;impl->vmix=vmix;impl->atem=atem;return true;
    }catch(...){return false;}
}
}
