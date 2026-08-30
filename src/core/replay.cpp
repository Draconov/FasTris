#include "fasttris/replay.hpp"
#include "fasttris/sha256.hpp"
#include <fstream>
#include <sstream>
namespace fasttris {
const char* actionToken(Action a){return actionName(a).data();}
bool parseActionToken(const std::string&s,Action&a){for(int i=0;i<static_cast<int>(Action::Count);++i){auto x=static_cast<Action>(i);if(s==actionName(x)){a=x;return true;}}return false;}
std::string stateHash(const Game& g){return sha256(g.deterministicState());}
bool saveReplay(const Replay&r,const std::string&path,std::string*err){
    std::ofstream f(path,std::ios::binary);if(!f){if(err)*err="cannot open replay for writing";return false;}
    f<<"FASTTRIS_REPLAY "<<r.version<<"\nseed "<<r.seed<<"\nmode "<<static_cast<int>(r.mode)<<"\n";
    const auto&h=r.rules.handling;f<<"simver "<<r.rules.simulation_version<<"\ndas "<<h.das_ms<<"\narr "<<h.arr_ms<<"\nsdf "<<h.sdf<<"\ndcd "<<h.dcd_ms<<"\nlock "<<h.lock_delay_ms<<"\nresets "<<h.max_lock_resets<<"\nrot180 "<<h.allow_180<<"\nirs "<<h.irs<<"\nihs "<<h.ihs<<"\nghost "<<r.rules.ghost<<"\nnext "<<r.rules.next_count<<"\ntournament "<<r.rules.tournament<<"\ngcap "<<r.rules.garbage_cap<<"\ngdelay "<<r.rules.garbage_delay_ms<<"\ngmess "<<r.rules.garbage_messiness_pct<<"\ncustomgrav "<<r.rules.custom_gravity_ms<<"\ncustomlines "<<r.rules.custom_line_goal<<"\ncustomtime "<<r.rules.custom_time_limit_s<<"\ncustomgarbage "<<r.rules.custom_start_garbage<<"\n";
    for(auto&e:r.events)f<<"event "<<e.time_us<<' '<<actionToken(e.action)<<' '<<(e.down?1:0)<<"\n";
    f<<"duration "<<r.duration_us<<"\nhash "<<r.final_hash<<"\n";return bool(f);
}
bool loadReplay(const std::string&path,Replay&out,std::string*err){
    out=Replay{};
    // Replays written before simulation-version tagging used the original 0.1.0 rules.
    out.rules.simulation_version=1;
    std::ifstream f(path,std::ios::binary);if(!f){if(err)*err="cannot open replay";return false;}std::string line;std::getline(f,line);std::istringstream head(line);std::string magic;head>>magic>>out.version;if(magic!="FASTTRIS_REPLAY"||out.version!=1){if(err)*err="unsupported replay format";return false;}
    out.events.clear();while(std::getline(f,line)){if(line.empty())continue;std::istringstream s(line);std::string k;s>>k;if(k=="seed")s>>out.seed;else if(k=="mode"){int v;s>>v;if(v<0||v>static_cast<int>(Mode::Custom)){if(err)*err="invalid mode";return false;}out.mode=static_cast<Mode>(v);}else if(k=="simver")s>>out.rules.simulation_version;else if(k=="das")s>>out.rules.handling.das_ms;else if(k=="arr")s>>out.rules.handling.arr_ms;else if(k=="sdf")s>>out.rules.handling.sdf;else if(k=="dcd")s>>out.rules.handling.dcd_ms;else if(k=="lock")s>>out.rules.handling.lock_delay_ms;else if(k=="resets")s>>out.rules.handling.max_lock_resets;else if(k=="rot180")s>>out.rules.handling.allow_180;else if(k=="irs")s>>out.rules.handling.irs;else if(k=="ihs")s>>out.rules.handling.ihs;else if(k=="ghost")s>>out.rules.ghost;else if(k=="next")s>>out.rules.next_count;else if(k=="tournament")s>>out.rules.tournament;else if(k=="gcap")s>>out.rules.garbage_cap;else if(k=="gdelay")s>>out.rules.garbage_delay_ms;else if(k=="gmess")s>>out.rules.garbage_messiness_pct;else if(k=="customgrav")s>>out.rules.custom_gravity_ms;else if(k=="customlines")s>>out.rules.custom_line_goal;else if(k=="customtime")s>>out.rules.custom_time_limit_s;else if(k=="customgarbage")s>>out.rules.custom_start_garbage;else if(k=="event"){ReplayEvent e;std::string a;int d;s>>e.time_us>>a>>d;if(!parseActionToken(a,e.action)){if(err)*err="bad action token";return false;}e.down=d!=0;out.events.push_back(e);}else if(k=="duration")s>>out.duration_us;else if(k=="hash")s>>out.final_hash;}
    return true;
}
bool verifyReplay(const Replay&r,std::string*actual){Game g(r.seed,r.mode,r.rules);for(auto&e:r.events){if(e.time_us<g.now())return false;g.advanceTo(e.time_us);if(e.down)g.press(e.action);else g.release(e.action);}g.advanceTo(r.duration_us);auto h=stateHash(g);if(actual)*actual=h;return !r.final_hash.empty()&&h==r.final_hash;}
} // namespace fasttris
