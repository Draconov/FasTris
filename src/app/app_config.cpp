#include "app_config.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace fasttris::app {
namespace {
constexpr std::size_t ix(Action a){return static_cast<std::size_t>(a);}
void clampConfig(AppConfig&c){
    auto&h=c.rules.handling;
    h.das_ms=std::clamp(h.das_ms,0,1000);
    h.arr_ms=std::clamp(h.arr_ms,0,500);
    h.sdf=std::clamp(h.sdf,0,200);
    h.dcd_ms=std::clamp(h.dcd_ms,0,1000);
    h.lock_delay_ms=std::clamp(h.lock_delay_ms,0,2000);
    h.max_lock_resets=std::clamp(h.max_lock_resets,0,100);
    c.rules.next_count=std::clamp(c.rules.next_count,1,8);
    c.fps_cap=std::clamp(c.fps_cap,0,1000);
}
}
AppConfig defaultConfig(){
    AppConfig c;c.keys.fill(SDLK_UNKNOWN);c.pads.fill(-1);
    c.keys[ix(Action::Left)]=SDLK_LEFT;c.keys[ix(Action::Right)]=SDLK_RIGHT;c.keys[ix(Action::SoftDrop)]=SDLK_DOWN;c.keys[ix(Action::HardDrop)]=SDLK_SPACE;c.keys[ix(Action::RotateCW)]=SDLK_UP;c.keys[ix(Action::RotateCCW)]=SDLK_Z;c.keys[ix(Action::Rotate180)]=SDLK_A;c.keys[ix(Action::Hold)]=SDLK_C;c.keys[ix(Action::Restart)]=SDLK_F5;c.keys[ix(Action::Pause)]=SDLK_P;
    c.pads[ix(Action::Left)]=SDL_GAMEPAD_BUTTON_DPAD_LEFT;c.pads[ix(Action::Right)]=SDL_GAMEPAD_BUTTON_DPAD_RIGHT;c.pads[ix(Action::SoftDrop)]=SDL_GAMEPAD_BUTTON_DPAD_DOWN;c.pads[ix(Action::HardDrop)]=SDL_GAMEPAD_BUTTON_DPAD_UP;c.pads[ix(Action::RotateCW)]=SDL_GAMEPAD_BUTTON_SOUTH;c.pads[ix(Action::RotateCCW)]=SDL_GAMEPAD_BUTTON_WEST;c.pads[ix(Action::Rotate180)]=SDL_GAMEPAD_BUTTON_NORTH;c.pads[ix(Action::Hold)]=SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;c.pads[ix(Action::Restart)]=SDL_GAMEPAD_BUTTON_START;c.pads[ix(Action::Pause)]=SDL_GAMEPAD_BUTTON_BACK;
    return c;
}
bool loadConfig(const std::string&path,AppConfig&c){
    std::ifstream f(path);if(!f)return false;
    std::string line;
    while(std::getline(f,line)){
        auto p=line.find('=');if(p==std::string::npos)continue;auto k=line.substr(0,p),v=line.substr(p+1);
        try{
            int n=std::stoi(v);auto&h=c.rules.handling;
            if(k=="das")h.das_ms=n;
            else if(k=="arr")h.arr_ms=n;
            else if(k=="sdf")h.sdf=n;
            else if(k=="dcd")h.dcd_ms=n;
            else if(k=="lock")h.lock_delay_ms=n;
            else if(k=="resets")h.max_lock_resets=n;
            else if(k=="allow180")h.allow_180=n!=0;
            else if(k=="irs")h.irs=n!=0;
            else if(k=="ihs")h.ihs=n!=0;
            else if(k=="ghost")c.rules.ghost=n!=0;
            else if(k=="next")c.rules.next_count=n;
            else if(k=="vsync")c.vsync=n!=0;
            else if(k=="fps")c.fps_cap=n;
            else if(k=="tournament")c.rules.tournament=n!=0;
            else if(k.rfind("key",0)==0){int i=std::stoi(k.substr(3));if(i>=0&&i<(int)c.keys.size())c.keys[i]=static_cast<SDL_Keycode>(n);}
            else if(k.rfind("pad",0)==0){int i=std::stoi(k.substr(3));if(i>=0&&i<(int)c.pads.size())c.pads[i]=n;}
        }catch(...){}
    }

    clampConfig(c);return true;
}
bool saveConfig(const std::string&path,const AppConfig&c){
    std::ofstream f(path);if(!f)return false;const auto&h=c.rules.handling;
    f<<"das="<<h.das_ms
     <<"\narr="<<h.arr_ms
     <<"\nsdf="<<h.sdf
     <<"\ndcd="<<h.dcd_ms
     <<"\nlock="<<h.lock_delay_ms
     <<"\nresets="<<h.max_lock_resets
     <<"\nallow180="<<h.allow_180
     <<"\nirs="<<h.irs
     <<"\nihs="<<h.ihs
     <<"\nghost="<<c.rules.ghost
     <<"\nnext="<<c.rules.next_count
     <<"\nvsync="<<c.vsync
     <<"\nfps="<<c.fps_cap
     <<"\ntournament="<<c.rules.tournament<<"\n";
    for(std::size_t i=0;i<c.keys.size();++i)f<<"key"<<i<<'='<<static_cast<long long>(c.keys[i])<<"\n";
    for(std::size_t i=0;i<c.pads.size();++i)f<<"pad"<<i<<'='<<c.pads[i]<<"\n";
    return bool(f);
}
const char* padName(int b){
    switch(static_cast<SDL_GamepadButton>(b)){
        case SDL_GAMEPAD_BUTTON_SOUTH:return "A/SOUTH";case SDL_GAMEPAD_BUTTON_EAST:return "B/EAST";case SDL_GAMEPAD_BUTTON_WEST:return "X/WEST";case SDL_GAMEPAD_BUTTON_NORTH:return "Y/NORTH";case SDL_GAMEPAD_BUTTON_BACK:return "BACK";case SDL_GAMEPAD_BUTTON_GUIDE:return "GUIDE";case SDL_GAMEPAD_BUTTON_START:return "START";case SDL_GAMEPAD_BUTTON_LEFT_STICK:return "L3";case SDL_GAMEPAD_BUTTON_RIGHT_STICK:return "R3";case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:return "LB";case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:return "RB";case SDL_GAMEPAD_BUTTON_DPAD_UP:return "DPAD UP";case SDL_GAMEPAD_BUTTON_DPAD_DOWN:return "DPAD DOWN";case SDL_GAMEPAD_BUTTON_DPAD_LEFT:return "DPAD LEFT";case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:return "DPAD RIGHT";default:return "UNBOUND";
    }
}
} // namespace fasttris::app
