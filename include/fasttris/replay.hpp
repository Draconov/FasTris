#pragma once
#include "game.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fasttris {

inline constexpr std::size_t kMaxReplayBytes = 16u * 1024u * 1024u;
inline constexpr std::size_t kMaxReplayEvents = 1'000'000u;
inline constexpr TimeUs kMaxReplayDurationUs = 12LL * 60LL * 60LL * 1'000'000LL;
inline constexpr TimeUs kReplayCheckpointIntervalUs = 5'000'000;
inline constexpr TimeUs kReplayIndexSimulationSliceUs = 250'000;

struct ReplayEvent {
    TimeUs time_us{};
    Action action{Action::Left};
    bool down{};
    bool operator==(const ReplayEvent&) const = default;
};

struct Replay {
    std::uint64_t seed{1};
    Mode mode{Mode::Sprint40};
    Rules rules{};
    TimeUs duration_us{};
    std::vector<ReplayEvent> events;
    std::string final_hash;
};

struct ReplayMarker {
    TimeUs time_us{};
    std::size_t event_index{}; // Index of the next replay event after this marker.
    int value{};
};

struct ReplayCheckpoint {
    TimeUs time_us{};
    std::size_t event_index{}; // Index of the next replay event after this state.
    Game game;
};

struct ReplayIndex {
    std::vector<ReplayCheckpoint> checkpoints;
    std::vector<ReplayMarker> pieces;
    std::vector<ReplayMarker> line_clears;
    std::vector<ReplayMarker> tspins;
    std::vector<ReplayMarker> perfect_clears;
    std::optional<bool> verification;
};

// Incrementally re-simulates one replay. The app can call step() inside a small
// per-frame time budget. The same pass verifies the replay and builds all seek
// and analysis indexes, so that work is never duplicated.
class ReplayIndexBuilder {
public:
    explicit ReplayIndexBuilder(const Replay& replay);
    bool step(); // Returns true when useful work was performed.
    bool finished() const { return finished_; }
    TimeUs processedThrough() const { return playhead_; }
    const ReplayIndex& index() const { return index_; }

private:
    void recordStats(const Stats& before, const Stats& after, TimeUs at, std::size_t next_event);
    void advanceAndRecord(TimeUs target);
    void addCheckpoint(TimeUs at);

    const Replay* replay_{};
    Game game_;
    std::size_t event_index_{};
    TimeUs playhead_{};
    TimeUs next_checkpoint_us_{kReplayCheckpointIntervalUs};
    ReplayIndex index_;
    bool finished_{};
};

std::string stateHash(const Game& game);
bool validateReplay(const Replay& replay, std::string* error=nullptr);

// Current .ftr format is compact binary. std::string is used as an owning byte
// buffer and may contain NUL bytes.
std::string serializeReplay(const Replay& replay);
bool deserializeReplay(std::string_view bytes, Replay& out, std::string* error=nullptr);
bool saveReplay(const Replay& replay, const std::string& path, std::string* error=nullptr);
bool loadReplay(const std::string& path, Replay& out, std::string* error=nullptr);
bool verifyReplay(const Replay& replay, std::string* actual_hash=nullptr);


} // namespace fasttris
