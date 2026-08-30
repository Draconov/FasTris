#include "fasttris/garbage.hpp"
#include <algorithm>
namespace fasttris {
int GarbageQueue::cancel(int attack){
    while(attack>0&&!q_.empty()){
        int n=std::min(attack,q_.front().lines);q_.front().lines-=n;attack-=n;if(q_.front().lines==0)q_.pop_front();
    }
    return attack;
}
int GarbageQueue::readyLines(TimeUs now) const { int n=0;for(auto&p:q_){if(p.ready_us>now)break;n+=p.lines;}return n; }
int GarbageQueue::popReadyHole(TimeUs now,int messiness_pct){
    if(q_.empty()||q_.front().ready_us>now)return -1;
    auto &p=q_.front(); int hole=p.hole;
    if(hole<0){
        bool change=last_hole_<0 || static_cast<int>(rng_.bounded(100))<messiness_pct;
        if(change){int nh=static_cast<int>(rng_.bounded(kBoardW)); if(kBoardW>1&&nh==last_hole_)nh=(nh+1+static_cast<int>(rng_.bounded(kBoardW-1)))%kBoardW; last_hole_=nh;}
        hole=last_hole_;
    }
    if(--p.lines==0) q_.pop_front();
    return hole;
}
} // namespace fasttris
