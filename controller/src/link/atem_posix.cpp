#include "atem_posix.hpp"
#include "atem_transport.hpp"
#include <nlohmann/json.hpp>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>
#include <map>
#include <set>
#include <cstdio>
#include <cerrno>
using Json=nlohmann::json;
namespace bkds::link {
namespace {
using atem::put;using atem::Bytes;using atem::field;using atem::u16;
uint64_t now_ms(){return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
struct Failure:std::runtime_error{using std::runtime_error::runtime_error;};
std::string str(const uint8_t* p,size_t n){auto end=std::find(p,p+n,0);return std::string(p,end);}
struct Input {std::string title,short_name;uint8_t me_mask=0,availability=0,kind=0;};
struct USK {
    bool on_known=false,base_known=false,luma_known=false,on=false,can_fly=false,premultiplied=false,invert=false;
    unsigned type=0,fill=0,key=0,clip=0,gain=0,revision=0;
    bool mask_known=false,mask=false,dve_known=false,dve_mask=false,border=false,shadow=false,pattern_known=false,pattern_invert=false;
    Bytes base,luma;
};
// Sony identifiers stay stable on the keypad; ATEM uses its own enum.
// DIRECT 800..817 addresses the complete ATEM enum without inventing Sony IDs.
constexpr unsigned extra_patterns[]={4,10,11,12,13,14,15,17};
constexpr const char* extra_names[]={"FOUR CORNERS","BOTTOM RIGHT BOX","BOTTOM LEFT BOX","TOP CENTRE BOX","RIGHT CENTRE BOX","BOTTOM CENTRE BOX","LEFT CENTRE BOX","TOP RIGHT DIAGONAL"};
int wipe_pattern(uint32_t code){
    if(code>=800&&code<=817)return int(code-800);
    switch(code){case 1:return 0;case 3:return 1;case 18:return 3;case 17:return 2;
    case 21:return 5;case 23:return 6;case 24:return 7;case 5:return 8;case 6:return 9;
    case 9:return 16;default:return -1;}
}
int dve_style(uint32_t code){
    if((code>=2601&&code<=2608)||(code>=2621&&code<=2628)){static const unsigned positions[]={4,6,8,2,7,9,3,1};bool squeeze=code>=2621;code=positions[code-(squeeze?2621:2601)]+(squeeze?10:0);}

    const bool squeeze=code>=10;if(squeeze)code-=10;
    int direction=-1;switch(code){case 7:direction=0;break;case 8:direction=1;break;case 9:direction=2;break;case 4:direction=3;break;case 6:direction=4;break;case 1:direction=5;break;case 2:direction=6;break;case 3:direction=7;break;}
    return direction<0?-1:direction+(squeeze?16:24);
}
void dve_settings(Bytes& out,unsigned me,unsigned frames,uint32_t code,bool reverse,bool with_rate){
    Bytes data(20,0);put(data,0,4|1024|2048|(with_rate?1:0));data[2]=uint8_t(me);data[3]=uint8_t(frames);data[5]=uint8_t(dve_style(code));data[17]=reverse;field(out,"CTDv",data);
}
void wipe_settings(Bytes& out,unsigned me,unsigned frames,uint32_t code,bool reverse,uint32_t soft,bool with_rate,unsigned extra=0,unsigned border=0,unsigned symmetry=5000,unsigned x=5000,unsigned y=5000){
    Bytes data(20,0);put(data,0,extra|2|32|256|512|(with_rate?1:0));data[2]=uint8_t(me);data[3]=uint8_t(frames);data[4]=uint8_t(wipe_pattern(code));put(data,6,border);put(data,10,symmetry);put(data,14,x);put(data,16,y);put(data,12,soft*100);data[18]=reverse;field(out,"CTWp",data);
}
struct ME {std::vector<USK> keys;bool next_known=false;unsigned next_layers=0;bool ftb_known=false,black=false,ftb_mixing=false;unsigned ftb_rate=0,ftb_revision=0;int program=-1,preview=-1,keyers=-1;unsigned rate=0,style=0,layers=0;bool transitioning=false,both=false,preview_transition=false,preview_known=false,manual=false;unsigned position=0,prepared_wipe_mask=0,prepared_border=0,prepared_symmetry=5000,prepared_x=5000,prepared_y=5000;bool dip_known=false;unsigned dip_rate=25,dip_input=2001;bool dve_known=false;unsigned dve_rate=25,dve_style=24;bool dve_reverse=false,dve_flip=false;bool wipe_known=false;unsigned wipe_rate=25,wipe_pattern=0,wipe_soft=0;bool wipe_reverse=false,wipe_flip=false;};
struct DSK {
    bool state_known=false,base_known=false,props_known=false,on=false,mixing=false,auto_mix=false,tie=false,premultiplied=false,invert=false;
    unsigned fill=0,key=0,rate=0,clip=0,gain=0,revision=0,rate_revision=0;
    bool mask_known=false,mask=false;
    Bytes base,props;
};
struct Profile {
    unsigned me=0;std::array<std::string,24> sources{};
    std::map<unsigned,std::array<std::string,24>> me_sources;
    const std::array<std::string,24>& for_me(unsigned index) const {
        auto it=me_sources.find(index);return it==me_sources.end()?sources:it->second;
    }
};
std::array<std::string,24> parse_row(const Json& row){
    if(!row.is_array()||row.size()!=24)throw Failure("Invalid source row");
    std::array<std::string,24> result;
    for(unsigned i=0;i<24;i++){if(!row[i].is_string())throw Failure("Invalid input ID");auto s=row[i].get<std::string>();
        if(!s.empty()&&(s.size()>5||s.find_first_not_of("0123456789")!=std::string::npos||std::stoul(s)>65535||std::to_string(std::stoul(s))!=s))throw Failure("Invalid input ID");
        if(i==11||i==23)continue; // Reserved next-M/E key, including migrated v1 profiles.
        for(unsigned k=0;k<i;k++)if(!s.empty()&&result[k]==s)throw Failure("Input assigned more than once");
        result[i]=std::move(s);
    }return result;
}
Profile parse_profile(const Json& j){
    if(!j.is_object()||(j.at("version")!=1&&j.at("version")!=2)||!j.at("me").is_number_unsigned()||j.at("me")>255)throw Failure("Invalid ATEM profile");
    Profile p;p.me=j.at("me");p.sources=parse_row(j.at("sources"));
    if(j.at("version")==2){const auto& rows=j.at("meSources");if(!rows.is_object()||rows.size()>256)throw Failure("Invalid M/E assignments");
        for(auto it=rows.begin();it!=rows.end();++it){const auto& key=it.key();if(key.empty()||key.size()>3||key.find_first_not_of("0123456789")!=std::string::npos||std::stoul(key)>255||std::to_string(std::stoul(key))!=key)throw Failure("Invalid M/E index");p.me_sources[unsigned(std::stoul(key))]=parse_row(it.value());}
    }return p;
}
Json profile_json(const Profile& p){Json rows=Json::object();for(const auto& [me,row]:p.me_sources)rows[std::to_string(me)]=row;return Json{{"version",2},{"me",p.me},{"sources",p.sources},{"meSources",rows}};}

}
struct AtemAdapter::Impl {
    std::mutex mutex;std::atomic<bool> stop{false};std::thread worker;Config config;unsigned generation=0;bool enabled=false;
    MixerState view;std::map<unsigned,Input> inputs;std::vector<ME> mes;std::vector<DSK> dsks;Json caps=Json::object();std::string product,path;uint32_t version=0;
    Profile profile;unsigned selected_me=0;unsigned revision=0;bool invalid_profile=false,initialized=false;
    bool clear_command=false,clear_pending=false;unsigned clear_slot=0;uint64_t clear_until=0;
    bool media_known=false,media_op=false;unsigned still_slots=0,media_type=0,media_still=0,media_desired=0;
    std::map<unsigned,uint8_t> source_tallies;
    std::map<unsigned,unsigned> aux_sources;unsigned aux_revision=0;
    VideoFormats formats;bool format_op=false;unsigned format_target=0;
    bool route_op=false,output_op=false,output_target_on=false;unsigned route_target=0,route_source=0,output_target=0;
    std::array<unsigned,2> output_status{};
    std::map<unsigned,MultiviewSettings> multiviews;unsigned multiview_revision=0;
    bool multiview_op=false,multiview_safe=false,multiview_enabled=false;
    unsigned multiview_target=0,multiview_window=0;std::bitset<16> multiview_pending;
    std::map<unsigned,std::pair<bool,std::string>> stills;
    std::map<unsigned,Bytes> still_fingerprints,capture_before;
    std::set<unsigned> capture_changes;
    bool capture_command=false,capture_pending=false,capture_dispatched=false;
    uint64_t capture_until=0;
    bool capture_supported()const{return view.connected&&product.find("ATEM Mini")!=std::string::npos;}
    void capture_update(){
        if(!capture_pending||capture_command||!capture_dispatched)return;
        if(capture_changes.size()==1){view.media_capture_slot=int(*capture_changes.begin());view.media_capture_status=2;capture_pending=false;}
        else if(capture_changes.size()>1||now_ms()>=capture_until){view.media_capture_slot=-1;view.media_capture_status=3;capture_pending=false;}
    }

