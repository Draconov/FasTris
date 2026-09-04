#include "fasttris/rules.hpp"
#include "fasttris/tetromino.hpp"
#include <algorithm>

namespace fasttris {
Rules effectiveRulesForMode(const Rules& personal, Mode mode) {
    Rules rules=personal;

    if(mode==Mode::SeedRace){
        rules.handling.allow_180=true;
        rules.handling.irs=true;
        rules.handling.ihs=true;
        rules.handling.lock_delay_ms=500;
        rules.handling.max_lock_resets=15;
        rules.ghost=true;
        rules.next_count=5;
    }

    if(rules.tournament){
        rules.handling.allow_180=false;
        rules.handling.irs=true;
        rules.handling.ihs=true;
        rules.handling.lock_delay_ms=500;
        rules.handling.max_lock_resets=15;
        rules.ghost=true;
        rules.next_count=5;
        rules.garbage_cap=8;
        rules.garbage_delay_ms=500;
        rules.garbage_messiness_pct=25;
    }

    if(rules.guideline){
        rules.handling.das_ms=167;
        rules.handling.arr_ms=33;
        rules.handling.sdf=20;
        rules.handling.dcd_ms=0;
        rules.handling.lock_delay_ms=500;
        rules.handling.max_lock_resets=15;
        rules.handling.allow_180=false;
        rules.handling.irs=true;
        rules.handling.ihs=true;
        rules.ghost=true;
        rules.next_count=5;
        rules.hidden_rows=kGuidelineHiddenH;
        rules.entry_delay_ms=100;
        rules.garbage_cap=8;
        rules.garbage_delay_ms=500;
        rules.garbage_messiness_pct=25;
    }else{
        rules.hidden_rows=kHiddenH;
        rules.entry_delay_ms=0;
    }
    return rules;
}

bool guidelineLockOut(const ActivePiece& piece, int hidden_rows) {
    if(piece.piece==Piece::None)return false;
    for(const auto block:blocks(piece.piece,piece.rot))
        if(piece.y+block.y>=hidden_rows)return false;
    return true;
}
}
