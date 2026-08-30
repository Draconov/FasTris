#include "fasttris/scoring.hpp"
#include <algorithm>
#include <array>

namespace fasttris {
namespace {
ScoreResult scoreLegacy(ClearKind k,int combo,bool b2b,bool pc,int level){
    int base=0,atk=0;bool diff=false;
    switch(k){
        case ClearKind::Single:base=100;break; case ClearKind::Double:base=300;atk=1;break;
        case ClearKind::Triple:base=500;atk=2;break; case ClearKind::Quad:base=800;atk=4;diff=true;break;
        case ClearKind::MiniNoLine:base=100;diff=true;break; case ClearKind::MiniSingle:base=200;atk=1;diff=true;break;
        case ClearKind::MiniDouble:base=400;atk=1;diff=true;break;
        case ClearKind::TSpinNoLine:base=400;diff=true;break; case ClearKind::TSpinSingle:base=800;atk=2;diff=true;break;
        case ClearKind::TSpinDouble:base=1200;atk=4;diff=true;break; case ClearKind::TSpinTriple:base=1600;atk=6;diff=true;break;
        default:break;
    }
    if(diff&&b2b){base=base*3/2;if(atk>0)++atk;}
    if(combo>0){base+=50*combo; static constexpr int ctab[]{0,0,1,1,2,2,3,3,4,4,5,5}; atk+=ctab[std::min(combo,11)];}
    if(pc){base+=3500;atk+=10;}
    return {base*std::max(1,level),atk,diff};
}

int linesForClear(ClearKind k){
    switch(k){
        case ClearKind::Single: case ClearKind::MiniSingle: case ClearKind::TSpinSingle: return 1;
        case ClearKind::Double: case ClearKind::MiniDouble: case ClearKind::TSpinDouble: return 2;
        case ClearKind::Triple: case ClearKind::TSpinTriple: return 3;
        case ClearKind::Quad: return 4;
        default: return 0;
    }
}
}

ScoreResult scoreClear(ClearKind k,int combo,bool b2b,bool pc,int level,int simulation_version){
    if(simulation_version<=1) return scoreLegacy(k,combo,b2b,pc,level);

    int base=0,atk=0;
    bool difficult_line=false;
    switch(k){
        case ClearKind::Single: base=100; break;
        case ClearKind::Double: base=300; atk=1; break;
        case ClearKind::Triple: base=500; atk=2; break;
        case ClearKind::Quad: base=800; atk=4; difficult_line=true; break;
        case ClearKind::MiniNoLine: base=100; break;
        case ClearKind::MiniSingle: base=200; difficult_line=true; break;
        case ClearKind::MiniDouble: base=400; atk=1; difficult_line=true; break;
        case ClearKind::TSpinNoLine: base=400; break;
        case ClearKind::TSpinSingle: base=800; atk=2; difficult_line=true; break;
        case ClearKind::TSpinDouble: base=1200; atk=4; difficult_line=true; break;
        case ClearKind::TSpinTriple: base=1600; atk=6; difficult_line=true; break;
        default: break;
    }

    if(difficult_line && b2b){
        base=base*3/2;
        if(atk>0) ++atk;
    }

    if(combo>0){
        base += 50*combo;
        // Guideline-style combo attack progression. combo==0 is the first clear.
        static constexpr std::array<int,14> ctab{0,0,1,1,2,2,3,3,4,4,4,5,5,5};
        atk += ctab[std::min(combo,13)];
    }

    if(pc){
        const int lines=linesForClear(k);
        int pc_bonus=0;
        if(lines==1) pc_bonus=800;
        else if(lines==2) pc_bonus=1200;
        else if(lines==3) pc_bonus=1800;
        else if(lines==4) pc_bonus=(k==ClearKind::Quad && b2b)?3200:2000;
        base += pc_bonus;
        atk += 10;
    }

    return {base*std::max(1,level),atk,difficult_line};
}
} // namespace fasttris