    void clear_update(){
        if(!clear_pending||clear_command)return;
        const auto f=stills.find(clear_slot);
        if(f!=stills.end()&&!f->second.first){view.media_clear_status=2;clear_pending=false;}
        else if(now_ms()>=clear_until){view.media_clear_status=3;clear_pending=false;}
    }
    // An acknowledged, unconfirmed key command is not a broken UDP session.
    // Keep authoritative state/tallies, release only the failed operation and
    // never resend it with a new identity. Transport loss still reconnects.
    void key_unconfirmed(){
        std::fprintf(stderr,"ATEM COMMAND NOT CONFIRMED: key M/E=%u index=%u operation=%u\n",key_target_me+1,key_target+1,unsigned(key_op));
        ++view.command_failures;key_op=KeyOp::none;dispatched=false;
        action=MixerAction::none;view.busy=false;publish();
    }
    bool transition_seen=false;
    Bytes live_commands;
    Bytes queued;MixerAction action=MixerAction::none;bool preview_op=false,preview_desired=false,preview_sent=false;
    bool manual_active=false,manual_terminal=false,manual_preview=false;unsigned manual_desired=0,manual_sent=10001;uint64_t manual_send_at=0;
    bool rate_only=false,wipe_rate_only=false,dve_rate_only=false,dip_rate_only=false;unsigned requested_rate=0;int target_pg=-1,target_pv=-1;
    enum class KeyOp {none,toggle,next,settings,decorations};KeyOp key_op=KeyOp::none;
    unsigned key_target_me=0,key_target=0,next_desired=1,next_sent=1;bool key_target_on=false;
    USK expected_key;std::array<bool,4> target_keys{};unsigned take_layers=0;uint32_t key_revision_counter=0;
    enum class FtbOp {none,toggle,rate};FtbOp ftb_op=FtbOp::none;bool ftb_target=false;uint32_t ftb_revision_counter=0;
    enum class DskOp {none,toggle,rate,settings,mask};DskOp dsk_op=DskOp::none;
    uint32_t dsk_revision_counter=0,dsk_rate_counter=0;
    unsigned dsk_target=0;DSK expected_dsk;bool dsk_target_on=false;
    int fd=-1;atem::Transport transport;bool dispatched=false;uint64_t deadline=0;
    explicit Impl(std::string p):path(std::move(p)){std::ifstream in(path);if(in){try{Json j;in>>j;profile=parse_profile(j);}catch(...){invalid_profile=true;}}worker=std::thread([this]{run();});}
    ~Impl(){stop=true;worker.join();}
    void pause(unsigned ms){for(unsigned i=0;i<ms&&!stop;i+=10)std::this_thread::sleep_for(std::chrono::milliseconds(10));}
    bool usable(unsigned id) const {auto it=inputs.find(id);return it!=inputs.end()&&selected_me<8&&(it->second.me_mask&(1u<<selected_me));}
    int source_at(unsigned slot) const {
        if(slot>=24)return -1;
        if(slot==11||slot==23){
            if(selected_me+1>=mes.size())return -1;
            const unsigned next=10010+10*(selected_me+1);
            auto it=inputs.find(next);
            return it!=inputs.end()&&it->second.kind==128&&usable(next)?int(next):-1;
        }
        const auto& row=profile.for_me(selected_me);return row[slot].empty()?-1:int(std::stoul(row[slot]));
    }
    int mapped(unsigned id) const {for(unsigned i=0;i<24;i++)if(source_at(i)==int(id))return int(i);return -1;}
    bool media_safe_to_clear(unsigned index)const{
        if(media_type!=1||index!=media_still)return true;
        const auto fill=source_tallies.find(3010),key=source_tallies.find(3011);
        return fill!=source_tallies.end()&&key!=source_tallies.end()&&!(fill->second&1)&&!(key->second&1);
    }
    void publish(){
        const auto fill_tally=source_tallies.find(3010),key_tally=source_tallies.find(3011);
        view.media_program_known=initialized&&fill_tally!=source_tallies.end()&&key_tally!=source_tallies.end();
        view.media_on_program=view.media_program_known&&((fill_tally->second|key_tally->second)&1);
        view.media_known=initialized&&media_known;view.media_still_slots=initialized?still_slots:0;
        view.media_still=view.media_known&&media_type==1?int(media_still):-1;
        view.key_premult.fill(-1);view.dsk_premult.fill(-1);view.key_mask.fill(-1);view.key_submask.fill(-1);view.key_border.fill(-1);view.key_shadow.fill(-1);view.key_invert.fill(-1);view.dsk_mask.fill(-1);view.dsk_invert.fill(-1);view.dsk_source.fill(-1);
        view.key_kind.fill(-1);view.key_cut_source.fill(-1);view.dsk_kind.fill(-1);view.dsk_cut_source.fill(-1);
        if(initialized&&!mes.empty()&&selected_me>=mes.size())selected_me=0;
        for(unsigned i=0;i<2;i++){
            const bool known=initialized&&i<dsks.size()&&dsks[i].state_known;
            view.dsk_known_at(i)=known;view.dsk_on_at(i)=known&&dsks[i].on;view.dsk_mixing_at(i)=known&&dsks[i].mixing;
            view.dsk_frames[i]=initialized&&i<dsks.size()&&dsks[i].props_known?dsks[i].rate:0;
            view.dsk_rate_revision[i]=i<dsks.size()?dsks[i].rate_revision:0;
            if(known&&dsks[i].base_known&&dsks[i].props_known){const auto& d=dsks[i];view.dsk_source[i]=mapped(d.fill);view.dsk_cut_source[i]=mapped(d.key);view.dsk_kind[i]=88;view.dsk_premult[i]=d.premultiplied;view.dsk_invert[i]=d.invert;view.dsk_mask[i]=d.mask_known?int(d.mask):-1;}
        }
        view.key_available.reset();view.keyers_known=view.next_known=false;view.next_background=false;view.key_on.fill(false);view.next_key.fill(false);view.key_source.fill(-1);
        view.available.reset();view.preview=view.program=-1;view.sources=unsigned(inputs.size());view.me=int(selected_me);view.me_count=unsigned(mes.size());
        view.connected=initialized&&selected_me<mes.size()&&mes[selected_me].program>=0&&mes[selected_me].preview>=0;
        view.ftb=view.ftb_known=view.ftb_transitioning=false;view.ftb_frames=view.ftb_rate_revision=0;
        view.studio=view.connected;view.transitioning=false;view.both_sources=false;view.rate_known=false;view.wipe_rate_known=false;view.dme_rate_known=false;view.dip_rate_known=false;view.transition_preview_known=view.transition_preview=false;view.manual_transition=false;
        if(view.connected){const auto& me=mes[selected_me];
            view.keyers_known=me.keyers>=0;view.next_known=me.next_known;view.next_background=me.next_known&&(me.next_layers&1);
            for(unsigned i=0;i<std::min<size_t>(4,me.keys.size());i++){
                const auto& k=me.keys[i];if(k.base_known){view.key_source[i]=mapped(k.fill);view.key_cut_source[i]=mapped(k.key);view.key_kind[i]=k.type==3?106:k.type==1?91:k.type==2?92:k.type==0?88:-1;view.key_premult[i]=k.luma_known&&k.type==0?int(k.premultiplied):-1;view.key_mask[i]=k.mask_known?int(k.mask):-1;view.key_submask[i]=k.dve_known&&k.type==3?int(k.dve_mask):-1;view.key_border[i]=k.dve_known&&k.type==3?int(k.border):-1;view.key_shadow[i]=k.dve_known&&k.type==3?int(k.shadow):-1;view.key_invert[i]=k.luma_known&&k.type==0?int(k.invert):k.pattern_known&&k.type==2?int(k.pattern_invert):-1;}
                view.key_available[i]=true;view.key_on[i]=me.keys[i].on_known&&me.keys[i].on;view.keyers_known=view.keyers_known&&me.keys[i].on_known;view.next_key[i]=me.next_known&&(me.next_layers&(2u<<i));}
view.ftb_known=me.ftb_known;view.ftb=me.ftb_known&&(me.black||me.ftb_mixing);view.ftb_transitioning=me.ftb_mixing;view.ftb_frames=me.ftb_rate;view.ftb_rate_revision=me.ftb_revision;view.transitioning=me.transitioning;view.both_sources=me.both&&!me.preview_transition;view.manual_transition=manual_active||(me.transitioning&&me.manual);view.transition_preview_known=me.preview_known;view.transition_preview=me.preview_transition;
            view.dip_rate_known=me.dip_known;
            if(me.dip_known&&view.dip_frames!=me.dip_rate){view.dip_frames=me.dip_rate;++view.dip_rate_revision;}
            view.dme_rate_known=me.dve_known;
            if(me.dve_known&&view.dme_frames!=me.dve_rate){view.dme_frames=me.dve_rate;++view.dme_rate_revision;}
            view.wipe_rate_known=me.wipe_known;
            if(me.wipe_known&&view.wipe_frames!=me.wipe_rate){view.wipe_frames=me.wipe_rate;++view.wipe_rate_revision;}
            if(me.rate){if(view.auto_frames!=me.rate){view.auto_frames=me.rate;++view.rate_revision;}view.rate_known=true;}
            for(unsigned i=0;i<24;i++)if(!invalid_profile){const int id=source_at(i);if(id>=0&&usable(unsigned(id))){view.available.set(i);if(i!=23){if(id==me.program)view.program=int(i);if(id==me.preview)view.preview=int(i);}}}
        }
        std::snprintf(view.message.data(),view.message.size(),"%s",invalid_profile?"ATEM INVALID MAPPING FILE":view.connected?"ATEM CONNECTED":initialized?"ATEM M/E UNAVAILABLE":"ATEM SYNCHRONIZING");
    }
    void reset(){formats={};aux_sources.clear();++aux_revision;format_op=false;route_op=output_op=false;output_status.fill(0);multiviews.clear();++multiview_revision;multiview_op=false;source_tallies.clear();clear_command=clear_pending=false;capture_command=capture_pending=capture_dispatched=false;capture_changes.clear();capture_before.clear();still_fingerprints.clear();media_known=media_op=false;still_slots=media_type=media_still=0;stills.clear();manual_active=manual_terminal=preview_op=false;key_op=KeyOp::none;ftb_op=FtbOp::none;view={};inputs.clear();mes.clear();dsks.clear();dsk_op=DskOp::none;caps=Json::object();product.clear();version=0;initialized=false;queued.clear();live_commands.clear();transition_seen=false;action=MixerAction::none;rate_only=false;dispatched=false;}
    void fail(const char* msg,unsigned gen){std::lock_guard<std::mutex> l(mutex);if(gen!=generation)return;reset();std::snprintf(view.message.data(),view.message.size(),"%s",msg);}
    void check(unsigned gen){std::lock_guard<std::mutex> l(mutex);if(stop||!enabled||gen!=generation)throw Failure("ATEM SESSION CHANGED");}
    void send(const Bytes& b){if(b.empty())return;ssize_t n;do{n=::send(fd,b.data(),b.size(),MSG_NOSIGNAL);}while(n<0&&errno==EINTR);if(n!=ssize_t(b.size()))throw Failure("ATEM UDP SEND FAILED");}
    void decode(const Bytes& b){
        for(size_t at=0;at<b.size();){if(b.size()-at<8)throw Failure("ATEM TRUNCATED FIELD");auto size=u16(b.data()+at);if(size<8||size>b.size()-at)throw Failure("ATEM INVALID FIELD LENGTH");
            std::string name(b.begin()+at+4,b.begin()+at+8);const auto* p=b.data()+at+8;size_t n=size-8;
            if(name=="_ver"&&n>=4)version=(uint32_t(u16(p))<<16)|u16(p+2);
            else if(name=="_pin"&&n>=40)product=str(p,40);
            else if(name=="_top"&&n>=6){
                mes.resize(p[0]);dsks.resize(p[2]);caps["mixEffects"]=p[0];caps["reportedSources"]=p[1];caps["downstreamKeyers"]=p[2];caps["auxiliaries"]=p[3];caps["mixMinusOutputs"]=p[4];caps["mediaPlayers"]=p[5];
                // Protocol 2.30 added the multiviewer count at offset 6.
                unsigned off=version>0x0002001d?1:0;
                if(off&&n>6)caps["multiviewers"]=p[6];
                if(n>10+off){caps["serialPorts"]=p[6+off];caps["hyperdecks"]=p[7+off];caps["dves"]=p[8+off];caps["stingers"]=p[9+off];caps["superSources"]=p[10+off];}
                if(n>22+off){caps["advancedChromaKeyers"]=p[21+off]==1;caps["onlyConfigurableOutputs"]=p[22+off]==1;}
            }
            else if(name=="_MeC"&&n>=2&&p[0]<mes.size()){mes[p[0]].keyers=p[1];mes[p[0]].keys.resize(p[1]);}
            else if(name=="_MvC"&&n>=2){if(version<0x0002001e)caps["multiviewers"]=p[0];if(p[1])caps["multiviewWindows"]=p[1];}
            else if(name=="InPr"&&n>=36)inputs[u16(p)]={str(p+2,20),str(p+22,4),p[35],p[34],p[32]};
            else if(name=="_mpl"&&n>=2){still_slots=p[0];caps["stillSlots"]=still_slots;}
            else if(name=="VidM"&&n>=1){formats.current=p[0];++formats.revision;}
            else if(name=="_VMC"&&n>=4){const unsigned count=u16(p),stride=version>=0x0002001c?13:12;if(count>(n-4)/stride)throw Failure("ATEM INVALID VIDEO MODES");formats.supported.clear();for(unsigned i=0;i<count;i++)formats.supported.push_back(p[4+i*stride]);++formats.revision;}
            else if(name=="AuxS"&&n>=4){aux_sources[p[0]]=u16(p+2);++aux_revision;}
            else if((name=="StRS"||name=="RTMS")&&n>=2){const unsigned i=name=="StRS"?0:1;output_status[i]=u16(p);view.output_known[i]=true;view.output_active[i]=(output_status[i]&(i?1:4))!=0;}
            else if((name=="VuMC"||name=="SaMw"||name=="MvIn")&&n>=(name=="MvIn"?6u:3u)&&p[1]<16){
                auto& window=multiviews[p[0]].windows[p[1]];const auto before=window;window.present=true;
                if(name=="VuMC")window.meters=p[2]!=0;
                else if(name=="SaMw")window.safe=p[2]!=0;
                else{window.source=u16(p+2);window.meters_supported=p[4]!=0;window.safe_supported=p[5]!=0;}
                if(before.present!=window.present||before.source!=window.source||before.safe!=window.safe||before.meters!=window.meters||before.safe_supported!=window.safe_supported||before.meters_supported!=window.meters_supported)++multiview_revision;
            }
            else if(name=="TlSr"&&n>=2&&u16(p)<=(n-2)/3){
                std::map<unsigned,uint8_t> tallies;
                for(unsigned i=0;i<u16(p);i++)tallies[u16(p+2+i*3)]=p[4+i*3];
                source_tallies=std::move(tallies);
            }
            else if(name=="MPCE"&&n>=4&&p[0]==0){media_known=true;media_type=p[1];media_still=p[2];}
            else if(name=="MPfe"&&n>=24&&p[0]==0&&p[23]<=n-24){const unsigned index=u16(p+2);Bytes fingerprint(p+4,p+n);
                if(capture_pending&&capture_dispatched&&index<still_slots&&p[4]&&capture_before.count(index)&&capture_before.at(index)!=fingerprint)capture_changes.insert(index);
                still_fingerprints[index]=std::move(fingerprint);stills[index]={p[4]!=0,str(p+24,p[23])};}
            else if(name=="InCm")initialized=true;
            else if((name=="DskB"||name=="DskP"||name=="DskS")&&n&&p[0]<dsks.size()){
                auto& d=dsks[p[0]];
                if(name=="DskB"&&n>=6){Bytes data(p+2,p+6);if(data!=d.base){d.base=std::move(data);d.revision=++dsk_revision_counter;}d.base_known=true;d.fill=u16(p+2);d.key=u16(p+4);}
                else if(name=="DskP"&&n>=9){Bytes data(p+1,p+9);if(data!=d.props){d.props=std::move(data);d.revision=++dsk_revision_counter;}if(!d.props_known||d.rate!=p[2])d.rate_revision=++dsk_rate_counter;d.props_known=true;d.tie=p[1]!=0;d.rate=p[2];d.premultiplied=p[3]!=0;d.clip=u16(p+4);d.gain=u16(p+6);d.invert=p[8]!=0;if(n>=18){d.mask_known=true;d.mask=p[9]!=0;}}
                else if(name=="DskS"&&n>=5){d.state_known=true;d.on=p[1]!=0;d.mixing=p[2]!=0;d.auto_mix=p[3]!=0;}
            }
            else if(n&&p[0]<mes.size()){
                auto& me=mes[p[0]];
                if(name=="PrgI"&&n>=4)me.program=u16(p+2);
                else if(name=="PrvI"&&n>=5){me.preview=u16(p+2);me.both=p[4]!=0;}
                else if(name=="TrPs"&&n>=6){me.transitioning=p[1]!=0;me.manual=me.transitioning&&p[2]==0;me.position=u16(p+4);}
                else if(name=="TrSS"&&n>=5){me.next_known=true;me.style=p[1];me.layers=p[2];me.next_layers=p[4];}
                else if((name=="TrPr"||name=="TsPr")&&n>=2){me.preview_known=true;me.preview_transition=p[1]!=0;}
                else if((name=="KeOn"||name=="KeBP"||name=="KeLm"||name=="KeDV"||name=="KePt")&&n>=2&&p[1]<me.keys.size()){
                    auto& k=me.keys[p[1]];
                    if(name=="KeOn"&&n>=3){k.on_known=true;k.on=p[2]!=0;}
                    else if(name=="KeBP"&&n>=10){Bytes data(p+2,p+n);if(data!=k.base){k.base=std::move(data);k.revision=++key_revision_counter;}k.base_known=true;k.type=p[2];k.can_fly=n>4&&p[4]!=0;k.fill=u16(p+6);k.key=u16(p+8);if(n>=20){k.mask_known=true;k.mask=p[10]!=0;}}
                    else if(name=="KePt"&&n>=15){k.pattern_known=true;k.pattern_invert=p[14]!=0;}
                    else if(name=="KeDV"&&n>=57){k.dve_known=true;k.border=p[24]!=0;k.shadow=p[25]!=0;k.dve_mask=p[47]!=0;}
                    else if(name=="KeLm"&&n>=9){Bytes data(p+2,p+9);if(data!=k.luma){k.luma=std::move(data);k.revision=++key_revision_counter;}k.luma_known=true;k.premultiplied=p[2]!=0;k.clip=u16(p+4);k.gain=u16(p+6);k.invert=p[8]!=0;}
                }
                else if(name=="FtbS"&&n>=4){me.ftb_known=true;me.black=p[1]!=0;me.ftb_mixing=p[2]!=0;}
                else if(name=="FtbP"&&n>=2){if(me.ftb_rate!=p[1]){me.ftb_rate=p[1];me.ftb_revision=++ftb_revision_counter;}}
                else if(name=="TDpP"&&n>=4){me.dip_known=true;me.dip_rate=p[1];me.dip_input=u16(p+2);}
                else if(name=="TDvP"&&n>=17){me.dve_known=true;me.dve_rate=p[1];me.dve_style=p[3];me.dve_reverse=p[15]!=0;me.dve_flip=p[16]!=0;}
                else if(name=="TWpP"&&n>=18){me.wipe_known=true;me.wipe_rate=p[1];me.wipe_pattern=p[2];me.wipe_soft=u16(p+10);me.wipe_reverse=p[16]!=0;me.wipe_flip=p[17]!=0;}
                else if(name=="TMxP"&&n>=2)me.rate=p[1];
            }
            at+=size;
        }
        // Terminal bus updates may arrive in separate UDP packets. Once a take
        // has started, TrPs ending it is authoritative; sources may have changed
        // from our buses or another client during the take.
        if(selected_me<mes.size()&&(manual_active||action==MixerAction::automatic||action==MixerAction::cut)&&mes[selected_me].transitioning)transition_seen=true;
        publish();
    }
    void finish(){
        if(!queued.empty()||!live_commands.empty()||!dispatched||!transport.pending.empty()||selected_me>=mes.size())return;
        auto& me=mes[selected_me];bool done=false;
        if(clear_command){clear_command=false;done=true;}
        else if(capture_command){capture_command=false;done=true;}
        else if(format_op)done=formats.current==int(format_target);
        else if(route_op){const auto it=aux_sources.find(route_target);done=it!=aux_sources.end()&&it->second==route_source;}
        else if(output_op)done=view.output_known[output_target]&&view.output_active[output_target]==output_target_on;
        else if(multiview_op){
            done=true;for(unsigned i=0;i<16;i++)if(multiview_pending[i]){const auto& w=multiviews[multiview_target].windows[i];if((multiview_safe?w.safe:w.meters)!=int(multiview_enabled))done=false;}
        }
        else if(media_op)done=media_known&&media_type==1&&media_still==media_desired;
        else if(preview_op){
            done=me.preview_known&&me.preview_transition==preview_sent;
            if(done&&preview_desired!=preview_sent){preview_sent=preview_desired;Bytes b;field(b,"CTPr",{uint8_t(selected_me),uint8_t(preview_sent),0,0});queued=std::move(b);dispatched=false;return;}
        }
        else if(manual_active){
            if(!manual_terminal||manual_sent!=manual_desired){dispatched=false;return;}
            done=!me.transitioning&&(transition_seen||(me.program==target_pg&&me.preview==target_pv));
            for(unsigned i=0;i<4;i++)if(take_layers&(2u<<i))done=done&&i<me.keys.size()&&me.keys[i].on_known&&me.keys[i].on==target_keys[i];
        }
        else if(key_op!=KeyOp::none){
            const auto& owner=mes[key_target_me];
            if(key_op==KeyOp::next){
                done=owner.next_known&&owner.next_layers==next_sent;
                if(done&&next_desired!=next_sent){next_sent=next_desired;Bytes b;field(b,"CTTp",{2,uint8_t(key_target_me),0,uint8_t(next_sent)});queued=std::move(b);dispatched=false;return;}
            }else if(key_target<owner.keys.size()){
                const auto& k=owner.keys[key_target];
                if(key_op==KeyOp::decorations)done=k.mask==expected_key.mask&&k.border==expected_key.border&&k.shadow==expected_key.shadow&&k.dve_mask==expected_key.dve_mask&&k.pattern_invert==expected_key.pattern_invert;
                else if(key_op==KeyOp::toggle)done=k.on_known&&k.on==key_target_on;
                else done=k.base_known&&k.type==expected_key.type&&k.fill==expected_key.fill&&k.key==expected_key.key&&(k.type!=0||(k.luma_known&&k.clip==expected_key.clip&&k.gain==expected_key.gain&&k.premultiplied==expected_key.premultiplied&&k.invert==expected_key.invert));
            }
        }
        else if(ftb_op!=FtbOp::none){done=ftb_op==FtbOp::rate?me.ftb_rate==requested_rate:me.ftb_known&&(me.ftb_mixing||me.black==ftb_target);}
        else if(dsk_op!=DskOp::none&&dsk_target<dsks.size()){
            const auto& d=dsks[dsk_target];
            if(dsk_op==DskOp::mask)done=d.mask_known&&d.mask==expected_dsk.mask;
            else if(dsk_op==DskOp::toggle)done=d.state_known&&((!d.mixing&&d.on==dsk_target_on)||(action==MixerAction::dsk_mix&&d.mixing&&d.auto_mix));
            else if(dsk_op==DskOp::rate)done=d.props_known&&d.rate==requested_rate;
            else done=d.base_known&&d.props_known&&d.fill==expected_dsk.fill&&d.key==expected_dsk.key&&d.rate==expected_dsk.rate&&d.tie==expected_dsk.tie&&d.premultiplied==expected_dsk.premultiplied&&d.clip==expected_dsk.clip&&d.gain==expected_dsk.gain&&d.invert==expected_dsk.invert;
        }
        else if(rate_only)done=dip_rate_only?me.dip_known&&me.dip_rate==requested_rate:dve_rate_only?me.dve_known&&me.dve_rate==requested_rate:wipe_rate_only?me.wipe_known&&me.wipe_rate==requested_rate:me.rate==requested_rate;
        else if(action==MixerAction::program)done=me.program==target_pg;
        else if(action==MixerAction::preview)done=me.preview==target_pv;
        else if(action==MixerAction::cut||action==MixerAction::automatic){
            done=!me.transitioning&&(transition_seen||(me.program==target_pg&&me.preview==target_pv));
            for(unsigned i=0;i<4;i++)if(take_layers&(2u<<i))done=done&&i<me.keys.size()&&me.keys[i].on_known&&me.keys[i].on==target_keys[i];
        }
        if(done){format_op=false;route_op=output_op=false;multiview_op=false;media_op=false;if(action==MixerAction::automatic||(action==MixerAction::cut&&transition_seen&&!me.preview_transition)||(manual_active&&!manual_preview&&manual_desired==10000))++view.completed_auto;manual_active=manual_terminal=preview_op=false;dispatched=false;action=MixerAction::none;rate_only=false;key_op=KeyOp::none;ftb_op=FtbOp::none;dsk_op=DskOp::none;view.busy=false;publish();}
    }
    void run(){while(!stop){Config c;unsigned gen;bool on;{std::lock_guard<std::mutex> l(mutex);c=config;gen=generation;on=enabled;}if(!on){pause(10);continue;}
        try{
            fail("ATEM CONNECTING",gen);fd=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);if(fd<0)throw Failure("ATEM SOCKET FAILED");
            sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(c.active().port?c.active().port:9910);auto ip=c.active().host;a.sin_addr.s_addr=htonl((uint32_t(ip[0])<<24)|(uint32_t(ip[1])<<16)|(uint32_t(ip[2])<<8)|ip[3]);
            if(connect(fd,reinterpret_cast<sockaddr*>(&a),sizeof a)<0)throw Failure("ATEM CONNECT FAILED");
            auto start=now_ms();send(transport.hello(uint16_t((start^unsigned(getpid()))&0x7fff),start));
            for(;;){check(gen);send(transport.tick(now_ms()));
                {std::lock_guard<std::mutex> l(mutex);if(gen!=generation)continue;
                    if(manual_active&&!dispatched&&live_commands.empty()&&queued.empty()&&transport.pending.empty()&&manual_sent!=manual_desired&&now_ms()>=manual_send_at){
                        manual_sent=manual_desired;Bytes b;field(b,"CTPs",{uint8_t(selected_me),0,uint8_t(manual_sent>>8),uint8_t(manual_sent)});queued=std::move(b);manual_send_at=now_ms()+10;
                    }
                    if(clear_command&&!queued.empty()&&transport.pending.empty()&&!media_safe_to_clear(clear_slot)){
                        queued.clear();clear_command=clear_pending=false;view.media_clear_status=3;view.busy=false;
                        publish();std::fprintf(stderr,"ATEM MEDIA CLEAR BLOCKED: MP1 on program or tally unknown\n");
                    }
                    if(!queued.empty()&&transport.pending.empty()){if(clear_command)clear_until=now_ms()+5000;if(capture_command){capture_before=still_fingerprints;capture_changes.clear();capture_dispatched=true;capture_until=now_ms()+5000;}send(transport.send(queued,now_ms()));queued.clear();dispatched=true;deadline=now_ms()+((action==MixerAction::automatic||action==MixerAction::dsk_mix)?20000:2500);}
                    if(!live_commands.empty()&&queued.empty()&&transport.pending.empty()){send(transport.send(live_commands,now_ms()));live_commands.clear();}
                    finish();capture_update();clear_update();
                    if(dispatched&&now_ms()>deadline&&!(action==MixerAction::automatic&&view.transitioning)){
                        if((format_op||multiview_op||route_op||output_op)&&transport.pending.empty()){format_op=false;route_op=output_op=false;multiview_op=false;dispatched=false;view.busy=false;std::fprintf(stderr,"ATEM DISPLAY/OUTPUT COMMAND NOT CONFIRMED\n");}
                        else if(key_op!=KeyOp::none&&transport.pending.empty()&&queued.empty()&&live_commands.empty())key_unconfirmed();
                        else throw Failure("ATEM COMMAND NOT CONFIRMED");
                    }
                    if(!initialized&&now_ms()-start>5000)throw Failure("ATEM INITIAL STATE TIMEOUT");
                }
                pollfd pfd{fd,POLLIN,0};int r=poll(&pfd,1,5);if(r<0&&errno==EINTR)continue;if(r<0||(pfd.revents&(POLLERR|POLLNVAL)))throw Failure("ATEM DISCONNECTED");if(!r)continue;
                uint8_t data[2048];auto n=recv(fd,data,sizeof data,0);if(n<0&&(errno==EAGAIN||errno==EINTR))continue;if(n<0)throw Failure("ATEM RECEIVE FAILED");
                std::vector<Bytes> replies;auto fields=transport.receive(Bytes(data,data+n),now_ms(),replies);for(const auto& reply:replies)send(reply);
                {std::lock_guard<std::mutex> l(mutex);if(gen==generation){if(!fields.empty())decode(fields);finish();capture_update();clear_update();}}
            }
        }catch(const std::exception& e){fail(e.what(),gen);}if(fd>=0){close(fd);fd=-1;}pause(300);
    }}
    bool prepare_take(const ME& me){
        if(!me.next_known||!me.next_layers||(me.next_layers&~31u))return false;
        take_layers=me.next_layers;target_pg=(take_layers&1)?me.preview:me.program;target_pv=(take_layers&1)?me.program:me.preview;
        for(unsigned i=0;i<4;i++)if(take_layers&(2u<<i)){if(i>=me.keys.size()||!me.keys[i].on_known)return false;target_keys[i]=!me.keys[i].on;}
        return true;
    }
    bool ready() const{return view.connected&&!invalid_profile&&!view.busy&&!view.transitioning&&selected_me<mes.size();}
    void queue(Bytes b,MixerAction a){transition_seen=false;format_op=false;route_op=output_op=false;multiview_op=false;media_op=false;preview_op=false;key_op=KeyOp::none;ftb_op=FtbOp::none;dsk_op=DskOp::none;queued=std::move(b);action=a;rate_only=wipe_rate_only=dve_rate_only=dip_rate_only=false;view.busy=true;}
};
AtemAdapter::AtemAdapter(const std::string& path):impl(new Impl(path)){}
AtemAdapter::~AtemAdapter(){delete impl;}
void AtemAdapter::configure(const Config& c){std::lock_guard<std::mutex> l(impl->mutex);impl->config=c;++impl->generation;impl->enabled=c.backend==Backend::atem;impl->selected_me=impl->profile.me;impl->reset();}
void AtemAdapter::deactivate(){Config c;c.backend=Backend::midi;configure(c);}
MixerState AtemAdapter::state(){std::lock_guard<std::mutex> l(impl->mutex);return impl->view;}
bool AtemAdapter::request(MixerAction action,unsigned slot){std::lock_guard<std::mutex> l(impl->mutex);if(!impl->view.connected||impl->invalid_profile||impl->selected_me>=impl->mes.size())return false;
    const bool source=action==MixerAction::program||action==MixerAction::preview;
    const bool live=impl->view.transitioning&&(source||action==MixerAction::cut);
    if(!live&&!impl->ready())return false;
    auto me=uint8_t(impl->selected_me);Bytes b;const auto& state=impl->mes[me];
    if(live&&action==MixerAction::cut){
        if(!impl->view.busy&&!impl->prepare_take(state))return false;
        field(impl->live_commands,"DCut",{me,0,0,0});impl->manual_active=impl->manual_terminal=false;
        impl->transition_seen=true;impl->action=MixerAction::cut;impl->dispatched=true;impl->view.busy=true;impl->deadline=now_ms()+2500;return true;
    }
    if(action==MixerAction::cut){if(!impl->prepare_take(state))return false;field(b,"CTTp",{2,me,0,uint8_t(impl->take_layers)});field(b,"DCut",{me,0,0,0});}
    else if(action==MixerAction::program||action==MixerAction::preview){if(slot>=24||!impl->view.available[slot])return false;auto id=unsigned(impl->source_at(slot));field(b,action==MixerAction::program?"CPgI":"CPvI",{me,0,uint8_t(id>>8),uint8_t(id)});if(live){impl->live_commands.insert(impl->live_commands.end(),b.begin(),b.end());return true;}impl->target_pg=action==MixerAction::program?int(id):state.program;impl->target_pv=action==MixerAction::preview?int(id):state.preview;}
    else return false;
    impl->queue(std::move(b),action);return true;
}
bool AtemAdapter::set_transition_preview(bool on){
    std::lock_guard<std::mutex> l(impl->mutex);
    if(impl->preview_op){impl->preview_desired=on;return true;}
    if(!impl->ready()||!impl->mes[impl->selected_me].preview_known)return false;
    if(impl->mes[impl->selected_me].preview_transition==on)return true;
    Bytes b;field(b,"CTPr",{uint8_t(impl->selected_me),uint8_t(on),0,0});impl->queue(std::move(b),MixerAction::none);
    impl->preview_op=true;impl->preview_sent=impl->preview_desired=on;return true;
}
bool AtemAdapter::manual(uint16_t position,TransitionType type,uint32_t code,bool reverse,uint32_t soft){
    if(position>4095||(type!=TransitionType::mix&&type!=TransitionType::wipe&&type!=TransitionType::dme&&type!=TransitionType::dip)||(type==TransitionType::wipe&&(wipe_pattern(code)<0||soft>100))||(type==TransitionType::dme&&dve_style(code)<0))return false;
    std::lock_guard<std::mutex> l(impl->mutex);
    if(type==TransitionType::dme&&impl->caps.value("dves",0u)==0)return false;
    const unsigned value=position==4095?10000:unsigned(position)*10000/4095;
    if(impl->manual_active){
        if(!impl->manual_terminal){
            impl->manual_desired=value;impl->manual_terminal=position==0||position==4095;
            if(position==0){const auto& me=impl->mes[impl->selected_me];impl->target_pg=me.program;impl->target_pv=me.preview;for(unsigned i=0;i<4&&i<me.keys.size();i++)impl->target_keys[i]=me.keys[i].on;}
        }
        return true;
    }
    if(position==0||!impl->ready())return false;
    const auto& me=impl->mes[impl->selected_me];
    if(!me.preview_known||!impl->prepare_take(me)||(type==TransitionType::dip&&!impl->usable(2001)))return false;
    impl->manual_preview=me.preview_transition;
    if(impl->manual_preview){impl->target_pg=me.program;impl->target_pv=me.preview;for(unsigned i=0;i<4&&i<me.keys.size();i++)impl->target_keys[i]=me.keys[i].on;}
    Bytes b;field(b,"CTTp",{3,uint8_t(impl->selected_me),uint8_t(type==TransitionType::wipe?2:type==TransitionType::dme?3:type==TransitionType::dip?1:0),uint8_t(impl->take_layers)});
    if(type==TransitionType::wipe)wipe_settings(b,impl->selected_me,0,code,reverse,soft,false,me.prepared_wipe_mask,me.prepared_border,me.prepared_symmetry,me.prepared_x,me.prepared_y);
    if(type==TransitionType::dme)dve_settings(b,impl->selected_me,0,code,reverse,false);
    if(type==TransitionType::dip){Bytes d(8,0);d[0]=2;d[1]=uint8_t(impl->selected_me);put(d,4,2001);field(b,"CTDp",d);}
    impl->queue(std::move(b),MixerAction::manual);impl->manual_active=true;impl->manual_terminal=position==4095;impl->manual_desired=value;impl->manual_sent=10001;impl->manual_send_at=0;return true;
}
void AtemAdapter::cancel_manual(){
    std::lock_guard<std::mutex> l(impl->mutex);
    if(!impl->manual_active||impl->manual_terminal)return;
    impl->manual_desired=0;impl->manual_terminal=true;
    const auto& me=impl->mes[impl->selected_me];impl->target_pg=me.program;impl->target_pv=me.preview;for(unsigned i=0;i<4&&i<me.keys.size();i++)impl->target_keys[i]=me.keys[i].on;
}
bool AtemAdapter::automatic(TransitionType type,uint32_t frames,uint32_t code,bool reverse,uint32_t soft){
    std::lock_guard<std::mutex> l(impl->mutex);
    if(impl->view.connected&&!impl->invalid_profile&&impl->view.transitioning&&impl->selected_me<impl->mes.size()){
        if(!impl->view.busy&&!impl->prepare_take(impl->mes[impl->selected_me]))return false;
        field(impl->live_commands,"DAut",{uint8_t(impl->selected_me),0,0,0});
        impl->manual_active=impl->manual_terminal=false;impl->transition_seen=true;impl->action=MixerAction::automatic;
        impl->dispatched=true;impl->view.busy=true;impl->deadline=now_ms()+20000;return true;
    }
    if((type!=TransitionType::mix&&type!=TransitionType::wipe&&type!=TransitionType::dme&&type!=TransitionType::stinger&&type!=TransitionType::dip)||(type!=TransitionType::stinger&&(frames<1||frames>250))||(type==TransitionType::wipe&&(wipe_pattern(code)<0||soft>100))||(type==TransitionType::dme&&dve_style(code)<0))return false;
    if(!impl->ready())return false;
    auto me=uint8_t(impl->selected_me);const auto& state=impl->mes[me];
    if(type==TransitionType::dme&&impl->caps.value("dves",0u)==0)return false;
    if(type==TransitionType::stinger&&impl->caps.value("stingers",0u)==0)return false;
    if((type==TransitionType::dip&&!impl->usable(2001))||state.preview_transition||!impl->prepare_take(state)||((impl->take_layers&~1u)==0&&state.preview==state.program))return false;
    Bytes b;field(b,"CTTp",{3,me,uint8_t(type==TransitionType::wipe?2:type==TransitionType::dme?3:type==TransitionType::stinger?4:type==TransitionType::dip?1:0),uint8_t(impl->take_layers)});
    if(type==TransitionType::wipe)wipe_settings(b,me,frames,code,reverse,soft,true,state.prepared_wipe_mask,state.prepared_border,state.prepared_symmetry,state.prepared_x,state.prepared_y);
    else if(type==TransitionType::dme)dve_settings(b,me,frames,code,reverse,true);
    else if(type==TransitionType::dip){Bytes d(8,0);d[0]=3;d[1]=me;d[2]=uint8_t(frames);put(d,4,2001);field(b,"CTDp",d);}
    else if(type==TransitionType::mix)field(b,"CTMx",{me,uint8_t(frames),0,0});
    field(b,"DAut",{me,0,0,0});impl->queue(std::move(b),MixerAction::automatic);return true;
}
bool AtemAdapter::set_me(unsigned index){std::lock_guard<std::mutex> l(impl->mutex);
    if(!impl->view.connected||impl->view.busy||impl->view.transitioning||index>=impl->mes.size())return false;
    impl->selected_me=index;++impl->view.rate_revision;impl->publish();return true;
}
bool AtemAdapter::prepare_wipe_modifier(unsigned id,uint32_t a,uint32_t b){
    if((id==168&&a>100)||(id==170&&a>100)||(id==174&&(a>1000||b>1000))||!wipe_modifier_supported(id))return false;
    std::lock_guard<std::mutex> l(impl->mutex);if(!impl->view.connected||impl->selected_me>=impl->mes.size())return false;
    auto& me=impl->mes[impl->selected_me];
    if(id==168){me.prepared_wipe_mask|=4;me.prepared_border=a*100;}
    else if(id==170){me.prepared_wipe_mask|=16;me.prepared_symmetry=a*100;}
    else {me.prepared_wipe_mask|=192;me.prepared_x=a*10;me.prepared_y=b*10;}
    return true;
}
unsigned AtemAdapter::stinger_slots() const {std::lock_guard<std::mutex> l(impl->mutex);return impl->view.connected&&impl->caps.value("stingers",0u)>0?1u:0u;}
bool AtemAdapter::supports_dme() const {std::lock_guard<std::mutex> l(impl->mutex);return impl->view.connected&&impl->caps.value("dves",0u)>0;}
uint32_t AtemAdapter::wipe_preset(unsigned index) const {return index<10?MixerAdapter::wipe_preset(index):0;}
const char* AtemAdapter::wipe_name(uint32_t code) const {for(unsigned i=0;i<8;i++)if(code==800+extra_patterns[i])return extra_names[i];return nullptr;}
bool AtemAdapter::valid_wipe_code(uint32_t code) const {return wipe_pattern(code)>=0;}
bool AtemAdapter::set_rate(uint32_t frames){return set_transition_rate(TransitionType::mix,frames);}
bool AtemAdapter::set_transition_rate(TransitionType type,uint32_t frames){
    if(frames<1||frames>250||(type!=TransitionType::mix&&type!=TransitionType::wipe&&type!=TransitionType::dme&&type!=TransitionType::dip))return false;
    std::lock_guard<std::mutex> l(impl->mutex);if(!impl->ready())return false;
    if(type==TransitionType::dme&&impl->caps.value("dves",0u)==0)return false;
    Bytes b;if(type==TransitionType::dip){Bytes d(8,0);d[0]=1;d[1]=uint8_t(impl->selected_me);d[2]=uint8_t(frames);field(b,"CTDp",d);}
    else if(type==TransitionType::dme){Bytes d(20,0);put(d,0,1);d[2]=uint8_t(impl->selected_me);d[3]=uint8_t(frames);field(b,"CTDv",d);}
    else if(type==TransitionType::wipe){Bytes d(20,0);put(d,0,1);d[2]=uint8_t(impl->selected_me);d[3]=uint8_t(frames);field(b,"CTWp",d);}
    else field(b,"CTMx",{uint8_t(impl->selected_me),uint8_t(frames),0,0});
    impl->queue(std::move(b),MixerAction::none);impl->rate_only=true;impl->wipe_rate_only=type==TransitionType::wipe;impl->dve_rate_only=type==TransitionType::dme;impl->dip_rate_only=type==TransitionType::dip;impl->requested_rate=frames;return true;
}
std::string AtemAdapter::web_status(){std::lock_guard<std::mutex> l(impl->mutex);Json list=Json::array(),mes=Json::array(),keys=Json::array(),media=Json::array();for(const auto& [i,f]:impl->stills)media.push_back({{"index",i},{"used",f.first},{"name",f.second}});for(const auto& [id,i]:impl->inputs)list.push_back({{"number",id},{"key",std::to_string(id)},{"title",i.title},{"shortName",i.short_name},{"meAvailability",i.me_mask},{"sourceAvailability",i.availability},{"kind",i.kind}});
    for(unsigned index=0;index<impl->mes.size();index++){const auto& me=impl->mes[index];mes.push_back({{"index",index},{"transitionPreviewKnown",me.preview_known},{"transitionPreview",me.preview_transition},{"dipKnown",me.dip_known},{"dipFrames",me.dip_rate},{"dipInput",me.dip_input},{"dveKnown",me.dve_known},{"dveFrames",me.dve_rate},{"dveStyle",me.dve_style},{"dveReverse",me.dve_reverse},{"dveFlipFlop",me.dve_flip},{"wipeKnown",me.wipe_known},{"wipeFrames",me.wipe_rate},{"wipePattern",me.wipe_pattern},{"wipeSoftness",me.wipe_soft},{"wipeReverse",me.wipe_reverse},{"wipeFlipFlop",me.wipe_flip},{"position",me.position},{"nextKnown",me.next_known},{"nextLayers",me.next_layers},{"ftbKnown",me.ftb_known},{"ftbBlack",me.black},{"ftbTransitioning",me.ftb_mixing},{"ftbFrames",me.ftb_rate},{"upstreamKeyers",me.keyers},{"program",me.program},{"preview",me.preview},{"rate",me.rate},{"transitioning",me.transitioning}});}
    for(unsigned m=0;m<impl->mes.size();m++)for(unsigned i=0;i<impl->mes[m].keys.size();i++){
        const auto& k=impl->mes[m].keys[i];keys.push_back({{"me",m},{"index",i},{"known",k.base_known&&k.on_known},{"lumaKnown",k.luma_known},{"onAir",k.on},{"type",k.type},{"fill",k.fill},{"key",k.key},{"clip",k.clip},{"gain",k.gain},{"premultiplied",k.premultiplied},{"invert",k.invert},{"maskKnown",k.mask_known},{"maskEnabled",k.mask},{"dveKnown",k.dve_known},{"dveMaskEnabled",k.dve_mask},{"borderEnabled",k.border},{"shadowEnabled",k.shadow},{"canFly",k.can_fly},{"revision",k.revision},{"transitioning",impl->mes[m].transitioning}});
    }
    Json routes=Json::array();for(const auto& [index,source]:impl->aux_sources){Json choices=Json::array();const bool mini=impl->product.find("ATEM Mini Pro")!=std::string::npos&&impl->caps.value("auxiliaries",0u)==2;for(const auto& [id,input]:impl->inputs)if((input.availability&1)&&(!mini||index!=1||id==10010||id==10011||id==9001))choices.push_back({{"id",id},{"name",input.title}});routes.push_back({{"index",index},{"source",source},{"name",mini?(index?"USB":"HDMI"):"OUTPUT "+std::to_string(index+1)},{"choices",choices}});}
    Json viewers=Json::array();for(const auto& [index,settings]:impl->multiviews){Json windows=Json::array();for(unsigned i=0;i<16;i++){const auto& w=settings.windows[i];if(w.present)windows.push_back({{"index",i},{"source",w.source},{"safeSupported",w.safe_supported},{"metersSupported",w.meters_supported},{"safe",w.safe},{"meters",w.meters}});}viewers.push_back({{"index",index},{"windows",windows}});}
    Json dsks=Json::array();for(unsigned index=0;index<impl->dsks.size();index++){const auto& d=impl->dsks[index];dsks.push_back({{"index",index},{"known",d.state_known&&d.base_known&&d.props_known},{"onAir",d.on},{"transitioning",d.mixing},{"fill",d.fill},{"key",d.key},{"frames",d.rate},{"tie",d.tie},{"premultiplied",d.premultiplied},{"clip",d.clip},{"gain",d.gain},{"invert",d.invert},{"maskKnown",d.mask_known},{"maskEnabled",d.mask},{"revision",d.revision}});}
    Json modes=Json::array();for(auto mode:impl->formats.supported)modes.push_back({{"id",mode},{"name",video_format_name(mode)}});
    return Json({{"videoFormat",impl->formats.current},{"videoFormats",modes},{"outputs",routes},{"outputKnown",impl->view.output_known},{"outputActive",impl->view.output_active},{"multiviews",viewers},{"media",{{"programKnown",impl->view.media_program_known},{"onProgram",impl->view.media_on_program},{"clearStatus",impl->view.media_clear_status},{"clearSlot",impl->view.media_clear_slot},{"captureSupported",impl->capture_supported()},{"captureStatus",impl->view.media_capture_status},{"captureSlot",impl->view.media_capture_slot},{"known",impl->view.media_known},{"still",impl->view.media_still},{"slots",impl->still_slots},{"stills",media}}},{"keys",keys},{"dsks",dsks},{"connected",impl->view.connected},{"busy",impl->view.busy},{"transitioning",impl->view.transitioning},{"commandFailures",impl->view.command_failures},{"message",impl->view.message.data()},{"product",impl->product},{"protocolVersion",impl->version},{"capabilities",impl->caps},{"mixEffects",mes},{"inputs",list},{"preview",impl->view.preview},{"program",impl->view.program},{"revision",impl->revision},{"selectedMe",impl->selected_me},{"nextMeSource",impl->source_at(11)},{"profile",profile_json(impl->profile)}}).dump(-1,' ',false,Json::error_handler_t::replace);
}
bool AtemAdapter::save_mappings(const std::string& body,std::string& error){try{auto j=Json::parse(body);auto profile=parse_profile(j.at("profile"));std::lock_guard<std::mutex> l(impl->mutex);
    if(!j.at("revision").is_number_unsigned()||j.at("revision")!=impl->revision){error="Mappings changed; reload";return false;}
    if(impl->view.busy||std::any_of(impl->mes.begin(),impl->mes.end(),[](const ME& me){return me.transitioning;})){error="Mixer busy";return false;}
    if(impl->initialized&&!impl->mes.empty()&&profile.me>=impl->mes.size())profile.me=0;
    std::string pattern=impl->path+".tmp-XXXXXX";std::vector<char> name(pattern.begin(),pattern.end());name.push_back(0);int fd=mkstemp(name.data());if(fd<0){error="Cannot save mappings";return false;}
    auto text=profile_json(profile).dump(2)+"\n";size_t at=0;while(at<text.size()){auto n=write(fd,text.data()+at,text.size()-at);if(n<0&&errno==EINTR)continue;if(n<=0)break;at+=size_t(n);}bool ok=at==text.size()&&fsync(fd)==0;close(fd);
    if(!ok||rename(name.data(),impl->path.c_str())<0){unlink(name.data());error="Cannot save mappings";return false;}
    impl->profile=profile;impl->invalid_profile=false;++impl->revision;++impl->view.rate_revision;impl->publish();return true;
}catch(const std::exception&){error="Invalid ATEM mappings";return false;}}
}

