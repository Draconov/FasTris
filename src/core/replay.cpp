#include "fasttris/replay.hpp"
#include "fasttris/sha256.hpp"
#include <cstdint>
#include <fstream>
#include <sstream>
namespace fasttris {
const char* actionToken(Action a){return actionName(a).data();}
bool parseActionToken(const std::string&s,Action&a){for(int i=0;i<static_cast<int>(Action::Count);++i){auto x=static_cast<Action>(i);if(s==actionName(x)){a=x;return true;}}return false;}
std::string stateHash(const Game& g){return sha256(g.deterministicState());}
bool saveReplay(const Replay&r,const std::string&path,std::string*err){
    std::ofstream f(path,std::ios::binary);if(!f){if(err)*err="cannot open replay for writing";return false;}
    f<<"FASTTRIS_REPLAY "<<r.version<<"\nseed "<<r.seed<<"\nmode "<<static_cast<int>(r.mode)<<"\n";
    const auto&h=r.rules.handling;f<<"das "<<h.das_ms<<"\narr "<<h.arr_ms<<"\nsdf "<<h.sdf<<"\ndcd "<<h.dcd_ms<<"\nlock "<<h.lock_delay_ms<<"\nresets "<<h.max_lock_resets<<"\nrot180 "<<h.allow_180<<"\nirs "<<h.irs<<"\nihs "<<h.ihs<<"\nghost "<<r.rules.ghost<<"\nnext "<<r.rules.next_count<<"\ntournament "<<r.rules.tournament<<"\ngcap "<<r.rules.garbage_cap<<"\ngdelay "<<r.rules.garbage_delay_ms<<"\ngmess "<<r.rules.garbage_messiness_pct<<"\ncustomgrav "<<r.rules.custom_gravity_ms<<"\ncustomlines "<<r.rules.custom_line_goal<<"\ncustomtime "<<r.rules.custom_time_limit_s<<"\ncustomgarbage "<<r.rules.custom_start_garbage<<"\n";
    for(auto&e:r.events)f<<"event "<<e.time_us<<' '<<actionToken(e.action)<<' '<<(e.down?1:0)<<"\n";
    f<<"duration "<<r.duration_us<<"\nhash "<<r.final_hash<<"\n";return bool(f);
}
bool loadReplay(const std::string&path,Replay&out,std::string*err){
    out=Replay{};
    std::ifstream f(path,std::ios::binary);
    if(!f){if(err)*err="cannot open replay";return false;}

    std::string line;
    std::getline(f,line);
    std::istringstream head(line);
    std::string magic;
    head>>magic>>out.version;
    if(magic!="FASTTRIS_REPLAY"||out.version!=1){if(err)*err="unsupported replay format";return false;}

    enum Field : std::uint32_t {
        Seed=1u<<0, ModeField=1u<<1, Das=1u<<2, Arr=1u<<3, Sdf=1u<<4, Dcd=1u<<5,
        Lock=1u<<6, Resets=1u<<7, Rot180=1u<<8, Irs=1u<<9, Ihs=1u<<10,
        Ghost=1u<<11, Next=1u<<12, Tournament=1u<<13, Gcap=1u<<14, Gdelay=1u<<15,
        Gmess=1u<<16, CustomGrav=1u<<17, CustomLines=1u<<18, CustomTime=1u<<19,
        CustomGarbage=1u<<20, Duration=1u<<21, Hash=1u<<22
    };
    constexpr std::uint32_t required=(1u<<23)-1u;
    std::uint32_t seen=0;
    out.events.clear();

    while(std::getline(f,line)){
        if(line.empty())continue;
        std::istringstream s(line);
        std::string k;
        s>>k;
        if(k=="seed"){s>>out.seed;seen|=Seed;}
        else if(k=="mode"){
            int v;s>>v;
            if(v<0||v>static_cast<int>(Mode::Custom)){if(err)*err="invalid mode";return false;}
            out.mode=static_cast<Mode>(v);seen|=ModeField;
        }
        else if(k=="das"){s>>out.rules.handling.das_ms;seen|=Das;}
        else if(k=="arr"){s>>out.rules.handling.arr_ms;seen|=Arr;}
        else if(k=="sdf"){s>>out.rules.handling.sdf;seen|=Sdf;}
        else if(k=="dcd"){s>>out.rules.handling.dcd_ms;seen|=Dcd;}
        else if(k=="lock"){s>>out.rules.handling.lock_delay_ms;seen|=Lock;}
        else if(k=="resets"){s>>out.rules.handling.max_lock_resets;seen|=Resets;}
        else if(k=="rot180"){s>>out.rules.handling.allow_180;seen|=Rot180;}
        else if(k=="irs"){s>>out.rules.handling.irs;seen|=Irs;}
        else if(k=="ihs"){s>>out.rules.handling.ihs;seen|=Ihs;}
        else if(k=="ghost"){s>>out.rules.ghost;seen|=Ghost;}
        else if(k=="next"){s>>out.rules.next_count;seen|=Next;}
        else if(k=="tournament"){s>>out.rules.tournament;seen|=Tournament;}
        else if(k=="gcap"){s>>out.rules.garbage_cap;seen|=Gcap;}
        else if(k=="gdelay"){s>>out.rules.garbage_delay_ms;seen|=Gdelay;}
        else if(k=="gmess"){s>>out.rules.garbage_messiness_pct;seen|=Gmess;}
        else if(k=="customgrav"){s>>out.rules.custom_gravity_ms;seen|=CustomGrav;}
        else if(k=="customlines"){s>>out.rules.custom_line_goal;seen|=CustomLines;}
        else if(k=="customtime"){s>>out.rules.custom_time_limit_s;seen|=CustomTime;}
        else if(k=="customgarbage"){s>>out.rules.custom_start_garbage;seen|=CustomGarbage;}
        else if(k=="event"){
            ReplayEvent e;std::string a;int d;
            s>>e.time_us>>a>>d;
            if(!parseActionToken(a,e.action)){if(err)*err="bad action token";return false;}
            e.down=d!=0;out.events.push_back(e);
        }
        else if(k=="duration"){s>>out.duration_us;seen|=Duration;}
        else if(k=="hash"){s>>out.final_hash;seen|=Hash;}
        else {if(err)*err="unsupported replay field";return false;}
        if(!s){if(err)*err="malformed replay field";return false;}
    }

    if(seen!=required){if(err)*err="incomplete replay format";return false;}
    return true;
}

bool verifyReplay(const Replay&r,std::string*actual){Game g(r.seed,r.mode,r.rules);for(auto&e:r.events){if(e.time_us<g.now())return false;g.advanceTo(e.time_us);if(e.down)g.press(e.action);else g.release(e.action);}g.advanceTo(r.duration_us);auto h=stateHash(g);if(actual)*actual=h;return !r.final_hash.empty()&&h==r.final_hash;}
} // namespace fasttris
