#pragma once
#include "types.hpp"

namespace fasttris {
Rules effectiveRulesForMode(const Rules& personal, Mode mode);
bool guidelineLockOut(const ActivePiece& piece, int hidden_rows);
}