namespace bkds::link {
unsigned AtemAdapter::dsk_channels() const {std::lock_guard<std::mutex> l(impl->mutex);return impl->initialized?unsigned(impl->dsks.size()):0;}
bool AtemAdapter::dsk(bool mix,uint32_t frames,unsigned slot){
    std::lock_guard<std::mutex> l(impl->mutex);
    if(!impl->ready()||slot>=impl->dsks.size()||(mix&&(frames<1||frames>250)))return false;
    const auto& d=impl->dsks[slot];if(!d.state_known||d.mixing)return false;
    const auto index=uint8_t(slot);Bytes b;bool target=!d.on;
    if(mix){field(b,"CDsR",{index,uint8_t(frames),0,0});
        // Since protocol 2.29 DDsA has mask/index/direction, not index at byte 0.
        field(b,"DDsA",impl->version>=0x0002001d?Bytes{1,index,uint8_t(target),0}:Bytes{index,0,0,0});
    }else field(b,"CDsL",{index,uint8_t(target),0,0});
    impl->queue(std::move(b),mix?MixerAction::dsk_mix:MixerAction::dsk_cut);impl->dsk_op=Impl::DskOp::toggle;impl->dsk_target=slot;impl->dsk_target_on=target;return true;
}
bool AtemAdapter::set_dsk_rate(uint32_t frames,unsigned slot){
    std::lock_guard<std::mutex> l(impl->mutex);
    if(!impl->ready()||slot>=impl->dsks.size()||impl->dsks[slot].mixing||frames<1||frames>250)return false;
    Bytes b;field(b,"CDsR",{uint8_t(slot),uint8_t(frames),0,0});impl->queue(std::move(b),MixerAction::none);impl->dsk_op=Impl::DskOp::rate;impl->dsk_target=slot;impl->requested_rate=frames;return true;
}
bool AtemAdapter::configure_dsk(const std::string& body,std::string& error){try{
    auto j=Json::parse(body);auto integer=[&](const char* key,unsigned maximum){const auto& v=j.at(key);if(!v.is_number_unsigned()||v>maximum)throw Failure("Invalid DSK value");return v.get<unsigned>();};
    auto boolean=[&](const char* key){if(!j.at(key).is_boolean())throw Failure("Invalid DSK flag");return j.at(key).get<bool>();};
    const auto slot=integer("index",255),revision=integer("revision",0xffffffffu);
    DSK next;next.fill=integer("fill",65535);next.key=integer("key",65535);next.rate=integer("frames",250);if(!next.rate)throw Failure("Invalid DSK duration");
    next.clip=integer("clip",1000);next.gain=integer("gain",1000);next.tie=boolean("tie");next.premultiplied=boolean("premultiplied");next.invert=boolean("invert");
    std::lock_guard<std::mutex> l(impl->mutex);
    if(!impl->ready()||slot>=impl->dsks.size()){error="ATEM unavailable or busy";return false;}
    const auto& d=impl->dsks[slot];if(!d.state_known||!d.base_known||!d.props_known||d.mixing||d.on){error="Take the DSK off air before changing its configuration";return false;}
    if(d.revision!=revision){error="DSK settings changed; reload";return false;}
    auto fill=impl->inputs.find(next.fill),key=impl->inputs.find(next.key);
    if(fill==impl->inputs.end()||!(fill->second.me_mask&1)||key==impl->inputs.end()||!(key->second.availability&16)){error="Source unavailable for DSK fill/key";return false;}
    auto index=uint8_t(slot);Bytes b,gain(12);gain[0]=15;gain[1]=index;gain[2]=next.premultiplied;atem::put(gain,4,next.clip);atem::put(gain,6,next.gain);gain[8]=next.invert;
    field(b,"CDsF",{index,0,uint8_t(next.fill>>8),uint8_t(next.fill)});field(b,"CDsC",{index,0,uint8_t(next.key>>8),uint8_t(next.key)});field(b,"CDsR",{index,uint8_t(next.rate),0,0});field(b,"CDsT",{index,uint8_t(next.tie),0,0});field(b,"CDsG",gain);
    impl->queue(std::move(b),MixerAction::none);impl->dsk_op=Impl::DskOp::settings;impl->dsk_target=slot;impl->expected_dsk=next;return true;
}catch(const std::exception&){error="Invalid DSK configuration";return false;}}
}

