#pragma once
#include "types.hpp"
#include <array>
#include <cstdint>

namespace fasttris {

struct ClearResult { int lines{}; int garbage_lines{}; };

class Board {
public:
    Board() { clear(); }
    void clear();
    bool occupied(int x, int y) const;
    Piece cell(int x, int y) const;
    bool collides(const ActivePiece& a) const;
    void stamp(const ActivePiece& a);
    ClearResult clearFullLines();
    bool perfectClear() const;
    bool addGarbageLine(int hole);
    std::uint16_t rowMask(int y) const { return masks_[y]; }
    const std::array<std::array<std::uint8_t,kBoardW>,kBoardH>& cells() const { return cells_; }
private:
    std::array<std::uint16_t,kBoardH> masks_{};
    std::array<std::array<std::uint8_t,kBoardW>,kBoardH> cells_{};
};

} // namespace fasttris
