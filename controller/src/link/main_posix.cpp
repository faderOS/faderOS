#include "platform.hpp"
#ifndef FADEROS_PLATFORM_POSIX
#error "main_posix.cpp belongs to the Linux runtime"
#endif
#include "analog.hpp"
#include "core.hpp"
#include "panel.hpp"
#include "setup.hpp"
#include "home.hpp"
#include "obs_posix.hpp"
#include "kavtor_posix.hpp"
#include "vmix_posix.hpp"
#include "atem_posix.hpp"
#include "adapter_router.hpp"
#include "transitions.hpp"
#include "keys.hpp"
#include "keypad.hpp"
#include "web_posix.hpp"
#include <memory>
#include <cstdlib>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
using namespace bkds::link;
extern int set_custom_baud(int fd,unsigned rate);
static volatile std::sig_atomic_t stopping=0;
static void stop(int) { stopping=1; }
class Posix final:public Transport {
    int fd=-1;
    std::string baud_control;
    uint32_t requested_rate=0,applied_rate=0;
    unsigned long request_id=0;
public:
    explicit Posix(const char* path,std::string control):baud_control(std::move(control)) {
        request_id=static_cast<unsigned long>(::getpid())*1000;
        fd=::open(path,O_RDWR|O_NOCTTY|O_NONBLOCK);
        if(fd<0) return;
        termios t{};
        if(tcgetattr(fd,&t)<0) { ::close(fd); fd=-1; return; }
        cfmakeraw(&t); cfsetispeed(&t,B9600); cfsetospeed(&t,B9600); t.c_cflag|=CLOCAL|CREAD; t.c_cflag&=~CRTSCTS;
        if(tcsetattr(fd,TCSANOW,&t)<0) { ::close(fd); fd=-1; }
    }
    int set_baud(uint32_t rate) override {
        // A timed-out bridge request must be superseded even if the local
        // termios rate already matches the fallback. Otherwise it may apply late.
        if(rate==applied_rate&&(baud_control.empty()||requested_rate==rate)) return 1;
        speed_t baud;
        if(rate==9600) baud=B9600;
        else if(rate==19200) baud=B19200;
        else if(rate==38400) baud=B38400;
        else if(rate==76800) baud=B38400; // actual rate set using termios2 below
        else return -1;
        if(!baud_control.empty()) {
            if(requested_rate!=rate) {
                ++request_id; requested_rate=rate; applied_rate=0;
                std::ofstream out(baud_control+"/baud-request.tmp");
                out<<request_id<<' '<<rate<<'\n'; out.close();
                if(!out||std::rename((baud_control+"/baud-request.tmp").c_str(),(baud_control+"/baud-request").c_str())!=0) return -1;
            }
            std::ifstream in(baud_control+"/baud-ack"); unsigned long id=0; uint32_t actual=0;
            if(!(in>>id>>actual)||id!=request_id||actual!=rate) return 0;
        }
        if(rate==76800) {
            // TCSETS2 may change the device even if readback/validation fails.
            // Invalidate the cache BEFORE the attempt so fallback really writes 9600.
            applied_rate=0;
            if(set_custom_baud(fd,rate)<0) { std::perror("SCI2 custom baud"); return -1; }
            applied_rate=rate; return 1;
        }
        termios t{};
        if(tcgetattr(fd,&t)<0) return -1;
        cfsetispeed(&t,baud); cfsetospeed(&t,baud);
        if(tcsetattr(fd,TCSANOW,&t)<0) return -1;
        applied_rate=rate;
        return 1;
    }
    ~Posix() override { if(fd>=0) ::close(fd); }
    bool valid() const { return fd>=0; }
    int read(uint8_t* p,std::size_t n) override {
        auto r=::read(fd,p,n);
        if(r<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)) return 0;
        return r>0?int(r):-1;
    }
    int write(const uint8_t* p,std::size_t n) override {
        auto r=::write(fd,p,n);
        if(r<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)) return 0;
        return r>0?int(r):-1;
    }
};
class Store final:public ConfigStore {
    std::string path;
public:
    explicit Store(std::string p):path(std::move(p)) {}
    bool load(Config& c) {
        std::ifstream in(path);
        return bool(in)&&parse_settings(in,c);
    }
    bool save(const Config& c) override {
        const auto temp=path+".tmp";
        std::ofstream out(temp,std::ios::trunc);
        if(!out||!write_settings(out,c)) return false;
        out.close(); if(!out) return false;
        return std::rename(temp.c_str(),path.c_str())==0;
    }
};
class Application final:public Listener {
public:
    Panel panel;
    Inputs inputs;
    EncoderSteps encoders;
    Configuration configuration;
    Setup setup;
    MixerAdapter& mixer_adapter;
    MixerControl mixer;
    std::unique_ptr<TransitionControl> transition_control;
    TransitionControl& transitions() { return *transition_control; }
    const Mappings& obs_mappings;
    std::array<Mappings,3>* server_mappings=nullptr;int requested_server=-1;
    Mappings& live_mappings;
    Config applied_config;
    KeyControl key_control;
    bool mixer_enabled=false;
    explicit Application(ConfigStore& store,const Config& c,MixerAdapter& adapter,Mappings& m,const Mappings& obs_profile,bool enabled)
        :configuration(store,c),setup(panel,configuration),mixer_adapter(adapter),mixer(panel,adapter,m),transition_control(new TransitionControl(panel,adapter)),obs_mappings(obs_profile),live_mappings(m),applied_config(c),key_control(panel,adapter),mixer_enabled(enabled) { setup.attach_mixer(&adapter); auto settings=m.transitions;if(c.backend!=Backend::obs)settings.softness=adapter.default_softness();transitions().defaults(settings); }
    AnalogCalibration calibration;
    std::string calibration_path;
    uint16_t raw_tbar=0;unsigned raw_x=128,raw_y=128;
    std::array<bool,2> analog_valid{};
    std::array<uint32_t,2> analog_count{};
    bool analog_watch=false;
    bool verbose=false;
    std::array<uint32_t,2> analog_missing{};
    std::array<uint32_t,8> last_diag{};
    bool have_diag=false;
    std::string firmware_version,firmware_date;
    bool panel_connected=false;
    void firmware_info(const char* version,const char* date) override {
        firmware_version=version;firmware_date=date;
        setup.firmware_info(version);
        if(*version)std::cout<<"FIRMWARE "<<version<<" / "<<date<<'\n';
    }
    void analog_gap(unsigned axis,uint16_t missed) override {analog_missing[axis]+=missed;}
    void diagnostic(const std::array<uint32_t,8>& d) override {
        if(verbose&&have_diag) {
            const uint32_t elapsed=d[0]-last_diag[0];
            if(elapsed&&elapsed<10000) {
                auto rate=[&](unsigned i) {return uint64_t(uint32_t(d[i]-last_diag[i]))*1000/elapsed;};
                std::cout<<"PANEL Hz scan="<<rate(2)<<" loop="<<rate(1)
                    <<" changes="<<rate(4)<<'/'<<rate(5)<<" TX B/s="<<rate(3)
                    <<" UART errors="<<uint32_t(d[6]-last_diag[6])<<" max scan gap ms="<<d[7]<<'\n';
            }
        }
        last_diag=d;have_diag=true;
    }
    void analog(unsigned axis,uint16_t sample_ms,uint16_t value) override {
        analog_valid[axis]=true;analog_count[axis]++;
        if(!axis) {
            raw_tbar=value;
            if(mixer_enabled&&!setup.testing()) transitions().tbar(uint16_t(calibration.tbar_value(raw_tbar)));
        } else {
            raw_x=value>>8;raw_y=value&255;
            if(mixer_enabled&&!setup.testing()) transitions().joystick(calibration.x_value(int(raw_x)),calibration.y_value(int(raw_y)));
        }
        if(setup.testing())setup.test_analog(axis,value);
        if(analog_watch) {
            std::cout<<"SAMPLE axis="<<axis<<" panel_ms="<<sample_ms<<' ';
            analog_report();
        }
    }
    void analog_report() {
        std::cout<<"ANALOG RAW "<<raw_tbar<<' '<<raw_x<<' '<<raw_y
                 <<" CAL "<<calibration.tbar_value(raw_tbar)<<' '
                 <<calibration.x_value(int(raw_x))<<' '<<calibration.y_value(int(raw_y))<<'\n';
    }
    bool calibrate(const std::string& axis,const std::string& endpoint) {
        if((axis!="tbar"&&axis!="x"&&axis!="y")||!analog_valid[axis=="tbar"?0:1]) return false;
        auto next=calibration;
        auto& range=axis=="tbar"?next.tbar:axis=="x"?next.x:next.y;
        int value=axis=="tbar"?calibration.tbar_unwrap(raw_tbar):axis=="x"?raw_x:raw_y;
        if(endpoint=="min") range.low=value;else if(endpoint=="max") range.high=value;
        else if(endpoint=="center"&&axis!="tbar") range.center=value;else return false;
        if(!next.valid()) return false;
        const auto temp=calibration_path+".tmp";
        { std::ofstream file(temp);file<<"2\n";
          for(const auto* r:{&next.tbar,&next.x,&next.y}) file<<r->low<<' '<<r->center<<' '<<r->high<<'\n';
          file<<next.tbar_margin<<' '<<next.joystick_deadband<<'\n';
          file.flush();if(!file) return false; }
        if(std::rename(temp.c_str(),calibration_path.c_str())!=0) return false;
        calibration=next;analog_report();return true;
    }
    uint32_t gesture_now=0;
    std::array<char,9> home_clock{};
    void apply_adapter() {
        const auto& next=configuration.saved();
        if(same_config(next,applied_config)) return;
        if(next.server==applied_config.server&&next.backend==applied_config.backend&&next.active()==applied_config.active()) {applied_config=next;return;}
        const auto state=mixer_adapter.state();
        if(state.busy||state.transitioning) return;
        const bool switched=next.server!=applied_config.server||next.backend!=applied_config.backend;
        if(switched) transitions().reset_tbar();
        mixer_adapter.configure(next);
        if(switched) {
            live_mappings=next.backend==Backend::obs?(server_mappings?(*server_mappings)[next.server]:obs_mappings):Mappings{};
            transition_control.reset(new TransitionControl(panel,mixer_adapter));
            auto settings=live_mappings.transitions;
            if(next.backend!=Backend::obs) settings.softness=mixer_adapter.default_softness();
            transitions().defaults(settings);
            mixer.sync_shift(false); key_control.reset(); encoders.reset();
            for(unsigned i=0;i<KeyCount;i++) panel.led(i,0);
            panel.resync();
            setup.adapter_changed();
        }
        applied_config=next;
    }
    std::string last_mixer_message;
    int reported_clear_slot=-1;unsigned reported_clear_status=0;
    void tick() {
        if(setup.testing()){setup.test_tick(gesture_now);return;}
        for(unsigned i=0;i<3;i++)panel.led(31-i,i==applied_config.server?2:0);
        Changes no_edges;transitions().dsk_shift(no_edges,gesture_now);
        transitions().advance_modifiers(gesture_now,!setup.active()&&!key_control.owns_lcd());
        transitions().position_tick(gesture_now);
        setup.take_apply_request();
        apply_adapter();
        const auto report=mixer_adapter.state();
        const std::string message=report.message.data();
        if(message!=last_mixer_message){last_mixer_message=message;std::cout<<"MIXER "<<BackendNames[unsigned(applied_config.backend)]<<" "<<message<<'\n';}
        if(report.media_clear_status&&(report.media_clear_status!=reported_clear_status||report.media_clear_slot!=reported_clear_slot)){
            reported_clear_status=report.media_clear_status;reported_clear_slot=report.media_clear_slot;
            std::cout<<"MEDIA CLEAR SLOT "<<reported_clear_slot+1<<" "<<(reported_clear_status==1?"PENDING":reported_clear_status==2?"CONFIRMED":"NOT CONFIRMED")<<'\n';
        }
        mixer.refresh_shift(gesture_now);
        if(mixer_enabled) { mixer.refresh(gesture_now); key_control.refresh(transitions().dsk_slot(),mixer.second_layer()); }
        transitions().refresh(!setup.active()&&!key_control.owns_lcd(),mixer.second_layer()); setup.refresh_keypad(); setup.sync_overlay();
        if(!setup.active()&&key_control.owns_lcd())key_control.render();
        if(!setup.active()&&!transitions().owns_lcd()&&!key_control.owns_lcd()) {
            const auto state=mixer_adapter.state();
            home_screen(panel,applied_config.backend,mixer_enabled&&state.connected,
                        gesture_now,home_clock.data());
            if(state.auth_required) panel.lcd(1,"AUTH REQUIRED - CONFIGURE IN WEB");
        }
    }
    void state(const Snapshot& s,bool baseline) override {
        auto change=inputs.update(s,baseline);
        key_control.overlay_sync(s.held);
        if(setup.testing()){
            if(baseline){panel_connected=true;panel.resync();}
            setup.test_inputs(s,change);encoders.reset();
            if(!setup.testing()){inputs.update(s,true);transitions().reset_tbar();mixer.sync_shift(s.held[80]);transitions().sync_dsk_shift(s.held[119]);}
            return;
        }
        if(baseline) { panel_connected=true;encoders.reset(); transitions().cancel_modifiers(); mixer.sync_shift(s.held[80]); transitions().sync_dsk_shift(s.held[119]); panel.resync(); std::cout<<"SYNC C++\n"; }
        else {
            change.rotary=encoders.apply(change.rotary);
            for(unsigned i=0;i<KeyCount;i++) {
                if(verbose&&change.pressed[i]) std::cout<<s.ms<<" DOWN "<<i<<'\n';
                if(verbose&&change.released[i]) std::cout<<s.ms<<" UP "<<i<<'\n';
                if(verbose&&change.double_click[i]) std::cout<<"DOUBLE "<<i<<'\n';
            }
            if(change.pressed.count()==1)for(unsigned slot=0;slot<3;slot++)if(change.pressed[31-slot]){requested_server=int(slot);encoders.reset();return;}
            key_control.cancel_menu_for(change.pressed);
            if(change.pressed[82]||change.pressed[83])transitions().close_lcd_menu();
            mixer.shift(change,gesture_now);
            transitions().dsk_shift(change,gesture_now);
            if(mixer_enabled&&change.pressed.count()==1&&(change.pressed[136]||change.pressed[120]||change.pressed[121]||change.pressed[122]||change.pressed[82]||change.pressed[83])&&setup.active()) {
                // Leave SETUP through its normal save/validation path before
                // granting WIPE/DME the LCD and keypad, including an active F editor.
                setup.close_menu();
                if(setup.active()) return; // invalid settings keep their error/focus
            }
            const bool menu=setup.active()||change.pressed[47]||change.pressed[46];
            std::bitset<KeyCount> reserved;
            if(setup.active()) for(unsigned i=0;i<KeyCount;i++) if(setup.claims(i)) reserved.set(i);
            reserved.set(46); reserved.set(47);
            if(mixer_enabled) transitions().modifiers(change,gesture_now,!menu);
            const bool output_key=mixer_enabled&&mixer.press_output(change.pressed);
            const bool editing=setup.claims_encoders();
            setup.handle(change);
            if(setup.testing()){setup.test_analog(0,raw_tbar);setup.test_analog(1,uint16_t(raw_x<<8|raw_y));encoders.reset();return;}
            if(mixer_enabled&&!output_key) {
                if(!editing){transitions().rotary(change.rotary[0]);transitions().position_rotary(change.rotary[0],change.rotary[1]);transitions().mix_rotary(change.rotary[0],change.rotary[1],change.rotary[2]);}
                auto keys=change.pressed;
                auto doubles=change.double_click;
                if(menu) { keys&=~reserved; doubles&=~reserved; }
                bool utility=transitions().background_press(keys,mixer.second_layer())||key_control.aux_chord(keys);
                if(!utility&&mixer_adapter.me_delegation()) {
                    for(unsigned s=0;s<4;++s) {
                        if(keys[108+s]) {
                            mixer_adapter.set_me(s);
                            utility=true;
                        }
                    }
                }
                if(!utility&&!key_control.overlay_press(keys,mixer.second_layer())&&!key_control.press(keys,transitions().dsk_slot(),doubles,mixer.second_layer())&&!transitions().press(keys,doubles)) mixer.press(keys);
            }
            // Movement predates simultaneous navigation; never carry a fraction
            // from the old editor into a newly selected field.
            if(change.pressed.any()) encoders.reset();
            for(unsigned i=0;i<6;i++) if(verbose&&change.rotary[i]) std::cout<<"ENCODER "<<i<<' '<<change.rotary[i]<<'\n';
        }

    }
    void ack(uint8_t type,uint8_t status) override {
        panel.ack(type,status);
        if(status||(verbose&&type!=2 && type!=0x13)) std::cout<<"ACK "<<unsigned(status)<<" TYPE "<<unsigned(type)<<'\n';
    }
    void lost(const char* why) override { panel_connected=false;key_control.overlay_sync({});firmware_version.clear();firmware_date.clear(); setup.firmware_info(""); transitions().cancel_modifiers();transitions().reset_tbar(); analog_valid.fill(false);have_diag=false;analog_missing.fill(0); panel.resync(); std::cout<<"LINK LOST "<<why<<"\n"; }
};
static void print_help() {
    std::cout << R"HELP(Usage: faderOS --port DEVICE [options]

Control a Sony BKDS-2010 running the binary-link firmware.

  --port DEVICE           Serial device or emulator PTY (required)
  --config FILE           Host settings (default: link-settings.conf)
  --calibration FILE      Analog limits (default: analog-calibration.conf)
  --baud RATE             9600, 19200, 38400 or experimental 76800
  --obs | --vmix | --atem | --kavtor
                          Select initial mixer; otherwise use saved settings
  --mappings FILE         OBS source/transition mappings
  --vmix-mappings FILE    vMix source mappings
  --atem-mappings FILE    ATEM source mappings
  --web PORT              Enable configuration HTTP server (1024–65535)
  --baud-control DIR      Emulator baud bridge directory
  --verbose               Button, encoder, ACK and analog diagnostics
  --catalog               Print physical control IDs and exit
  --mapping-schema        Print OBS mapping schema and exit
  --version               Print software version and exit
  -h, --help              Print this help and exit

Console commands: led, blink, lcd, seg, indicators, buzzer, analog [watch],
cal AXIS min|center|max, media-clear SLOT CONFIRM, quit.
Use --baud 38400 for the validated physical link. Web configuration is intended
for a trusted local network; it has no HTTP authentication or TLS.
)HELP";
}
int main(int argc,char** argv) {
    std::string path,config_path="link-settings.conf",mapping_path,baud_control; bool verbose=false;
    bool obs_enabled=false,kavtor_flag=false,vmix_flag=false,atem_flag=false; uint32_t preferred_baud=9600; unsigned web_port=0;
    std::string calibration_path="analog-calibration.conf",vmix_mapping_path="vmix-mappings.json",atem_mapping_path="atem-mappings.json";
    for(int i=1;i<argc;i++) {
        if(std::strcmp(argv[i],"--help")==0||std::strcmp(argv[i],"-h")==0) {print_help();return 0;}
        else if(std::strcmp(argv[i],"--version")==0) {std::cout<<ProductName<<" "<<HostVersion<<'\n';return 0;}
        else if(std::strcmp(argv[i],"--verbose")==0) verbose=true;
        else if(std::strcmp(argv[i],"--port")==0&&i+1<argc) path=argv[++i];
        else if(std::strcmp(argv[i],"--calibration")==0&&i+1<argc) calibration_path=argv[++i];
        else if(std::strcmp(argv[i],"--config")==0&&i+1<argc) config_path=argv[++i];
        else if(std::strcmp(argv[i],"--baud-control")==0&&i+1<argc) baud_control=argv[++i];
        else if(std::strcmp(argv[i],"--baud")==0&&i+1<argc) {
            const std::string value=argv[++i];
            if(value=="9600") preferred_baud=9600;
            else if(value=="19200") preferred_baud=19200;
            else if(value=="38400") preferred_baud=38400;
            else if(value=="76800") preferred_baud=76800;
            else { std::cerr<<"Supported baud: 9600, 19200, 38400, 76800\n"; return 1; }
        }
        else if(std::strcmp(argv[i],"--web")==0&&i+1<argc) {
            char* end=nullptr;auto value=std::strtoul(argv[++i],&end,10);
            if(!end||*end||value<1024||value>65535) { std::cerr<<"Invalid web port\n";return 1; }
            web_port=unsigned(value);
        }
        else if(std::strcmp(argv[i],"--obs")==0) obs_enabled=true;
        else if(std::strcmp(argv[i],"--atem")==0) atem_flag=true;
        else if(std::strcmp(argv[i],"--atem-mappings")==0&&i+1<argc) atem_mapping_path=argv[++i];
        else if(std::strcmp(argv[i],"--vmix")==0) vmix_flag=true;
        else if((std::strcmp(argv[i],"--kavtor")==0||std::strcmp(argv[i],"--strata")==0)) kavtor_flag=true;
        else if(std::strcmp(argv[i],"--vmix-mappings")==0&&i+1<argc) vmix_mapping_path=argv[++i];
        else if(std::strcmp(argv[i],"--mappings")==0&&i+1<argc) mapping_path=argv[++i];
        else if(std::strcmp(argv[i],"--mapping-schema")==0) { print_mapping_schema(); return 0; }
        else if(std::strcmp(argv[i],"--catalog")==0) {
            for(const auto& c:Controls) std::cout<<unsigned(c.id)<<' '<<c.name<<'\n';
            return 0;
        } else { std::cerr<<"Unknown option or missing argument: "<<argv[i]<<"\n"; print_help(); return 1; }
    }
    if(unsigned(obs_enabled)+unsigned(kavtor_flag)+unsigned(vmix_flag)+unsigned(atem_flag)>1) {std::cerr<<"Choose only one initial adapter: --obs, --kavtor, --vmix or --atem\n";return 1;}
    if(path.empty()) { std::cerr<<"Missing --port\n"; return 1; }
    Posix transport(path.c_str(),baud_control); if(!transport.valid()) { std::perror("SCI2"); return 1; }
    Store store(config_path); Config config; const bool loaded=store.load(config);
    if(atem_flag) config.backend=Backend::atem;
    else if(vmix_flag) config.backend=Backend::vmix;
    else if(kavtor_flag) config.backend=Backend::kavtor;
    else if(obs_enabled) config.backend=Backend::obs;
    const bool explicit_mappings=!mapping_path.empty();
    if(web_port&&mapping_path.empty()) mapping_path="obs-mappings.json";
    Mappings mappings; std::array<std::string,24> scenes{};
    if(!mapping_path.empty()&&(explicit_mappings||std::ifstream(mapping_path).good())&&!load_mappings(mapping_path,mappings,scenes)) { std::cerr<<"Invalid mappings file\n"; return 1; }
    if(obs_enabled&&mapping_path.empty()) std::cout<<"OBS: no source mappings; read-only connection\n";
    const char* password=std::getenv("BKDS_OBS_PASSWORD");
    std::array<std::unique_ptr<ObsAdapter>,3> obs_servers;
    std::array<std::unique_ptr<KavtorAdapter>,3> kavtor_servers;
    std::array<std::unique_ptr<VmixAdapter>,3> vmix_servers;
    std::array<std::unique_ptr<AtemAdapter>,3> atem_servers;
    std::array<Mappings,3> server_mappings;std::array<std::array<std::string,24>,3> server_scenes;
    std::array<std::string,3> mapping_files;
    AdapterRouter active;
    for(unsigned slot=0;slot<3;slot++){
        const std::string suffix=slot?".server"+std::to_string(slot+1):"";
        mapping_files[slot]=mapping_path.empty()?std::string():mapping_path+suffix;
        server_mappings[slot]=Mappings{};server_scenes[slot]={};
        if(slot==0){server_mappings[slot]=mappings;server_scenes[slot]=scenes;}
        else if(!mapping_files[slot].empty()&&std::ifstream(mapping_files[slot]).good()&&!load_mappings(mapping_files[slot],server_mappings[slot],server_scenes[slot])){std::cerr<<"Invalid mappings for server "<<slot+1<<'\n';return 1;}
        obs_servers[slot]=std::make_unique<ObsAdapter>(slot==0&&password?password:"",server_scenes[slot],config_path+suffix+".credentials.json");
        kavtor_servers[slot]=std::make_unique<KavtorAdapter>();vmix_servers[slot]=std::make_unique<VmixAdapter>(vmix_mapping_path+suffix);atem_servers[slot]=std::make_unique<AtemAdapter>(atem_mapping_path+suffix);
        obs_servers[slot]->set_transitions(server_mappings[slot].transitions);
        active.attach(Backend::atem,*atem_servers[slot],slot);active.attach(Backend::vmix,*vmix_servers[slot],slot);active.attach(Backend::obs,*obs_servers[slot],slot);active.attach(Backend::kavtor,*kavtor_servers[slot],slot);
        if(slot!=config.server&&config.servers[slot].enabled){Config background=config;background.select_server(slot);active.configure(background);}
    }
    active.configure(config);
    Mappings live_mappings=config.backend==Backend::obs?server_mappings[config.server]:Mappings{};
    Application app(store,config,active,live_mappings,mappings,true);
    app.verbose=verbose;app.server_mappings=&server_mappings;Session session(transport,app);
    app.calibration_path=calibration_path;
    { std::ifstream file(calibration_path);if(file) {
        unsigned version=0;AnalogCalibration cal;file>>version;
        for(auto* r:{&cal.tbar,&cal.x,&cal.y}) file>>r->low>>r->center>>r->high;
        if(version==2) file>>cal.tbar_margin>>cal.joystick_deadband;
        if(!file||(version!=1&&version!=2)||!cal.valid()) {std::cerr<<"Invalid analog calibration\n";return 1;}
        app.calibration=cal;
    } }
    std::cout<<std::unitbuf<<ProductName<<" / config "<<(loaded?"loaded":"defaults")<<" / "<<config_path<<'\n';
    std::cout<<"MIXER "<<BackendNames[unsigned(config.backend)]<<" / runtime selection enabled\n";
    std::cout<<"SYSTEM SETUP: button 47. Console: led, blink, lcd, seg, indicators, buzzer, analog [watch], cal AXIS min|center|max, media-clear SLOT CONFIRM, quit\n";
    std::signal(SIGINT,stop); std::signal(SIGTERM,stop); std::signal(SIGPIPE,SIG_IGN);
    std::random_device rng; session.prefer_baud(preferred_baud); session.start(rng(),0);
    std::unique_ptr<WebConfig> web;
    if(web_port) {
        web=std::make_unique<WebConfig>(web_port,mapping_files[config.server],*obs_servers[config.server],kavtor_servers[config.server].get(),vmix_servers[config.server].get(),atem_servers[config.server].get(),config.server);
        if(!web->valid()) {std::cerr<<"Cannot start web configuration\n";return 1;}
        std::cout<<"WEB http://127.0.0.1:"<<web_port<<'\n';
    }
    unsigned web_server=config.server;
    uint32_t observed_baud=0,ready_baud=0;
    auto start=std::chrono::steady_clock::now(); uint32_t clock_at=uint32_t(-1000),web_at=uint32_t(-250);
    std::string input;
    while(!stopping&&!session.is_closed()) {
        uint32_t now=uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count());
        app.gesture_now=now;
        session.tick(now);
        if(session.baud()!=observed_baud) { observed_baud=session.baud(); ready_baud=0; std::cout<<"SCI2 BAUD "<<observed_baud<<'\n'; }
        if(session.ready()&&ready_baud!=session.baud()) { ready_baud=session.baud(); std::cout<<"SCI2 READY "<<ready_baud<<'\n'; }
        if(web&&!obs_servers[web_server]->state().busy&&!obs_servers[web_server]->state().transitioning&&web->take(mappings,scenes)) {
            auto& obs=*obs_servers[web_server];server_mappings[web_server]=mappings;server_scenes[web_server]=scenes;
            obs.set_transitions(mappings.transitions);
            if(app.applied_config.server==web_server&&active.backend()==Backend::obs) {live_mappings=mappings;app.transitions().defaults(mappings.transitions);app.encoders.reset();}
            obs.set_sources(scenes);app.panel.resync();std::cout<<"WEB MAPPINGS APPLIED\n";
        }
        if(app.requested_server>=0){const auto state=active.state();if(!state.busy&&!state.transitioning){Config next=app.configuration.saved();next.select_server(unsigned(app.requested_server));const auto result=app.configuration.save(next,app.configuration.revision());if(result!=SaveResult::saved&&result!=SaveResult::unchanged)app.panel.beep();app.requested_server=-1;}}
        const unsigned selected_server=app.configuration.saved().server;
        if(web&&selected_server!=web_server){if(web->bind_profile(mapping_files[selected_server],*obs_servers[selected_server],kavtor_servers[selected_server].get(),vmix_servers[selected_server].get(),atem_servers[selected_server].get(),selected_server))web_server=selected_server;}
        if(web&&uint32_t(now-web_at)>=250) {
            web_at=now;const auto state=active.state();
            if(web->sync_system(app.configuration,app.firmware_version.c_str(),app.firmware_date.c_str(),session.baud(),app.panel_connected,active.supported(),!state.busy&&!state.transitioning,ProtocolLabels[unsigned(active.backend())]))app.apply_adapter();
        }
        app.tick(); app.panel.flush(session,now);
        if(uint32_t(now-clock_at)>=1000) {
            const auto elapsed=uint32_t(now-clock_at);
            if(elapsed<10000&&(app.analog_count[0]||app.analog_count[1])) {
                if(app.verbose) {app.analog_report();std::cout<<"ANALOG RX Hz "<<app.analog_count[0]*1000/elapsed<<' '<<app.analog_count[1]*1000/elapsed
                    <<" / "<<(session.split_analog()?"split":"legacy")<<" gaps "<<app.analog_missing[0]<<'/'<<app.analog_missing[1]<<'\n';}app.analog_count.fill(0);app.analog_missing.fill(0);
            }
            clock_at=now; const auto t=std::time(nullptr); std::tm utc{}; gmtime_r(&t,&utc);
            char text[24]; std::strftime(text,sizeof text,"%Y-%m-%d %H:%M:%S",&utc); app.setup.host_clock(text);
            std::tm local{};localtime_r(&t,&local);
            std::strftime(app.home_clock.data(),app.home_clock.size(),"%H:%M:%S",&local);
        }
        fd_set fds; FD_ZERO(&fds); FD_SET(STDIN_FILENO,&fds); timeval wait{0,1000};
        if(select(STDIN_FILENO+1,&fds,nullptr,nullptr,&wait)>0) {
            char bytes[256]; auto n=::read(STDIN_FILENO,bytes,sizeof bytes); if(n<=0) break;
            for(int i=0;i<n;i++) {
                if(bytes[i]!='\n') { if(input.size()<512) input+=bytes[i]; continue; }
                std::istringstream line(input); input.clear(); std::string cmd; line>>cmd;
                unsigned id=0,level=0,period=0; bool ok=false;
                if(cmd=="analog") {std::string mode;line>>mode;app.analog_watch=mode=="watch";app.analog_report();continue;}
                if(cmd=="cal") {std::string axis,endpoint;line>>axis>>endpoint;
                    std::cout<<(app.calibrate(axis,endpoint)?"CALIBRATION SAVED":"INVALID CALIBRATION / CHECK RAW LIMITS")<<'\n';continue;}
                if(cmd=="apply"&&app.mixer_enabled) { app.apply_adapter(); std::cout<<"APPLY MIXER ENDPOINT\n"; continue; }
                if(cmd=="media-clear") {
                    unsigned slot;std::string confirm,extra;
                    const bool valid=bool(line>>slot>>confirm)&&!(line>>extra)&&slot>=1&&confirm=="CONFIRM";
                    ok=valid&&active.clear_media_still(slot-1);
                    std::cout<<(ok?"MEDIA CLEAR QUEUED":"MEDIA CLEAR REJECTED: use media-clear SLOT CONFIRM; MP1 selection is protected")<<'\n';continue;
                }
                if(cmd=="quit") { stopping=1; break; }
                if(cmd=="led"&&(line>>id>>level)) ok=app.panel.led(id,level);
                if(cmd=="blink"&&(line>>id>>level>>period)) ok=period&&app.panel.led(id,level,period);
                if(cmd=="lcd"&&(line>>id)) { std::string text; std::getline(line>>std::ws,text); ok=app.panel.lcd(id,text.c_str()); }
                if(cmd=="buzzer"&&(line>>level)&&level<=1) { app.panel.set_buzzer(level); ok=true; }
                if(cmd=="indicators"&&(line>>std::hex>>level)&&level<=255&&!(level&15)) { app.panel.set_indicators(uint8_t(level)); ok=true; }
                if(cmd=="seg") {
                    uint8_t values[7]{}; ok=true;
                    for(auto& v:values) { unsigned x; if(!(line>>std::hex>>x)||x>255) { ok=false; break; } v=uint8_t(x); }
                    if(ok) ok=app.panel.digits(values);
                }
                std::cout<<(ok?"QUEUED":"INVALID COMMAND")<<'\n';
            }
        }
    }
    active.cancel_manual();
    std::cout<<"Host stopped"<<(session.is_closed()?": SCI2 disconnected":"")<<'\n';
    return 0;
}