namespace bkds::link {
bool AtemAdapter::fade_to_black(uint32_t frames){
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(!impl->ready()||frames<1||frames>250)return false;
    const auto& me=impl->mes[impl->selected_me];if(!me.ftb_known||me.ftb_mixing)return false;
    const auto index=uint8_t(impl->selected_me);Bytes b;
    field(b,"FtbC",{1,index,uint8_t(frames),0});field(b,"FtbA",{index,0,0,0});
    impl->queue(std::move(b),MixerAction::none);impl->ftb_op=Impl::FtbOp::toggle;impl->ftb_target=!me.black;return true;
}
bool AtemAdapter::set_ftb_rate(uint32_t frames){
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(!impl->ready()||frames<1||frames>250)return false;
    const auto& me=impl->mes[impl->selected_me];if(!me.ftb_known||me.ftb_mixing)return false;
    Bytes b;field(b,"FtbC",{1,uint8_t(impl->selected_me),uint8_t(frames),0});
    impl->queue(std::move(b),MixerAction::none);impl->ftb_op=Impl::FtbOp::rate;impl->requested_rate=frames;return true;
}
}

namespace bkds::link {
bool AtemAdapter::toggle_key(unsigned slot){
    std::lock_guard<std::mutex> lock(impl->mutex);if(!impl->ready())return false;
    const auto& me=impl->mes[impl->selected_me];if(slot>=me.keys.size()||!me.keys[slot].on_known)return false;
    const auto on=!me.keys[slot].on;Bytes b;field(b,"CKOn",{uint8_t(impl->selected_me),uint8_t(slot),uint8_t(on),0});
    impl->queue(std::move(b),MixerAction::none);impl->key_op=Impl::KeyOp::toggle;impl->key_target_me=impl->selected_me;impl->key_target=slot;impl->key_target_on=on;return true;
}
bool AtemAdapter::set_next_transition(bool background,const std::array<bool,4>& keys){
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(!impl->view.connected||impl->invalid_profile||impl->selected_me>=impl->mes.size())return false;
    const auto& me=impl->mes[impl->selected_me];if(me.transitioning||!me.next_known)return false;
    unsigned layers=background?1:0;
    for(unsigned i=0;i<4;i++)if(keys[i]){if(i>=me.keys.size())return false;layers|=2u<<i;}
    if(!layers)return false;
    if(impl->view.busy){if(impl->key_op!=Impl::KeyOp::next||impl->key_target_me!=impl->selected_me)return false;impl->next_desired=layers;return true;}
    Bytes b;field(b,"CTTp",{2,uint8_t(impl->selected_me),0,uint8_t(layers)});impl->queue(std::move(b),MixerAction::none);
    impl->key_op=Impl::KeyOp::next;impl->key_target_me=impl->selected_me;impl->next_desired=impl->next_sent=layers;return true;
}
bool AtemAdapter::toggle_next_background(){auto s=state();auto b=!s.next_background;if(!b&&!std::any_of(s.next_key.begin(),s.next_key.end(),[](bool k){return k;}))b=true;return s.next_known&&set_next_transition(b,s.next_key);}
bool AtemAdapter::toggle_next_key(unsigned slot){if(slot>=4)return false;auto s=state();if(!s.next_known||!s.key_available[slot])return false;auto k=s.next_key;k[slot]=!k[slot];auto b=s.next_background;if(!b&&!std::any_of(k.begin(),k.end(),[](bool on){return on;}))b=true;return set_next_transition(b,k);}
bool AtemAdapter::configure_key(const std::string& body,std::string& error){try{
    const auto j=Json::parse(body);auto number=[&](const char* key,unsigned maximum){const auto& v=j.at(key);if(!v.is_number_unsigned()||v>maximum)throw Failure("Invalid keyer value");return v.get<unsigned>();};
    auto flag=[&](const char* key){const auto& v=j.at(key);if(!v.is_boolean())throw Failure("Invalid keyer flag");return v.get<bool>();};
    const auto m=number("me",255),i=number("index",255),revision=number("revision",0xffffffffu);USK desired;
    desired.type=number("type",3);desired.fill=number("fill",65535);desired.key=number("key",65535);
    desired.clip=number("clip",1000);desired.gain=number("gain",1000);desired.premultiplied=flag("premultiplied");desired.invert=flag("invert");
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(!impl->ready()||m>=impl->mes.size()||i>=impl->mes[m].keys.size()){error="ATEM unavailable or busy";return false;}
    const auto& me=impl->mes[m];const auto& k=me.keys[i];
    if(!k.base_known||!k.on_known||k.on||me.transitioning){error="Take the keyer off air before changing its configuration";return false;}
    if(k.revision!=revision){error="Keyer settings changed; reload";return false;}
    if(desired.type==0&&!k.luma_known){error="Luma settings unavailable";return false;}
    if(desired.type==3&&!impl->caps.value("dves",0)){error="DVE keyer unavailable";return false;}
    const auto fill=impl->inputs.find(desired.fill),cut=impl->inputs.find(desired.key);
    if(m>=8||fill==impl->inputs.end()||!(fill->second.me_mask&(1u<<m))||cut==impl->inputs.end()||!(cut->second.availability&16)){error="Source unavailable for keyer fill/key";return false;}
    Bytes b;field(b,"CKeF",{uint8_t(m),uint8_t(i),uint8_t(desired.fill>>8),uint8_t(desired.fill)});field(b,"CKeC",{uint8_t(m),uint8_t(i),uint8_t(desired.key>>8),uint8_t(desired.key)});
    if(k.type!=desired.type)field(b,"CKTp",{1,uint8_t(m),uint8_t(i),uint8_t(desired.type),0,0,0,0});
    if(desired.type==0){Bytes luma(12);luma[0]=15;luma[1]=uint8_t(m);luma[2]=uint8_t(i);luma[3]=desired.premultiplied;atem::put(luma,4,desired.clip);atem::put(luma,6,desired.gain);luma[8]=desired.invert;field(b,"CKLm",luma);}
    impl->queue(std::move(b),MixerAction::none);impl->key_op=Impl::KeyOp::settings;impl->key_target_me=m;impl->key_target=i;impl->expected_key=desired;return true;
}catch(const std::exception&){error="Invalid keyer configuration";return false;}}
}

