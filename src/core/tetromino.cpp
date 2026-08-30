#include "fasttris/tetromino.hpp"
#include <array>

namespace fasttris {
namespace {
constexpr Blocks E{{{0,0},{0,0},{0,0},{0,0}}};
constexpr std::array<std::array<Blocks,4>,8> SHAPES{{
    {E,E,E,E},
    // I
    {{{{{0,1},{1,1},{2,1},{3,1}}}, {{{2,0},{2,1},{2,2},{2,3}}}, {{{0,2},{1,2},{2,2},{3,2}}}, {{{1,0},{1,1},{1,2},{1,3}}}}},
    // J
    {{{{{0,0},{0,1},{1,1},{2,1}}}, {{{1,0},{2,0},{1,1},{1,2}}}, {{{0,1},{1,1},{2,1},{2,2}}}, {{{1,0},{1,1},{0,2},{1,2}}}}},
    // L
    {{{{{2,0},{0,1},{1,1},{2,1}}}, {{{1,0},{1,1},{1,2},{2,2}}}, {{{0,1},{1,1},{2,1},{0,2}}}, {{{0,0},{1,0},{1,1},{1,2}}}}},
    // O
    {{{{{1,0},{2,0},{1,1},{2,1}}}, {{{1,0},{2,0},{1,1},{2,1}}}, {{{1,0},{2,0},{1,1},{2,1}}}, {{{1,0},{2,0},{1,1},{2,1}}}}},
    // S
    {{{{{1,0},{2,0},{0,1},{1,1}}}, {{{1,0},{1,1},{2,1},{2,2}}}, {{{1,1},{2,1},{0,2},{1,2}}}, {{{0,0},{0,1},{1,1},{1,2}}}}},
    // T
    {{{{{1,0},{0,1},{1,1},{2,1}}}, {{{1,0},{1,1},{2,1},{1,2}}}, {{{0,1},{1,1},{2,1},{1,2}}}, {{{1,0},{0,1},{1,1},{1,2}}}}},
    // Z
    {{{{{0,0},{1,0},{1,1},{2,1}}}, {{{2,0},{1,1},{2,1},{1,2}}}, {{{0,1},{1,1},{1,2},{2,2}}}, {{{1,0},{0,1},{1,1},{0,2}}}}}
}};

using K5 = std::array<Vec2,5>;
constexpr K5 NONE{{{0,0},{0,0},{0,0},{0,0},{0,0}}};
// Board coordinates: +y is down. These are SRS tables converted from +y-up notation.
constexpr K5 JL_0R{{{0,0},{-1,0},{-1,-1},{0,2},{-1,2}}};
constexpr K5 JL_R0{{{0,0},{1,0},{1,1},{0,-2},{1,-2}}};
constexpr K5 JL_R2{{{0,0},{1,0},{1,1},{0,-2},{1,-2}}};
constexpr K5 JL_2R{{{0,0},{-1,0},{-1,-1},{0,2},{-1,2}}};
constexpr K5 JL_2L{{{0,0},{1,0},{1,-1},{0,2},{1,2}}};
constexpr K5 JL_L2{{{0,0},{-1,0},{-1,1},{0,-2},{-1,-2}}};
constexpr K5 JL_L0{{{0,0},{-1,0},{-1,1},{0,-2},{-1,-2}}};
constexpr K5 JL_0L{{{0,0},{1,0},{1,-1},{0,2},{1,2}}};

constexpr K5 I_0R{{{0,0},{-2,0},{1,0},{-2,1},{1,-2}}};
constexpr K5 I_R0{{{0,0},{2,0},{-1,0},{2,-1},{-1,2}}};
constexpr K5 I_R2{{{0,0},{-1,0},{2,0},{-1,-2},{2,1}}};
constexpr K5 I_2R{{{0,0},{1,0},{-2,0},{1,2},{-2,-1}}};
constexpr K5 I_2L{{{0,0},{2,0},{-1,0},{2,-1},{-1,2}}};
constexpr K5 I_L2{{{0,0},{-2,0},{1,0},{-2,1},{1,-2}}};
constexpr K5 I_L0{{{0,0},{1,0},{-2,0},{1,2},{-2,-1}}};
constexpr K5 I_0L{{{0,0},{-1,0},{2,0},{-1,-2},{2,1}}};

constexpr std::array<Vec2,7> K180{{{0,0},{0,-1},{1,0},{-1,0},{2,0},{-2,0},{0,1}}};

const K5& k90(Piece p, Rotation a, Rotation b) {
    const bool i = p==Piece::I;
    if (a==Rotation::Spawn && b==Rotation::Right) return i?I_0R:JL_0R;
    if (a==Rotation::Right && b==Rotation::Spawn) return i?I_R0:JL_R0;
    if (a==Rotation::Right && b==Rotation::Reverse) return i?I_R2:JL_R2;
    if (a==Rotation::Reverse && b==Rotation::Right) return i?I_2R:JL_2R;
    if (a==Rotation::Reverse && b==Rotation::Left) return i?I_2L:JL_2L;
    if (a==Rotation::Left && b==Rotation::Reverse) return i?I_L2:JL_L2;
    if (a==Rotation::Left && b==Rotation::Spawn) return i?I_L0:JL_L0;
    if (a==Rotation::Spawn && b==Rotation::Left) return i?I_0L:JL_0L;
    return NONE;
}
}

const Blocks& blocks(Piece p, Rotation r) {
    auto pi=static_cast<std::size_t>(p); auto ri=static_cast<std::size_t>(r);
    if (pi>=SHAPES.size()) return E;
    return SHAPES[pi][ri&3u];
}
Rotation rotated(Rotation r, int delta) {
    int v=(static_cast<int>(r)+delta)%4; if(v<0)v+=4; return static_cast<Rotation>(v);
}
std::span<const Vec2> kickTests(Piece p, Rotation from, Rotation to) {
    if (p==Piece::O) { static constexpr std::array<Vec2,1> o{{{0,0}}}; return o; }
    return k90(p,from,to);
}
std::span<const Vec2> kickTests180(Piece, Rotation) { return K180; }

} // namespace fasttris
