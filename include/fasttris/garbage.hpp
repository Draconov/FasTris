#pragma once
#include "types.hpp"
#include "rng.hpp"
#include <deque>
#include <cstdint>

namespace fasttris {
struct GarbagePacket { int lines{}; TimeUs ready_us{}; int hole{-1}; };
class GarbageQueue {
public:
    explicit GarbageQueue(std::uint64_t seed=1):rng_(seed,0xBADC0DEULL){}
    void reset(std::uint64_t seed){q_.clear();rng_.seed(seed,0xBADC0DEULL);last_hole_=-1;}
    void enqueue(int lines, TimeUs ready, int forced_hole=-1){ if(lines>0) q_.push_back({lines,ready,forced_hole}); }
    int cancel(int attack);
    int readyLines(TimeUs now) const;
    int popReadyHole(TimeUs now, int messiness_pct);
    bool empty() const { return q_.empty(); }
private:
    std::deque<GarbagePacket> q_;
    Pcg32 rng_;
    int last_hole_{-1};
};
} // namespace fasttris