namespace bkds::link {
bool AtemAdapter::output(OutputKind kind){
    std::lock_guard<std::mutex> lock(impl->mutex);const unsigned i=unsigned(kind);
    if(!impl->ready()||i>=2||!impl->view.output_known[i])return false;
    const unsigned status=impl->output_status[i];const bool enable=!impl->view.output_active[i];
    if(i==0&&(status&(2|32|16|32768)))return false;
    if(i==1&&((status&128)||(enable&&!(status&2))))return false;
    Bytes b;field(b,i?"RcTM":"StrR",{uint8_t(enable),0,0,0});impl->queue(std::move(b),MixerAction::output);
    impl->output_op=true;impl->output_target=i;impl->output_target_on=enable;return true;
}
bool AtemAdapter::output_routes(OutputRoutes& result)const{
    std::lock_guard<std::mutex> lock(impl->mutex);if(!impl->view.connected)return false;
    result=OutputRoutes{};result.count=std::min(16u,impl->caps.value("auxiliaries",0u));result.revision=impl->aux_revision;
    for(unsigned i=0;i<result.count;i++)std::snprintf(result.labels[i].data(),8,"OUT%u",(i%16)+1);
    if(impl->product.find("ATEM Mini")!=std::string::npos&&result.count==2){std::snprintf(result.labels[0].data(),8,"HDMI");std::snprintf(result.labels[1].data(),8,"USB");}
    for(const auto& [i,source]:impl->aux_sources)if(i<16)result.sources[i]=int(source);
    for(unsigned route=0;route<result.count;route++)for(const auto& [id,input]:impl->inputs){
        const bool usb=impl->product.find("ATEM Mini Pro")!=std::string::npos&&result.count==2&&route==1;
        if(!(input.availability&1)||(usb&&id!=10010&&id!=10011&&id!=9001))continue;
        OutputSource source;source.id=id;std::snprintf(source.name.data(),source.name.size(),"%s",input.title.c_str());result.choices[route].push_back(source);
    }
    if(impl->inputs.count(10010)&&(impl->inputs.at(10010).availability&1))result.program=10010;
    if(impl->inputs.count(9001)&&(impl->inputs.at(9001).availability&1))result.multiview=9001;
    return result.count>0;
}
bool AtemAdapter::set_output_route(unsigned index,bool multiview){
    return set_output_source(index,multiview?9001:10010);
}
bool AtemAdapter::set_output_source(unsigned index,unsigned source){
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(impl->product.find("ATEM Mini Pro")!=std::string::npos&&impl->caps.value("auxiliaries",0u)==2&&index==1&&source!=10010&&source!=10011&&source!=9001)return false;
    const auto input=impl->inputs.find(source);
    if(!impl->ready()||index>=impl->caps.value("auxiliaries",0u)||!impl->aux_sources.count(index)||input==impl->inputs.end()||!(input->second.availability&1))return false;
    Bytes b;field(b,"CAuS",{1,uint8_t(index),uint8_t(source>>8),uint8_t(source)});impl->queue(std::move(b),MixerAction::none);
    impl->route_op=true;impl->route_target=index;impl->route_source=source;return true;
}
bool AtemAdapter::multiview_settings(unsigned index,MultiviewSettings& result)const{
    std::lock_guard<std::mutex> lock(impl->mutex);
    const unsigned count=impl->caps.value("multiviewers",0u);
    if(!impl->view.connected||index>=count)return false;
    const auto it=impl->multiviews.find(index);result=it==impl->multiviews.end()?MultiviewSettings{}:it->second;
    for(auto& w:result.windows){if(!w.safe_supported)w.safe=-1;if(!w.meters_supported)w.meters=-1;}
    result.count=count;result.revision=impl->multiview_revision;return true;
}
bool AtemAdapter::set_multiview_window(unsigned index,unsigned window,bool safe,bool enabled){
    std::lock_guard<std::mutex> lock(impl->mutex);
    const auto it=impl->multiviews.find(index);
    if(!impl->ready()||index>=impl->caps.value("multiviewers",0u)||window>=16||it==impl->multiviews.end())return false;
    const auto& w=it->second.windows[window];if(!w.present||!(safe?w.safe_supported:w.meters_supported)||(safe?w.safe:w.meters)<0)return false;
    Bytes b;field(b,safe?"SaMw":"VuMS",{uint8_t(index),uint8_t(window),uint8_t(enabled),0});impl->queue(std::move(b),MixerAction::none);
    impl->multiview_pending.reset();impl->multiview_pending.set(window);impl->multiview_op=true;impl->multiview_target=index;impl->multiview_window=window;impl->multiview_safe=safe;impl->multiview_enabled=enabled;return true;
}
bool AtemAdapter::supports_media_capture()const{std::lock_guard<std::mutex> lock(impl->mutex);return impl->capture_supported();}
bool AtemAdapter::capture_media_still(){
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(!impl->ready()||!impl->capture_supported()||impl->capture_pending||impl->clear_pending||!impl->media_known||!impl->still_slots)return false;
    for(unsigned i=0;i<impl->still_slots;i++)if(!impl->still_fingerprints.count(i))return false;
    Bytes b;field(b,"Capt",{});impl->queue(std::move(b),MixerAction::none);
    impl->capture_command=impl->capture_pending=true;impl->capture_dispatched=false;impl->capture_changes.clear();
    impl->view.media_capture_status=1;impl->view.media_capture_slot=-1;return true;
}
bool AtemAdapter::clear_media_still(unsigned index){
    std::lock_guard<std::mutex> lock(impl->mutex);
    const auto f=impl->stills.find(index);
    // Only MP1 is tracked. With additional players we cannot protect their
    // selections yet. Protect MP1 when on program or tally unknown. Serialize capture/clear to avoid attribution races.
    if(!impl->ready()||impl->caps.value("mediaPlayers",0u)!=1||!impl->media_known||
       impl->capture_pending||impl->clear_pending||index>=impl->still_slots||index>255||
       f==impl->stills.end()||!f->second.first||!impl->media_safe_to_clear(index))return false;
    Bytes b;field(b,"CSTL",{uint8_t(index),0,0,0});impl->queue(std::move(b),MixerAction::none);
    impl->clear_command=impl->clear_pending=true;impl->clear_slot=index;impl->clear_until=now_ms()+5000;
    impl->view.media_clear_status=1;impl->view.media_clear_slot=int(index);return true;
}
bool AtemAdapter::select_media_still(unsigned index){
    std::lock_guard<std::mutex> lock(impl->mutex);
    const auto f=impl->stills.find(index);
    if(!impl->ready()||impl->caps.value("mediaPlayers",0u)==0||!impl->media_known||index>=impl->still_slots||(impl->clear_pending&&index==impl->clear_slot)||f==impl->stills.end()||!f->second.first)return false;
    Bytes b;field(b,"MPSS",{3,0,1,uint8_t(index),0,0,0,0});
    impl->queue(std::move(b),MixerAction::none);impl->media_op=true;impl->media_desired=index;return true;
}
bool AtemAdapter::key_setting(unsigned delegation,unsigned dsk_slot,unsigned id,int source){
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(!impl->ready()||delegation>4||dsk_slot>1)return false;
    const bool dsk=delegation==4;const unsigned index=dsk?dsk_slot:delegation;
    if(dsk?index>=impl->dsks.size():index>=impl->mes[impl->selected_me].keys.size())return false;
    DSK d;USK k;unsigned fill,cut,type=0;bool premult;
    if(dsk){d=impl->dsks[index];if(!d.base_known||!d.props_known||!d.state_known||d.on||d.mixing)return false;fill=d.fill;cut=d.key;premult=d.premultiplied;}
    else{k=impl->mes[impl->selected_me].keys[index];if(!k.base_known||!k.on_known||k.on||(k.type==0&&!k.luma_known))return false;fill=k.fill;cut=k.key;premult=k.premultiplied;type=k.type;}
    if(id==102&&!dsk&&type==2){
        if(!k.pattern_known)return false;
        k.pattern_invert=!k.pattern_invert;Bytes b,data(16,0);data[0]=64;data[1]=uint8_t(impl->selected_me);data[2]=uint8_t(index);data[14]=k.pattern_invert;field(b,"CKPt",data);
        impl->queue(std::move(b),MixerAction::none);impl->key_op=Impl::KeyOp::decorations;impl->key_target_me=impl->selected_me;impl->key_target=index;impl->expected_key=k;return true;
    }
    if(id==100||id==101||id==96||id==98){
        Bytes b;const auto m=uint8_t(impl->selected_me),i=uint8_t(index);
        if(dsk){
            if(id!=100||!d.mask_known)return false;
            d.mask=!d.mask;Bytes data(12,0);data[0]=1;data[1]=i;data[2]=d.mask;field(b,"CDsM",data);
            impl->queue(std::move(b),MixerAction::none);impl->dsk_op=Impl::DskOp::mask;impl->dsk_target=index;impl->expected_dsk=d;
        }else{
            if(id==100){if(!k.mask_known)return false;k.mask=!k.mask;Bytes data(12,0);data[0]=1;data[1]=m;data[2]=i;data[3]=k.mask;field(b,"CKMs",data);}
            else{
                if(k.type!=3||!k.dve_known||!impl->caps.value("dves",0u))return false;
                Bytes data(64,0);const unsigned bit=id==96?5:id==98?6:20;const uint32_t mask=1u<<bit;
                data[0]=uint8_t(mask>>24);data[1]=uint8_t(mask>>16);data[2]=uint8_t(mask>>8);data[3]=uint8_t(mask);
                data[4]=m;data[5]=i;if(id==96){k.border=!k.border;data[28]=k.border;}else if(id==98){k.shadow=!k.shadow;data[29]=k.shadow;}else{k.dve_mask=!k.dve_mask;data[51]=k.dve_mask;}
                field(b,"CKDV",data);
            }
            impl->queue(std::move(b),MixerAction::none);impl->key_op=Impl::KeyOp::decorations;impl->key_target_me=impl->selected_me;impl->key_target=index;impl->expected_key=k;
        }return true;
    }
    auto paired=[&](unsigned value){auto i=impl->inputs.find(value);if(i!=impl->inputs.end()&&i->second.kind==4&&impl->inputs.count(value+1)&&(impl->inputs.at(value+1).availability&16))return value+1;return value;};
    if(id==67||id==70){
        if(source<0||source>=24)return false;
        const int mapped=impl->source_at(unsigned(source));if(mapped<0)return false;
        const unsigned value=unsigned(mapped);const auto input=impl->inputs.find(value);if(input==impl->inputs.end())return false;
        const unsigned owner=dsk?0:impl->selected_me;if(id==67&&(owner>=8||!(input->second.me_mask&(1u<<owner))))return false;
        if(id==70){if(!(impl->inputs.at(value).availability&16))return false;cut=value;}
        else{if(cut==fill)cut=value;else if(cut==paired(fill))cut=paired(value);fill=value;}
    }else if(id==68)cut=fill;
    else if(id==69)cut=paired(fill);
    else if((id>=88&&id<=92)||id==106){
        if(id==89)return false;
        if(id==106&&(dsk||!impl->caps.value("dves",0u)))return false;
        if(id>=91&&dsk)return false;
        type=id==106?3:id==91?1:id==92?2:0;
        if(id==90)premult=!premult;
    }else if(id==102){if(type!=0)return false;if(dsk)d.invert=!d.invert;else k.invert=!k.invert;}
    else return false;
    const auto cut_input=impl->inputs.find(cut);if(cut_input==impl->inputs.end()||!(cut_input->second.availability&16))return false;
    Bytes b;const auto m=uint8_t(impl->selected_me),i=uint8_t(index);
    if(dsk){
        d.fill=fill;d.key=cut;d.premultiplied=premult;
        field(b,"CDsF",{i,0,uint8_t(fill>>8),uint8_t(fill)});field(b,"CDsC",{i,0,uint8_t(cut>>8),uint8_t(cut)});
        Bytes g(12,0);g[0]=15;g[1]=i;g[2]=premult;put(g,4,d.clip);put(g,6,d.gain);g[8]=d.invert;field(b,"CDsG",g);
        impl->queue(std::move(b),MixerAction::none);impl->dsk_op=Impl::DskOp::settings;impl->dsk_target=index;impl->expected_dsk=d;
    }else{
        if(type==0&&!k.luma_known)return false;
        k.fill=fill;k.key=cut;k.premultiplied=premult;k.type=type;
        field(b,"CKeF",{m,i,uint8_t(fill>>8),uint8_t(fill)});field(b,"CKeC",{m,i,uint8_t(cut>>8),uint8_t(cut)});
        if(type!=impl->mes[impl->selected_me].keys[index].type)field(b,"CKTp",{1,m,i,uint8_t(type),0,0,0,0});
        if(type==0){Bytes g(12,0);g[0]=15;g[1]=m;g[2]=i;g[3]=premult;put(g,4,k.clip);put(g,6,k.gain);g[8]=k.invert;field(b,"CKLm",g);}
        impl->queue(std::move(b),MixerAction::none);impl->key_op=Impl::KeyOp::settings;impl->key_target_me=impl->selected_me;impl->key_target=index;impl->expected_key=k;
    }
    return true;
}
}

