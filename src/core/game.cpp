#include "fasttris/game.hpp"
#include "fasttris/scoring.hpp"
#include "fasttris/tetromino.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>

namespace fasttris {
namespace {
constexpr TimeUs ms(int v){return static_cast<TimeUs>(v)*1000;}
constexpr std::array<int,20> MARATHON_GRAVITY_MS{
    1000,793,617,473,355,262,190,135,94,64,43,28,18,11,7,5,4,3,2,1
};
}

Game::Game(std::uint64_t seed, Mode mode, Rules rules)
    : seed_(seed), mode_(mode), rules_(rules), bag_(seed),
      garbage_(splitMix64(seed^0xA11CEULL)), cheese_rng_(splitMix64(seed^0xC4EE5EULL),0xC4EE5EULL) {
    restart(seed,mode);
}

void Game::restart(std::uint64_t seed, Mode mode) {
    seed_=seed;mode_=mode;board_.clear();bag_.reset(seed_);
    garbage_.reset(splitMix64(seed_^0xA11CEULL));cheese_rng_.seed(splitMix64(seed_^0xC4EE5EULL),0xC4EE5EULL);
    active_={};hold_=Piece::None;hold_used_=false;stats_={};stats_.combo=-1;stats_.max_combo=-1;
    now_us_=0;next_gravity_us_=kNever;lock_deadline_us_=kNever;next_horizontal_us_=kNever;
    grounded_=false;lock_resets_=0;game_over_=false;complete_=false;paused_=false;
    left_held_=right_held_=soft_held_=false;cw_held_=ccw_held_=rot180_held_=hold_held_=false;horiz_dir_=0;outgoing_attack_=0;last_attack_visual_=0;
    last_action_rotation_=false;last_kick_index_=-1;last_clear_=ClearKind::None;
    if(mode_==Mode::Cheese40) seedCheese();
    spawn();
}

int Game::level() const {
    if(mode_==Mode::Marathon) return std::clamp(stats_.lines/10+1,1,20);
    return 1;
}

TimeUs Game::baseGravityInterval() const {
    if(mode_==Mode::Zen || mode_==Mode::Finesse) return kNever;
    if(mode_==Mode::Marathon) return ms(MARATHON_GRAVITY_MS[level()-1]);
    return 1000000; // 1 cell/s for fast modes; players normally hard-drop.
}
TimeUs Game::currentGravityInterval() const {
    auto base=baseGravityInterval(); if(base>=kNever/2)return kNever;
    if(!soft_held_)return base;
    if(rules_.handling.sdf<=0)return kNever;
    return std::max<TimeUs>(100,base/std::max(1,rules_.handling.sdf));
}
void Game::scheduleGravityFromNow(){auto i=currentGravityInterval();next_gravity_us_=(i>=kNever/2)?kNever:now_us_+i;}

void Game::spawn(Piece forced) {
    Piece chosen=(forced==Piece::None)?bag_.pop():forced;
    hold_used_=false;
    if(rules_.handling.ihs && hold_held_){
        if(hold_==Piece::None){hold_=chosen;chosen=bag_.pop();}
        else std::swap(hold_,chosen);
        hold_used_=true;++stats_.holds;
    }
    active_.piece=chosen;active_.rot=Rotation::Spawn;active_.x=3;active_.y=3;
    grounded_=false;lock_resets_=0;lock_deadline_us_=kNever;
    last_action_rotation_=false;last_kick_index_=-1;piece_input_count_=0;piece_spawn_x_=active_.x;piece_spawn_rot_=active_.rot;
    if(board_.collides(active_)){game_over_=true;active_.piece=Piece::None;next_gravity_us_=kNever;return;}
    if(rules_.handling.irs){
        if(rot180_held_ && rules_.handling.allow_180) tryRotate(2);
        else if(cw_held_) tryRotate(1);
        else if(ccw_held_) tryRotate(-1);
    }
    refreshGrounded(false);scheduleGravityFromNow();
}

bool Game::isGrounded() const {
    if(active_.piece==Piece::None) return false;
    auto t=active_;
    ++t.y;
    return board_.collides(t);
}
void Game::refreshGrounded(bool request_reset){
    bool g=isGrounded();
    if(!g){grounded_=false;lock_deadline_us_=kNever;return;}
    if(!grounded_){grounded_=true;lock_deadline_us_=now_us_+ms(std::max(0,rules_.handling.lock_delay_ms));return;}
    if(request_reset && lock_resets_<std::max(0,rules_.handling.max_lock_resets)){
        ++lock_resets_;lock_deadline_us_=now_us_+ms(std::max(0,rules_.handling.lock_delay_ms));
    }
}

bool Game::tryMove(int dx,int dy,bool reset_lock,bool count_finesse){
    if(active_.piece==Piece::None) return false;
    auto t=active_;
    t.x+=dx; t.y+=dy;
    if(board_.collides(t)) return false;
    active_=t;if(count_finesse)++piece_input_count_;if(dx!=0){last_action_rotation_=false;last_kick_index_=-1;}
    refreshGrounded(reset_lock);return true;
}

bool Game::tryRotate(int delta){
    if(active_.piece==Piece::None) return false;
    if(delta==2&&!rules_.handling.allow_180) return false;
    auto from=active_.rot,to=rotated(from,delta);auto tests=(std::abs(delta)==2)?kickTests180(active_.piece,from):kickTests(active_.piece,from,to);
    int idx=0;for(auto k:tests){auto t=active_;t.rot=to;t.x+=k.x;t.y+=k.y;if(!board_.collides(t)){
        active_=t;last_action_rotation_=true;last_kick_index_=idx;++piece_input_count_;++stats_.rotations;refreshGrounded(true);return true;}++idx;}
    return false;
}

void Game::doHold(){
    if(hold_used_||active_.piece==Piece::None) return;
    Piece cur=active_.piece;
    Piece incoming=hold_;
    hold_=cur; ++stats_.holds; hold_used_=true;
    if(incoming==Piece::None)incoming=bag_.pop();
    active_.piece=incoming;active_.rot=Rotation::Spawn;active_.x=3;active_.y=3;grounded_=false;lock_deadline_us_=kNever;lock_resets_=0;
    last_action_rotation_=false;last_kick_index_=-1;piece_input_count_=0;piece_spawn_x_=3;piece_spawn_rot_=Rotation::Spawn;
    if(board_.collides(active_)){game_over_=true;active_.piece=Piece::None;return;}refreshGrounded(false);scheduleGravityFromNow();
    hold_used_=true; // spawn-like reset above must not permit a second hold.
}

int Game::ghostY() const {if(active_.piece==Piece::None)return active_.y;auto t=active_;while(true){auto n=t;++n.y;if(board_.collides(n))break;t=n;}return t.y;}
void Game::hardDrop(){
    if(active_.piece==Piece::None) return;
    int d=0;
    while(tryMove(0,1,false,false)) ++d;
    if(d>0){last_action_rotation_=false;last_kick_index_=-1;}
    stats_.score+=2*d; ++stats_.hard_drops; lockPiece();
}
void Game::softSonicDrop(){if(active_.piece==Piece::None)return;int d=0;while(tryMove(0,1,false,false))++d;if(d>0){last_action_rotation_=false;last_kick_index_=-1;}stats_.score+=d;stats_.soft_drop_cells+=d;refreshGrounded(false);}

Spin Game::detectTSpin() const {
    if(active_.piece!=Piece::T||!last_action_rotation_)return Spin::None;
    int px=active_.x+1,py=active_.y+1;std::array<Vec2,4> c{{{px-1,py-1},{px+1,py-1},{px-1,py+1},{px+1,py+1}}};
    int occ=0;for(auto p:c)if(board_.occupied(p.x,p.y))++occ;if(occ<3)return Spin::None;
    std::array<Vec2,2> f{};
    switch(active_.rot){
        case Rotation::Spawn:f={c[0],c[1]};break;case Rotation::Right:f={c[1],c[3]};break;
        case Rotation::Reverse:f={c[2],c[3]};break;case Rotation::Left:f={c[0],c[2]};break;
    }
    int front=board_.occupied(f[0].x,f[0].y)+board_.occupied(f[1].x,f[1].y);
    return (front==2||last_kick_index_==4)?Spin::Full:Spin::Mini;
}
ClearKind Game::classifyClear(Spin s,int n) const {
    if(s==Spin::Full){if(n==0)return ClearKind::TSpinNoLine;if(n==1)return ClearKind::TSpinSingle;if(n==2)return ClearKind::TSpinDouble;return ClearKind::TSpinTriple;}
    if(s==Spin::Mini){return n==0?ClearKind::MiniNoLine:ClearKind::MiniSingle;}
    if(n==1) return ClearKind::Single;
    if(n==2) return ClearKind::Double;
    if(n==3) return ClearKind::Triple;
    if(n>=4) return ClearKind::Quad;
    return ClearKind::None;
}

int Game::estimatedOptimalFinesseInputs() const {
    int horizontal=(active_.x==piece_spawn_x_)?0:1;int rd=(static_cast<int>(active_.rot)-static_cast<int>(piece_spawn_rot_)+4)%4;int rotation=0;
    if(rd==1||rd==3)rotation=1;else if(rd==2)rotation=rules_.handling.allow_180?1:2;return horizontal+rotation;
}

void Game::lockPiece(){
    if(active_.piece==Piece::None||game_over_||complete_)return;
    Spin spin=detectTSpin();int finesse_opt=estimatedOptimalFinesseInputs();if(piece_input_count_>finesse_opt)stats_.finesse_faults+=piece_input_count_-finesse_opt;
    board_.stamp(active_);auto cr=board_.clearFullLines();bool pc=cr.lines>0&&board_.perfectClear();last_clear_=classifyClear(spin,cr.lines);
    if(cr.lines>0){++stats_.combo;stats_.max_combo=std::max(stats_.max_combo,stats_.combo);}else stats_.combo=-1;
    bool difficult=(last_clear_==ClearKind::Quad||last_clear_==ClearKind::MiniSingle||last_clear_==ClearKind::TSpinSingle||last_clear_==ClearKind::TSpinDouble||last_clear_==ClearKind::TSpinTriple);
    bool had_b2b=stats_.b2b_chain>0;auto sr=scoreClear(last_clear_,stats_.combo,had_b2b,pc,level());stats_.score+=sr.points;
    if(cr.lines>0){if(difficult){++stats_.b2b_chain;stats_.max_b2b=std::max(stats_.max_b2b,stats_.b2b_chain);}else stats_.b2b_chain=0;}
    stats_.lines+=cr.lines;stats_.garbage_lines_cleared+=cr.garbage_lines;stats_.attacks+=sr.attack;
    if(last_clear_==ClearKind::Quad) ++stats_.quads;
    if(spin!=Spin::None) ++stats_.tspins;
    if(pc) ++stats_.perfect_clears;
    ++stats_.pieces;
    int uncancelled=garbage_.cancel(sr.attack);outgoing_attack_+=uncancelled;last_attack_visual_=uncancelled;
    if(mode_==Mode::Cheese40 && cr.garbage_lines>0){
        int remain=std::max(0,40-stats_.garbage_lines_cleared);int add=std::min(cr.garbage_lines,remain);
        for(int i=0;i<add;++i){int hole=static_cast<int>(cheese_rng_.bounded(kBoardW));if(!board_.addGarbageLine(hole)){game_over_=true;break;}}
    }
    applyReadyGarbage();checkModeCompletion();
    if(!game_over_&&!complete_)spawn();else{active_.piece=Piece::None;next_gravity_us_=lock_deadline_us_=kNever;}
}

void Game::seedCheese(){for(int i=0;i<10;++i){int h=static_cast<int>(cheese_rng_.bounded(kBoardW));board_.addGarbageLine(h);}}
void Game::checkModeCompletion(){
    if((mode_==Mode::Sprint40||mode_==Mode::SeedRace)&&stats_.lines>=40)complete_=true;
    else if(mode_==Mode::Cheese40&&stats_.garbage_lines_cleared>=40)complete_=true;
    else if(mode_==Mode::Marathon&&stats_.lines>=150)complete_=true;
}

void Game::applyReadyGarbage(){
    if(game_over_) return;
    int cap=std::max(0,rules_.garbage_cap);
    int n=std::min(cap,garbage_.readyLines(now_us_));
    for(int i=0;i<n;++i){int h=garbage_.popReadyHole(now_us_,std::clamp(rules_.garbage_messiness_pct,0,100));if(h<0)break;if(!board_.addGarbageLine(h)){game_over_=true;break;}}
}
void Game::enqueueGarbage(int lines,TimeUs ready,int hole){garbage_.enqueue(lines,ready,hole);}
int Game::consumeOutgoingAttack(){int n=outgoing_attack_;outgoing_attack_=0;return n;}

void Game::setHorizontal(int dir,bool down){
    bool& held=(dir<0)?left_held_:right_held_;if(held==down)return;held=down;
    if(down){
        bool switching=(horiz_dir_!=0&&horiz_dir_!=dir);horiz_dir_=dir;tryMove(dir,0,true,true);
        TimeUs charge=ms(std::max(0,rules_.handling.das_ms));if(switching)charge=std::max<TimeUs>(0,charge-ms(std::max(0,rules_.handling.dcd_ms)));
        next_horizontal_us_=now_us_+charge;
    }else if(horiz_dir_==dir){
        int other=(dir<0?(right_held_?1:0):(left_held_?-1:0));horiz_dir_=other;
        if(other!=0){tryMove(other,0,true,true);next_horizontal_us_=now_us_+std::max<TimeUs>(0,ms(std::max(0,rules_.handling.das_ms))-ms(std::max(0,rules_.handling.dcd_ms)));}
        else next_horizontal_us_=kNever;
    }
}
void Game::processHorizontalRepeat(){
    if(horiz_dir_==0){next_horizontal_us_=kNever;return;}
    if(rules_.handling.arr_ms<=0){while(tryMove(horiz_dir_,0,true,false)){}next_horizontal_us_=kNever;}
    else{tryMove(horiz_dir_,0,true,false);next_horizontal_us_=now_us_+ms(rules_.handling.arr_ms);}
}

void Game::press(Action a){
    if(a==Action::Pause){paused_=!paused_;return;}if(a==Action::Restart){restart(seed_,mode_);return;}if(paused_||game_over_||complete_)return;
    ++stats_.inputs;
    switch(a){
        case Action::Left:setHorizontal(-1,true);break;case Action::Right:setHorizontal(1,true);break;
        case Action::SoftDrop:if(!soft_held_){soft_held_=true;if(rules_.handling.sdf<=0)softSonicDrop();scheduleGravityFromNow();}break;
        case Action::HardDrop:hardDrop();break;
        case Action::RotateCW:cw_held_=true;tryRotate(1);break;
        case Action::RotateCCW:ccw_held_=true;tryRotate(-1);break;
        case Action::Rotate180:rot180_held_=true;tryRotate(2);break;
        case Action::Hold:hold_held_=true;doHold();break;default:break;
    }
}
void Game::release(Action a){
    if(a==Action::Left)setHorizontal(-1,false);
    else if(a==Action::Right)setHorizontal(1,false);
    else if(a==Action::SoftDrop&&soft_held_){soft_held_=false;scheduleGravityFromNow();}
    else if(a==Action::RotateCW)cw_held_=false;
    else if(a==Action::RotateCCW)ccw_held_=false;
    else if(a==Action::Rotate180)rot180_held_=false;
    else if(a==Action::Hold)hold_held_=false;
}

void Game::advanceTo(TimeUs target){
    if(target<now_us_||game_over_||complete_)return;
    if(paused_)return;
    while(now_us_<target){
        if(game_over_||complete_)break;
        TimeUs next=target;next=std::min(next,next_horizontal_us_);next=std::min(next,next_gravity_us_);if(grounded_)next=std::min(next,lock_deadline_us_);
        if(mode_==Mode::Ultra120)next=std::min(next,TimeUs{120000000});
        if(next<now_us_) next=now_us_;
        now_us_=next; stats_.elapsed_us=now_us_;
        if(mode_==Mode::Ultra120&&now_us_>=120000000){complete_=true;active_.piece=Piece::None;break;}
        bool progressed=false;
        if(next_horizontal_us_==now_us_){processHorizontalRepeat();progressed=true;}
        if(!game_over_&&!complete_&&next_gravity_us_==now_us_){
            bool moved=tryMove(0,1,false,false);if(moved&&soft_held_&&rules_.handling.sdf>0){++stats_.score;++stats_.soft_drop_cells;}
            if(!moved) refreshGrounded(false);
            scheduleGravityFromNow(); progressed=true;
        }
        if(!game_over_&&!complete_&&grounded_&&lock_deadline_us_<=now_us_){lockPiece();progressed=true;}
        if(now_us_==target)break;
        if(!progressed && next==now_us_){++now_us_;stats_.elapsed_us=now_us_;}
    }
    stats_.elapsed_us=std::min(now_us_, mode_==Mode::Ultra120?TimeUs{120000000}:now_us_);
}

std::string Game::deterministicState() const {
    std::ostringstream o;o<<"fasttris-state-v1\n"<<seed_<<' '<<static_cast<int>(mode_)<<' '<<now_us_<<' '<<game_over_<<' '<<complete_<<'\n';
    for(int y=0;y<kBoardH;++y){o<<board_.rowMask(y)<<',';for(int x=0;x<kBoardW;++x)o<<static_cast<int>(board_.cell(x,y));o<<'\n';}
    o<<static_cast<int>(active_.piece)<<' '<<static_cast<int>(active_.rot)<<' '<<active_.x<<' '<<active_.y<<' '<<static_cast<int>(hold_)<<'\n';
    o<<stats_.score<<' '<<stats_.lines<<' '<<stats_.pieces<<' '<<stats_.attacks<<' '<<stats_.inputs<<' '<<stats_.holds<<' '<<stats_.rotations<<' '<<stats_.quads<<' '<<stats_.tspins<<' '<<stats_.perfect_clears<<' '<<stats_.combo<<' '<<stats_.b2b_chain<<' '<<stats_.finesse_faults<<'\n';
    auto q=bag_.queue();int n=0;for(auto p:q){o<<static_cast<int>(p)<<',';if(++n==14)break;}o<<'\n'<<bag_.rngState()<<'\n';return o.str();
}

} // namespace fasttris
