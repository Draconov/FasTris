#pragma once
#include "types.hpp"
#include <array>
#include <span>

namespace fasttris {
using Blocks = std::array<Vec2,4>;
const Blocks& blocks(Piece p, Rotation r);
std::span<const Vec2> kickTests(Piece p, Rotation from, Rotation to);
std::span<const Vec2> kickTests180(Piece p, Rotation from);
Rotation rotated(Rotation r, int delta);
} // namespace fasttris