namespace bkds::link {
bool AtemAdapter::video_formats(VideoFormats& out) const {std::lock_guard<std::mutex> lock(impl->mutex);out=impl->formats;return impl->view.connected&&out.current>=0&&!out.supported.empty();}
bool AtemAdapter::set_video_format(unsigned mode,int previous){
    std::lock_guard<std::mutex> lock(impl->mutex);
    if(!impl->ready()||impl->formats.current!=previous||std::find(impl->formats.supported.begin(),impl->formats.supported.end(),mode)==impl->formats.supported.end()||std::any_of(impl->mes.begin(),impl->mes.end(),[](const ME& me){return me.transitioning;})||std::any_of(impl->view.output_active.begin(),impl->view.output_active.end(),[](bool on){return on;}))return false;
    if(int(mode)==previous)return true;
    Bytes b;field(b,"CVdM",{uint8_t(mode),0,0,0});impl->queue(std::move(b),MixerAction::none);impl->format_op=true;impl->format_target=mode;return true;
}
}

namespace bkds::link {
bool AtemAdapter::set_multiview_inputs(unsigned index,bool safe,bool enabled){
    std::lock_guard<std::mutex> lock(impl->mutex);const auto it=impl->multiviews.find(index);
    if(!impl->ready()||it==impl->multiviews.end())return false;
    Bytes bytes;std::bitset<16> targets;
    for(unsigned i=0;i<16;i++){const auto& w=it->second.windows[i];if(!multiview_input(w)||!(safe?w.safe_supported:w.meters_supported)||(safe?w.safe:w.meters)<0)continue;
        targets.set(i);field(bytes,safe?"SaMw":"VuMS",{uint8_t(index),uint8_t(i),uint8_t(enabled),0});}
    if(targets.none())return false;
    impl->queue(std::move(bytes),MixerAction::none);impl->multiview_op=true;impl->multiview_pending=targets;impl->multiview_target=index;impl->multiview_safe=safe;impl->multiview_enabled=enabled;return true;
}
}

