#include "fasttris/scoring.hpp"
#include <algorithm>
namespace fasttris {
ScoreResult scoreClear(ClearKind k,int combo,bool b2b,bool pc,int level){
    int base=0,atk=0;bool diff=false;
    switch(k){
        case ClearKind::Single:base=100;break; case ClearKind::Double:base=300;atk=1;break;
        case ClearKind::Triple:base=500;atk=2;break; case ClearKind::Quad:base=800;atk=4;diff=true;break;
        case ClearKind::MiniNoLine:base=100;diff=true;break; case ClearKind::MiniSingle:base=200;atk=1;diff=true;break;
        case ClearKind::TSpinNoLine:base=400;diff=true;break; case ClearKind::TSpinSingle:base=800;atk=2;diff=true;break;
        case ClearKind::TSpinDouble:base=1200;atk=4;diff=true;break; case ClearKind::TSpinTriple:base=1600;atk=6;diff=true;break;
        default:break;
    }
    if(diff&&b2b){base=base*3/2;if(atk>0)++atk;}
    if(combo>0){base+=50*combo; static constexpr int ctab[]{0,0,1,1,2,2,3,3,4,4,5,5}; atk+=ctab[std::min(combo,11)];}
    if(pc){base+=3500;atk+=10;}
    return {base*std::max(1,level),atk,diff};
}
} // namespace fasttris
