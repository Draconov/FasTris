#pragma once
#include "types.hpp"
#include "rng.hpp"
#include <array>
#include <deque>
#include <vector>

namespace fasttris {
class Bag7 {
public:
    explicit Bag7(std::uint64_t seed=0) : rng_(seed, 0xC0FFEE1234ULL) { ensure(21); }
    void reset(std::uint64_t seed) { rng_.seed(seed, 0xC0FFEE1234ULL); q_.clear(); ensure(21); }
    Piece pop() { ensure(14); auto p=q_.front(); q_.pop_front(); ensure(14); return p; }
    Piece peek(std::size_t i) { ensure(i+1); return q_[i]; }
    std::vector<Piece> preview(std::size_t n) { ensure(n); return {q_.begin(), q_.begin()+static_cast<std::ptrdiff_t>(n)}; }
    std::uint64_t rngState() const { return rng_.state(); }
    const std::deque<Piece>& queue() const { return q_; }
private:
    void ensure(std::size_t n) {
        while (q_.size() < n) {
            std::array<Piece,7> bag{Piece::I,Piece::J,Piece::L,Piece::O,Piece::S,Piece::T,Piece::Z};
            for (int i=6;i>0;--i) { int j=static_cast<int>(rng_.bounded(static_cast<std::uint32_t>(i+1))); std::swap(bag[i],bag[j]); }
            for (auto p:bag) q_.push_back(p);
        }
    }
    Pcg32 rng_;
    std::deque<Piece> q_;
};
} // namespace fasttris
