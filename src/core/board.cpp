#include "fasttris/board.hpp"
#include "fasttris/tetromino.hpp"
#include <algorithm>

namespace fasttris {
void Board::clear() {
    masks_.fill(0); for(auto& r:cells_) r.fill(0);
}
void Board::setHeight(int height) {
    height_=std::clamp(height,1,kMaxBoardH);
    clear();
}
bool Board::occupied(int x,int y) const {
    if (x<0 || x>=kBoardW || y>=height_) return true;
    if (y<0) return false;
    return (masks_[y] & (1u<<x))!=0;
}
Piece Board::cell(int x,int y) const {
    if(x<0||x>=kBoardW||y<0||y>=height_) return Piece::None;
    return static_cast<Piece>(cells_[y][x]);
}
bool Board::collides(const ActivePiece& a) const {
    for(auto b:blocks(a.piece,a.rot)) if(occupied(a.x+b.x,a.y+b.y)) return true;
    return false;
}
void Board::stamp(const ActivePiece& a) {
    for(auto b:blocks(a.piece,a.rot)) {
        int x=a.x+b.x,y=a.y+b.y; if(y<0||y>=height_||x<0||x>=kBoardW) continue;
        masks_[y]|=static_cast<std::uint16_t>(1u<<x); cells_[y][x]=static_cast<std::uint8_t>(a.piece);
    }
}
ClearResult Board::clearFullLines() {
    ClearResult out{}; int dst=height_-1;
    for(int src=height_-1;src>=0;--src) {
        if(masks_[src]==0x3FFu) {
            ++out.lines;
            bool g=false; for(auto c:cells_[src]) if(static_cast<Piece>(c)==Piece::Garbage){g=true;break;}
            if(g) ++out.garbage_lines;
            continue;
        }
        if(dst!=src){masks_[dst]=masks_[src];cells_[dst]=cells_[src];}
        --dst;
    }
    while(dst>=0){masks_[dst]=0;cells_[dst].fill(0);--dst;}
    return out;
}
bool Board::perfectClear() const { for(int y=0;y<height_;++y) if(masks_[y]) return false; return true; }
bool Board::addGarbageLine(int hole) {
    if(masks_[0]!=0) return false;
    for(int y=0;y<height_-1;++y){masks_[y]=masks_[y+1];cells_[y]=cells_[y+1];}
    auto& r=cells_[height_-1]; r.fill(static_cast<std::uint8_t>(Piece::Garbage));
    hole=std::clamp(hole,0,kBoardW-1); r[hole]=0;
    masks_[height_-1]=static_cast<std::uint16_t>(0x3FFu & ~(1u<<hole));
    return true;
}
} // namespace fasttris
