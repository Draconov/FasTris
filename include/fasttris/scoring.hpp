#pragma once
#include "types.hpp"
namespace fasttris {
struct ScoreResult { int points{}; int attack{}; bool difficult{}; };
ScoreResult scoreClear(ClearKind kind, int combo, bool b2b, bool perfect_clear, int level=1, int simulation_version=kSimulationRulesVersion);
} // namespace fasttris
