#pragma once
#include <cstdint>

namespace fasttris {

class Pcg32 {
public:
    Pcg32() { seed(0, 0xda3e39cb94b95bdbULL); }
    explicit Pcg32(std::uint64_t initstate, std::uint64_t stream=0xda3e39cb94b95bdbULL) { seed(initstate, stream); }

    void seed(std::uint64_t initstate, std::uint64_t stream) {
        state_ = 0;
        inc_ = (stream << 1u) | 1u;
        next();
        state_ += initstate;
        next();
    }
    std::uint32_t next() {
        std::uint64_t oldstate = state_;
        state_ = oldstate * 6364136223846793005ULL + inc_;
        std::uint32_t xorshifted = static_cast<std::uint32_t>(((oldstate >> 18u) ^ oldstate) >> 27u);
        std::uint32_t rot = static_cast<std::uint32_t>(oldstate >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
    }
    std::uint32_t bounded(std::uint32_t bound) {
        if (bound == 0) return 0;
        std::uint32_t threshold = static_cast<std::uint32_t>(-bound) % bound;
        for (;;) { auto r = next(); if (r >= threshold) return r % bound; }
    }
    std::uint64_t state() const { return state_; }
    std::uint64_t stream() const { return inc_; }
private:
    std::uint64_t state_{};
    std::uint64_t inc_{};
};

inline std::uint64_t splitMix64(std::uint64_t x) {
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

} // namespace fasttris
