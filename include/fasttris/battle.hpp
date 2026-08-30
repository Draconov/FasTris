#pragma once
#include "game.hpp"
namespace fasttris {
class Battle {
public:
    Battle(std::uint64_t seed_a,std::uint64_t seed_b,Rules a={},Rules b={}) : a_(seed_a,Mode::Zen,a),b_(seed_b,Mode::Zen,b){}
    void advanceTo(TimeUs t){a_.advanceTo(t);b_.advanceTo(t);route();}
    void press(int player,Action x){(player==0?a_:b_).press(x);route();}
    void release(int player,Action x){(player==0?a_:b_).release(x);route();}
    Game& game(int p){return p==0?a_:b_;} const Game& game(int p)const{return p==0?a_:b_;}
private:
    void route(){int aa=a_.consumeOutgoingAttack(),bb=b_.consumeOutgoingAttack();if(aa>0)b_.enqueueGarbage(aa,a_.now()+static_cast<TimeUs>(b_.rules().garbage_delay_ms)*1000);if(bb>0)a_.enqueueGarbage(bb,b_.now()+static_cast<TimeUs>(a_.rules().garbage_delay_ms)*1000);}
    Game a_,b_;
};
} // namespace fasttris
