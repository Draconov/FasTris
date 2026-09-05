#pragma once
#include "bag.hpp"
#include "board.hpp"
#include "garbage.hpp"
#include "types.hpp"
#include "sha256.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace fasttris {

class Game {
public:
    Game(std::uint64_t seed=1, Mode mode=Mode::Sprint40, Rules rules={});
    void restart(std::uint64_t seed, Mode mode);
    void advanceTo(TimeUs target_us);
    void press(Action a);
    void release(Action a);
    void enqueueGarbage(int lines, TimeUs ready_us, int forced_hole=-1);
    int pendingGarbageLines() const;
    int consumeOutgoingAttack();

    const Board& board() const { return board_; }
    const ActivePiece& active() const { return active_; }
    Piece holdPiece() const { return hold_; }
    bool holdUsed() const { return hold_used_; }
    std::vector<Piece> next(std::size_t n) { return bag_.preview(n); }
    const Stats& stats() const { return stats_; }
    const Rules& rules() const { return rules_; }
    Rules& mutableRules() { return rules_; }
    std::uint64_t seed() const { return seed_; }
    Mode mode() const { return mode_; }
    TimeUs now() const { return now_us_; }
    bool gameOver() const { return game_over_; }
    bool complete() const { return complete_; }
    bool paused() const { return paused_; }
    int ghostY() const;
    int hiddenRows() const { return board_.height()-kVisibleH; }
    int level() const;
    std::string deterministicState() const;
    ClearKind lastClear() const { return last_clear_; }
    int lastAttack() const { return last_attack_visual_; }
    void setSemanticEventCapture(bool enabled);
    std::span<const GameEvent> semanticEvents() const { return semantic_events_; }
    void clearSemanticEvents() { semantic_events_.clear(); }

    friend Sha256Digest stateHash(const Game& game);

private:
    void spawn(Piece forced=Piece::None);
    int spawnY() const { return hiddenRows()-1; }
    bool tryMove(int dx, int dy, bool reset_lock, bool count_finesse=false);
    bool tryRotate(int delta);
    void doHold();
    void hardDrop();
    void softSonicDrop();
    void lockPiece();
    void refreshGrounded(bool request_reset);
    bool isGrounded() const;
    Spin detectTSpin() const;
    ClearKind classifyClear(Spin spin, int lines) const;
    TimeUs baseGravityInterval() const;
    TimeUs currentGravityInterval() const;
    TimeUs modeTimeLimit() const;
    void scheduleGravityFromNow();
    void processHorizontalRepeat();
    void setHorizontal(int dir, bool down);
    void applyReadyGarbage();
    void seedCheese();
    void seedStartingGarbage(int lines);
    void checkModeCompletion();
    int estimatedOptimalFinesseInputs() const;
    void emitSemanticEvent(GameEventKind kind,int value);

    std::uint64_t seed_{};
    Mode mode_{};
    Rules rules_{};
    Board board_{};
    Bag7 bag_{};
    GarbageQueue garbage_{};
    Pcg32 cheese_rng_{};
    ActivePiece active_{};
    Piece hold_{Piece::None};
    bool hold_used_{};
    Stats stats_{};

    TimeUs now_us_{};
    TimeUs next_gravity_us_{kNever};
    TimeUs lock_deadline_us_{kNever};
    TimeUs next_horizontal_us_{kNever};
    TimeUs next_spawn_us_{kNever};
    bool grounded_{};
    int lock_resets_{};
    bool game_over_{};
    bool complete_{};
    bool paused_{};

    bool left_held_{};
    bool right_held_{};
    bool soft_held_{};
    bool cw_held_{};
    bool ccw_held_{};
    bool rot180_held_{};
    bool hold_held_{};
    int horiz_dir_{};
    int outgoing_attack_{};
    int last_attack_visual_{};

    bool last_action_rotation_{};
    int last_kick_index_{-1};
    ClearKind last_clear_{ClearKind::None};
    int piece_input_count_{};
    int piece_spawn_x_{3};
    Rotation piece_spawn_rot_{Rotation::Spawn};
    bool capture_semantic_events_{};
    std::vector<GameEvent> semantic_events_;
};

} // namespace fasttris