namespace bkds::link {
bool AtemAdapter::server_info(ServerInfo& out)const{std::lock_guard<std::mutex> lock(impl->mutex);out.fields={{"MODEL",impl->product.empty()?"NOT REPORTED":impl->product},{"PROTOCOL",impl->version?std::to_string(impl->version>>16)+"."+std::to_string(impl->version&65535):"NOT REPORTED"},{"FIRMWARE","NOT REPORTED"},{"M/E",std::to_string(impl->mes.size())},{"INPUTS",std::to_string(impl->inputs.size())},{"LINK",impl->view.connected?"CONNECTED":"DISCONNECTED"}};return true;}
}

namespace bkds::link {
bool AtemAdapter::valid_dme_code(uint32_t code)const {std::lock_guard<std::mutex> lock(impl->mutex);return impl->view.connected&&impl->caps.value("dves",0u)>0&&((code>=2601&&code<=2608)||(code>=2621&&code<=2628));}
bool AtemAdapter::keypad_transition_available(TransitionType type,unsigned code)const {std::lock_guard<std::mutex> lock(impl->mutex);if(!impl->view.connected)return false;if(type==TransitionType::dme)return impl->caps.value("dves",0u)>0&&code<10&&dve_style(code)>=0;if(type==TransitionType::stinger)return code==1&&impl->caps.value("stingers",0u)>0;return true;}
}
