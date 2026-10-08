#pragma once
#include "config.hpp"
#include "mixer.hpp"
#include <string>
namespace bkds::link {
// Linux WebSocket transport/JSON/crypto live outside the portable panel core.
class ObsAdapter final:public MixerAdapter {
    struct Impl;
    Impl* impl;
public:
    explicit ObsAdapter(const std::string& password,const std::array<std::string,24>& scenes,const std::string& credentials_path="");
    ~ObsAdapter() override;
    bool set_password(const std::string&);
    void set_transitions(const TransitionSettings&);
    void dme_parameters(DmeParameters) override;
    bool manual(uint16_t,TransitionType,uint32_t=0,bool=false,uint32_t=3) override;
    void cancel_manual() override;
    bool output(OutputKind) override;
    bool dsk(bool,uint32_t,unsigned=0) override;
    bool automatic(TransitionType,uint32_t,uint32_t=0,bool=false,uint32_t=3) override;
    void set_sources(const std::array<std::string,24>&);
    std::string web_status();
    void configure(const Config&) override;
    void deactivate() override;
    MixerState state() override;
    uint32_t first_direct_dme_code() const override {return 0;}
    bool valid_dme_code(uint32_t) const override;
    bool keypad_transition_available(TransitionType,unsigned) const override;
    bool server_info(ServerInfo&) const override;
    bool request(MixerAction,unsigned) override;
};
bool load_mappings(const std::string& path,Mappings&,std::array<std::string,24>& scenes);
void print_mapping_schema();
}
