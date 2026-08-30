#include "renderer.hpp"
#include "fasttris/version.hpp"
#include "fasttris/tetromino.hpp"
#include <algorithm>
#include <cstdarg>
#include <array>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>

namespace fasttris::app {
namespace {
struct C { Uint8 r,g,b,a; };
struct Canvas { float w{960.0f}; float h{720.0f}; float scale{1.0f}; };

float g_offset_x = 0.0f;
float g_offset_y = 0.0f;

Canvas beginCanvas(SDL_Renderer* r, bool center_design = true) {
    int output_w = 960;
    int output_h = 720;
    if (!SDL_GetRenderOutputSize(r, &output_w, &output_h) || output_w <= 0 || output_h <= 0) {
        output_w = 960;
        output_h = 720;
    }

    constexpr float design_w = 960.0f;
    constexpr float design_h = 720.0f;
    const float scale = std::max(0.1f, std::min(output_w / design_w, output_h / design_h));
    const float canvas_w = output_w / scale;
    const float canvas_h = output_h / scale;

    SDL_SetRenderScale(r, scale, scale);
    g_offset_x = center_design ? std::max(0.0f, (canvas_w - design_w) * 0.5f) : 0.0f;
    g_offset_y = center_design ? std::max(0.0f, (canvas_h - design_h) * 0.5f) : 0.0f;
    return {canvas_w, canvas_h, scale};
}

C color(Piece p){switch(p){case Piece::I:return{60,210,230,255};case Piece::J:return{70,100,230,255};case Piece::L:return{240,150,55,255};case Piece::O:return{235,210,65,255};case Piece::S:return{80,210,100,255};case Piece::T:return{175,80,220,255};case Piece::Z:return{225,70,80,255};case Piece::Garbage:return{100,110,120,255};default:return{0,0,0,0};}}
void set(SDL_Renderer*r,C c){SDL_SetRenderDrawColor(r,c.r,c.g,c.b,c.a);}
void fill(SDL_Renderer*r,float x,float y,float w,float h,C c){set(r,c);SDL_FRect q{x+g_offset_x,y+g_offset_y,w,h};SDL_RenderFillRect(r,&q);}
void outline(SDL_Renderer*r,float x,float y,float w,float h,C c){set(r,c);SDL_FRect q{x+g_offset_x,y+g_offset_y,w,h};SDL_RenderRect(r,&q);}
void txt(SDL_Renderer*r,float x,float y,const std::string&s,C c={220,225,232,255},bool big=false){
    set(r,c);
    const float px=x+g_offset_x,py=y+g_offset_y;
    if(big){
        float sx=1.0f,sy=1.0f;
        SDL_GetRenderScale(r,&sx,&sy);
        SDL_SetRenderScale(r,sx*2.0f,sy*2.0f);
        SDL_RenderDebugText(r,px/2.0f,py/2.0f,s.c_str());
        SDL_SetRenderScale(r,sx,sy);
    }else SDL_RenderDebugText(r,px,py,s.c_str());
}
void txtf(SDL_Renderer*r,float x,float y,C c,const char*fmt,...){char b[256];va_list ap;va_start(ap,fmt);std::vsnprintf(b,sizeof(b),fmt,ap);va_end(ap);txt(r,x,y,b,c);}
const char* clearName(ClearKind k){switch(k){case ClearKind::Single:return"SINGLE";case ClearKind::Double:return"DOUBLE";case ClearKind::Triple:return"TRIPLE";case ClearKind::Quad:return"QUAD";case ClearKind::MiniNoLine:return"T-SPIN MINI";case ClearKind::MiniSingle:return"T-SPIN MINI SINGLE";case ClearKind::MiniDouble:return"T-SPIN MINI DOUBLE";case ClearKind::TSpinNoLine:return"T-SPIN";case ClearKind::TSpinSingle:return"T-SPIN SINGLE";case ClearKind::TSpinDouble:return"T-SPIN DOUBLE";case ClearKind::TSpinTriple:return"T-SPIN TRIPLE";default:return"";}}
void drawMini(SDL_Renderer*r,Piece p,float ox,float oy,float s){if(p==Piece::None)return;auto bs=blocks(p,Rotation::Spawn);int minx=4,maxx=0,miny=4,maxy=0;for(auto b:bs){minx=std::min(minx,b.x);maxx=std::max(maxx,b.x);miny=std::min(miny,b.y);maxy=std::max(maxy,b.y);}float cx=ox-(minx+maxx+1)*s/2.0f,cy=oy-(miny+maxy+1)*s/2.0f;for(auto b:bs){auto cc=color(p);fill(r,cx+b.x*s+1,cy+b.y*s+1,s-2,s-2,cc);}}
void actionLabel(SDL_Renderer*r,float x,float y,const ReplayEvent&e){std::string s=std::string(e.down?"+":"-")+std::string(actionName(e.action));txt(r,x,y,s,{150,160,175,255});}
}

void renderGame(SDL_Renderer*r,Game&g,const RenderInfo&i){
    const auto canvas=beginCanvas(r,false);
    set(r,{11,14,20,255});SDL_RenderClear(r);

    // The board is the visual anchor. Keep its physical size tied to the
    // available height, but use extra width for the surrounding information
    // instead of forcing the whole app into a 4:3 letterbox.
    const float cell=30.0f;
    const float board_w=cell*kBoardW;
    const float board_h=cell*kVisibleH;
    const float extra_h=std::max(0.0f,canvas.h-720.0f);
    const float bx=std::max(330.0f,canvas.w*0.5f-65.0f);
    const float by=60.0f+extra_h*0.5f;
    const float stats_x=std::max(20.0f,bx-390.0f);
    const float hold_x=bx-150.0f;
    const float next_x=bx+board_w+25.0f;

    // playfield
    fill(r,bx-4,by-4,board_w+8,board_h+8,{30,35,45,255});
    fill(r,bx,by,board_w,board_h,{14,18,24,255});
    for(int yy=0;yy<kVisibleH;++yy)for(int x=0;x<kBoardW;++x){int y=yy+kHiddenH;auto p=g.board().cell(x,y);if(p!=Piece::None){auto c=color(p);fill(r,bx+x*cell+1,by+yy*cell+1,cell-2,cell-2,c);fill(r,bx+x*cell+4,by+yy*cell+4,cell-8,4,{Uint8(std::min(255,c.r+35)),Uint8(std::min(255,c.g+35)),Uint8(std::min(255,c.b+35)),255});}}
    if(g.active().piece!=Piece::None){
        if(g.rules().ghost){auto a=g.active();a.y=g.ghostY();for(auto b:blocks(a.piece,a.rot)){int y=a.y+b.y-kHiddenH;if(y>=0){auto c=color(a.piece);c.a=85;outline(r,bx+(a.x+b.x)*cell+3,by+y*cell+3,cell-6,cell-6,c);}}}
        auto&a=g.active();for(auto b:blocks(a.piece,a.rot)){int y=a.y+b.y-kHiddenH;if(y>=0&&y<kVisibleH){auto c=color(a.piece);fill(r,bx+(a.x+b.x)*cell+1,by+y*cell+1,cell-2,cell-2,c);}}
    }
    txt(r,bx,by-38,"FASTRIS",{235,240,248,255},true);
    txt(r,bx+120,by-34,std::string(modeName(g.mode())),{130,210,255,255},true);

    // hold + next
    txt(r,hold_x+10,by+12,"HOLD",{145,155,170,255},true);
    outline(r,hold_x,by+45,120,90,{45,55,70,255});
    drawMini(r,g.holdPiece(),hold_x+60,by+90,20);
    txt(r,next_x+10,by+12,"NEXT",{145,155,170,255},true);
    auto n=g.next(std::max(1,g.rules().next_count));
    const std::size_t shown=std::min<std::size_t>(8,n.size());
    const float next_step=shown>6?62.0f:82.0f;
    const float next_h=shown>6?54.0f:72.0f;
    const float next_piece_size=shown>6?13.0f:16.0f;
    for(std::size_t k=0;k<shown;++k){float y=by+45+next_step*k;outline(r,next_x,y,125,next_h,{40,48,62,255});drawMini(r,n[k],next_x+62,y+next_h/2,next_piece_size);}

    // stats
    const auto&s=g.stats();double sec=s.elapsed_us/1e6;double rate_sec=std::max(0.001,sec);double pps=s.pieces/rate_sec,apm=s.attacks/rate_sec*60.0,kpp=s.pieces?double(s.inputs)/s.pieces:0.0;
    txt(r,stats_x,by+20,"RUN",{145,155,170,255},true);txtf(r,stats_x,by+56,{220,225,232,255},"SEED  %llu",(unsigned long long)i.seed);txtf(r,stats_x,by+76,{220,225,232,255},"TIME  %7.3f",sec);txtf(r,stats_x,by+96,{220,225,232,255},"SCORE %lld",(long long)s.score);txtf(r,stats_x,by+116,{220,225,232,255},"LINES %d",s.lines);txtf(r,stats_x,by+136,{220,225,232,255},"PPS   %.2f",pps);txtf(r,stats_x,by+156,{220,225,232,255},"APM   %.1f",apm);txtf(r,stats_x,by+176,{220,225,232,255},"KPP   %.2f",kpp);txtf(r,stats_x,by+196,{220,225,232,255},"FIN   %d",s.finesse_faults);txtf(r,stats_x,by+216,{220,225,232,255},"B2B   %d",s.b2b_chain);txtf(r,stats_x,by+236,{220,225,232,255},"COMBO %d",std::max(0,s.combo));
    auto cn=clearName(g.lastClear());if(*cn)txt(r,stats_x,by+270,cn,{255,205,100,255},true);if(g.lastAttack()>0)txtf(r,stats_x,by+300,{255,130,100,255},"ATTACK +%d",g.lastAttack());

    // Mode-specific progress: keep the information the player actually cares about visible.
    switch(g.mode()){
        case Mode::Sprint40:
        case Mode::SeedRace:
            txtf(r,stats_x,by+330,{145,155,170,255},"GOAL  40 LINES");
            txtf(r,stats_x,by+348,{130,210,255,255},"LEFT  %d",std::max(0,40-s.lines));
            break;
        case Mode::Ultra120:{
            const double left=std::max(0.0,120.0-sec);
            txt(r,stats_x,by+330,"GOAL  MAX SCORE",{145,155,170,255});
            txtf(r,stats_x,by+348,{130,210,255,255},"LEFT  %6.3f",left);
            break;
        }
        case Mode::Marathon:
            txtf(r,stats_x,by+330,{145,155,170,255},"LEVEL %d / 15",g.level());
            txtf(r,stats_x,by+348,{130,210,255,255},"LEFT  %d LINES",std::max(0,150-s.lines));
            break;
        case Mode::Cheese40:
            txtf(r,stats_x,by+330,{145,155,170,255},"CHEESE %d / 40",std::min(40,s.garbage_lines_cleared));
            txtf(r,stats_x,by+348,{130,210,255,255},"LEFT   %d",std::max(0,40-s.garbage_lines_cleared));
            break;
        case Mode::Finesse:{
            const double acc=s.pieces?100.0*double(s.finesse_perfect_pieces)/s.pieces:100.0;
            txtf(r,stats_x,by+330,{145,155,170,255},"FINESSE %.1f%%",acc);
            txtf(r,stats_x,by+348,{130,210,255,255},"FAULTS %d  STREAK %d",s.finesse_faults,s.finesse_streak);
            break;
        }
        case Mode::Zen:
            txt(r,stats_x,by+330,"ZEN   ENDLESS",{145,155,170,255});
            txt(r,stats_x,by+348,"NO AUTO GRAVITY",{130,210,255,255});
            break;
        case Mode::Custom:{
            const auto& cr=g.rules();
            if(cr.custom_line_goal>0)txtf(r,stats_x,by+330,{145,155,170,255},"LINES LEFT %d",std::max(0,cr.custom_line_goal-s.lines));
            else txt(r,stats_x,by+330,"LINES  ENDLESS",{145,155,170,255});
            if(cr.custom_time_limit_s>0)txtf(r,stats_x,by+348,{130,210,255,255},"TIME LEFT %.3f",std::max(0.0,double(cr.custom_time_limit_s)-sec));
            else if(cr.custom_gravity_ms==0)txt(r,stats_x,by+348,"GRAVITY OFF",{130,210,255,255});
            else txtf(r,stats_x,by+348,{130,210,255,255},"GRAVITY %d MS/CELL",cr.custom_gravity_ms);
            break;
        }
    }

    txt(r,stats_x,by+380,"INPUT",{145,155,170,255},true);int row=0;for(auto it=i.recent_inputs.rbegin();it!=i.recent_inputs.rend()&&row<7;++it,++row)actionLabel(r,stats_x,by+415+row*18,*it);
    txt(r,stats_x,by+580,"F5 restart  F6 save replay",{120,130,145,255});txt(r,stats_x,by+598,"P pause  ESC menu",{120,130,145,255});
    if(i.replay_mode){txtf(r,next_x,by+586,{125,220,170,255},"REPLAY %.1fx%s",i.replay_speed,i.replay_paused?" PAUSED":"");txt(r,next_x,by+604,"SPACE pause  arrows seek",{120,130,145,255});}
    if(!i.status.empty())txt(r,bx,by+620,i.status,{120,220,160,255},true);
    if(i.paused){fill(r,bx,by+240,board_w,90,{5,8,12,220});txt(r,bx+80,by+270,"PAUSED",{255,255,255,255},true);}
    if(g.gameOver()){
        fill(r,bx,by+220,board_w,135,{5,8,12,235});txt(r,bx+60,by+240,"GAME OVER",{255,110,110,255},true);txtf(r,bx+55,by+278,{220,225,232,255},"TIME %.3f   LINES %d",sec,s.lines);txt(r,bx+52,by+312,"F5 RETRY   ESC MENU",{180,190,205,255});
    } else if(g.complete()){
        fill(r,bx,by+208,board_w,160,{5,8,12,235});txt(r,bx+55,by+226,"RUN COMPLETE",{120,240,170,255},true);
        switch(g.mode()){
            case Mode::Sprint40:
            case Mode::SeedRace:
                txtf(r,bx+44,by+266,{220,225,232,255},"TIME %.3f   PPS %.2f",sec,pps);
                txtf(r,bx+44,by+288,{220,225,232,255},"KPP %.2f   FIN %d",kpp,s.finesse_faults);
                break;
            case Mode::Ultra120:
                txtf(r,bx+44,by+266,{220,225,232,255},"SCORE %lld",(long long)s.score);
                txtf(r,bx+44,by+288,{220,225,232,255},"LINES %d   PPS %.2f",s.lines,pps);
                break;
            case Mode::Marathon:
                txtf(r,bx+44,by+266,{220,225,232,255},"SCORE %lld",(long long)s.score);
                txtf(r,bx+44,by+288,{220,225,232,255},"TIME %.3f   LINES %d",sec,s.lines);
                break;
            case Mode::Cheese40:
                txtf(r,bx+44,by+266,{220,225,232,255},"TIME %.3f   PPS %.2f",sec,pps);
                txtf(r,bx+44,by+288,{220,225,232,255},"CHEESE %d   FIN %d",s.garbage_lines_cleared,s.finesse_faults);
                break;
            case Mode::Custom:
                txtf(r,bx+44,by+266,{220,225,232,255},"SCORE %lld   LINES %d",(long long)s.score,s.lines);
                txtf(r,bx+44,by+288,{220,225,232,255},"TIME %.3f   PPS %.2f",sec,pps);
                break;
            case Mode::Finesse:
            case Mode::Zen:
                txtf(r,bx+44,by+266,{220,225,232,255},"TIME %.3f   PIECES %d",sec,s.pieces);
                txtf(r,bx+44,by+288,{220,225,232,255},"FIN %d   PPS %.2f",s.finesse_faults,pps);
                break;
        }
        txt(r,bx+52,by+330,"F5 RETRY   ESC MENU",{180,190,205,255});
    }
}

void renderMenu(SDL_Renderer*r,int sel,std::uint64_t seed,bool tournament,bool hasReplay){
    beginCanvas(r,true);
    set(r,{11,14,20,255});SDL_RenderClear(r);txt(r,350,45,"FASTRIS",{235,240,248,255},true);txt(r,454,50,std::string("v")+fasttris::kVersion,{120,135,155,255});txt(r,276,78,"DETERMINISTIC COMPETITIVE BLOCK ENGINE",{120,135,155,255});
    static const std::array<const char*,12> items={"SPRINT 40L","ULTRA 2:00","MARATHON 150L","ZEN / ENDLESS","CHEESE RACE 40","FINESSE TRAINER","SEED RACE 40L","CUSTOM / SANDBOX","SETTINGS","CONTROLS / REBIND","PLAY LAST REPLAY","QUIT"};
    for(int n=0;n<(int)items.size();++n){C c=n==sel?C{120,220,255,255}:C{205,210,220,255};std::string pre=n==sel?"> ":"  ";std::string label=items[n];if(n==10&&!hasReplay)label+=" (NONE YET)";txt(r,310,122+n*31,pre+label,c,true);}
    static const std::array<const char*,8> desc={
        "Clear 40 lines as fast as possible.",
        "Score as high as possible in exactly two minutes.",
        "Clear 150 lines while gravity increases every 10 lines.",
        "Relaxed endless practice with no automatic gravity.",
        "Clear 40 garbage lines while the cheese replenishes.",
        "No-gravity technique trainer; minimize finesse faults.",
        "Standardized 40L rules; same seed means the same piece order.",
        "Configure gravity, goals, timer and starting garbage."
    };
    if(sel<8){txt(r,245,510,"MODE",{120,135,155,255},true);txt(r,245,536,desc[sel],{190,198,210,255});}
    txtf(r,245,580,{165,175,190,255},"SEED: %llu",(unsigned long long)seed);txt(r,245,600,"E enter seed   R random seed",{130,140,155,255});txt(r,245,620,"T tournament lock",{130,140,155,255});txt(r,435,620,tournament?"ON":"OFF",tournament?C{110,230,160,255}:C{170,175,185,255});txt(r,245,648,"ENTER select   arrows navigate   H help",{130,140,155,255});
}

void renderSettings(SDL_Renderer*r,const AppConfig&c,int sel){
    beginCanvas(r,true);
    set(r,{11,14,20,255});SDL_RenderClear(r);txt(r,330,35,"SETTINGS",{235,240,248,255},true);
    auto h=c.rules.handling;bool ghost=c.rules.ghost;int next=c.rules.next_count;
    if(c.rules.tournament){h.lock_delay_ms=500;h.max_lock_resets=15;h.allow_180=false;h.irs=true;h.ihs=true;ghost=true;next=5;}
    std::array<std::string,13> v={
        "DAS       "+std::to_string(h.das_ms)+" ms",
        "ARR       "+std::to_string(h.arr_ms)+" ms"+(h.arr_ms==0?" (INSTANT)":""),
        "SDF       "+std::to_string(h.sdf)+"x"+(h.sdf==0?" (SONIC)":""),
        "DCD       "+std::to_string(h.dcd_ms)+" ms",
        "LOCK      "+std::to_string(h.lock_delay_ms)+" ms",
        "RESETS    "+std::to_string(h.max_lock_resets),
        std::string("180 ROT   ")+(h.allow_180?"ON":"OFF"),
        std::string("IRS       ")+(h.irs?"ON":"OFF"),
        std::string("IHS       ")+(h.ihs?"ON":"OFF"),
        std::string("GHOST     ")+(ghost?"ON":"OFF"),
        "NEXT      "+std::to_string(next),
        std::string("VSYNC     ")+(c.vsync?"ON":"OFF"),
        "FPS CAP   "+std::string(c.fps_cap==0?"UNCAPPED":std::to_string(c.fps_cap))
    };
    for(int n=0;n<(int)v.size();++n){const bool locked=c.rules.tournament&&n>=4&&n<=10;std::string label=(n==sel?"> ":"  ")+v[n]+(locked?"  [LOCKED]":"");txt(r,270,86+n*34,label,n==sel?C{120,220,255,255}:locked?C{150,150,155,255}:C{210,215,225,255},true);}
    static const std::array<const char*,13> hint={
        "DAS: delay before a held horizontal key starts repeating.",
        "ARR: horizontal repeat interval after DAS. 0 intentionally shifts to the wall.",
        "SDF: soft-drop speed multiplier. 0 performs a sonic drop.",
        "DCD: reduces DAS charge when changing horizontal direction.",
        "Lock delay before a grounded piece locks.",
        "Maximum grounded move/rotation lock-delay resets.",
        "Optional 180-degree rotation.",
        "IRS: held rotation is applied to the next spawning piece.",
        "IHS: held Hold is applied to the next spawning piece.",
        "Show or hide the landing ghost.",
        "Number of upcoming pieces displayed, from 1 to 8.",
        "Synchronize presentation to the display refresh.",
        "Render cap when VSync is disabled. 0 means uncapped."
    };
    if(c.rules.tournament)txt(r,250,548,"TOURNAMENT LOCK: standard rule fields use fixed competitive values",{255,190,100,255});
    txt(r,250,578,hint[std::clamp(sel,0,12)],{150,165,182,255});
    txt(r,250,620,"LEFT/RIGHT change   R reset gameplay defaults",{135,145,160,255});txt(r,250,640,"ESC saves + back",{135,145,160,255});
}

void renderCustomSetup(SDL_Renderer*r,const AppConfig&c,int sel){
    beginCanvas(r,true);
    set(r,{11,14,20,255});SDL_RenderClear(r);txt(r,310,55,"CUSTOM / SANDBOX",{235,240,248,255},true);
    const auto& q=c.rules;
    auto gravity=q.custom_gravity_ms==0?std::string("OFF"):std::to_string(q.custom_gravity_ms)+" ms/cell";
    auto lines=q.custom_line_goal==0?std::string("ENDLESS"):std::to_string(q.custom_line_goal);
    auto timer=q.custom_time_limit_s==0?std::string("OFF"):std::to_string(q.custom_time_limit_s)+" s";
    std::array<std::string,5> items={
        "GRAVITY        "+gravity,
        "LINE GOAL      "+lines,
        "TIME LIMIT     "+timer,
        "START GARBAGE  "+std::to_string(q.custom_start_garbage),
        "START RUN"
    };
    for(int n=0;n<(int)items.size();++n){txt(r,290,160+n*62,(n==sel?"> ":"  ")+items[n],n==sel?C{120,220,255,255}:C{210,215,225,255},true);}
    txt(r,230,500,"Custom runs are deterministic: the seed controls pieces and starting garbage.",{155,168,185,255});
    txt(r,230,530,"Gravity 0 disables automatic fall. Goal/time 0 means endless/off.",{155,168,185,255});
    txt(r,230,590,"UP/DOWN select   LEFT/RIGHT change",{135,145,160,255});
    txt(r,230,612,"ENTER start   ESC back",{135,145,160,255});
}

void renderControls(SDL_Renderer*r,const AppConfig&c,int sel,bool rebinding,bool waitpad){
    beginCanvas(r,true);
    set(r,{11,14,20,255});SDL_RenderClear(r);txt(r,330,40,"CONTROLS",{235,240,248,255},true);for(int n=0;n<8;++n){auto a=static_cast<Action>(n);std::ostringstream s;s<<actionName(a)<<"   KEY "<<SDL_GetKeyName(c.keys[n])<<"   PAD "<<padName(c.pads[n]);txt(r,220,115+n*55,(n==sel?"> ":"  ")+s.str(),n==sel?C{120,220,255,255}:C{210,215,225,255},true);}if(rebinding)txt(r,225,590,waitpad?"PRESS A GAMEPAD BUTTON (ESC CANCELS)":"PRESS A KEY (G = BIND GAMEPAD INSTEAD)",{255,205,100,255},true);else txt(r,225,590,"ENTER rebind keyboard   G rebind gamepad   ESC back",{135,145,160,255});
}
void renderSeedEntry(SDL_Renderer*r,const std::string&t,const std::string&e){beginCanvas(r,true);set(r,{11,14,20,255});SDL_RenderClear(r);txt(r,315,210,"ENTER 64-BIT SEED",{235,240,248,255},true);outline(r,245,280,470,70,{80,95,120,255});txt(r,270,302,t.empty()?"_":t,{120,220,255,255},true);if(!e.empty())txt(r,270,380,e,{255,110,110,255});txt(r,270,420,"ENTER accept   ESC cancel",{135,145,160,255});}
void renderHelp(SDL_Renderer*r){beginCanvas(r,true);set(r,{11,14,20,255});SDL_RenderClear(r);txt(r,360,35,"HELP",{235,240,248,255},true);std::array<const char*,17> l={"Gameplay uses SDL event nanosecond timestamps.","OS key repeat is ignored; DAS/ARR are engine-driven.","Same seed + rules + input events = same simulation.","","Default keys:","Arrows: left/right/down    Space: hard drop","Up: rotate CW             Z: rotate CCW","A: rotate 180             C: hold","P: pause                  F5: restart","F6: save replay           ESC: menu","","Replay viewer:","Space pause, Left/Right seek 1 second","1/2/4/8 playback speed, N jump to next hard drop","","Command line: --seed N --mode sprint --replay file","--verify file --tournament --help"};for(int n=0;n<(int)l.size();++n)txt(r,160,105+n*30,l[n],{190,198,210,255},n<3);txt(r,160,660,"ESC back",{130,140,155,255});}

} // namespace fasttris::app
