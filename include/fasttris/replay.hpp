#pragma once
#include "game.hpp"
#include "sha256.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fasttris {

inline constexpr std::size_t kMaxReplayBytes = 16u * 1024u * 1024u;
inline constexpr std::size_t kMaxReplayEvents = 1'000'000u;
inline constexpr TimeUs kMaxReplayDurationUs = 12LL * 60LL * 60LL * 1'000'000LL;
inline constexpr TimeUs kReplayCheckpointBaseIntervalUs = 5'000'000;
inline constexpr std::size_t kMaxReplayCheckpoints = 2048u;
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
    std::optional<Sha256Digest> final_hash;
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
    TimeUs checkpoint_interval_us{kReplayCheckpointBaseIntervalUs};
    std::vector<ReplayCheckpoint> checkpoints;
    std::vector<ReplayMarker> pieces;
    std::vector<ReplayMarker> line_clears;
    std::vector<ReplayMarker> tspins;
    std::vector<ReplayMarker> perfect_clears;
    std::optional<bool> verification;
};

// Strict current-format decoder. It owns no input bytes, so the caller must keep
// the supplied span alive until finished(). step() bounds event parsing work and
// is used by the Web build to keep large/malicious files off the UI hot path.
class ReplayDecoder {
public:
    explicit ReplayDecoder(std::span<const std::uint8_t> bytes);
    explicit ReplayDecoder(std::string_view bytes)
        : ReplayDecoder(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size())) {}

    bool step(std::size_t max_events=4096);
    bool finished() const { return finished_; }
    bool ok() const { return finished_ && error_.empty(); }
    const std::string& error() const { return error_; }
    const Replay& replay() const { return replay_; }
    Replay takeReplay();
    std::size_t decodedEvents() const { return decoded_events_; }
    std::size_t expectedEvents() const { return expected_events_; }

private:
    bool readByte(std::uint8_t& out);
    bool readRaw(void* out,std::size_t size);
    bool readVar(std::uint64_t& out);
    bool readBoundedVar(std::uint64_t max,int& out);
    bool readRules();
    bool parseHeader();
    void fail(std::string_view message);
    bool finishDecode();

    std::span<const std::uint8_t> bytes_;
    std::size_t pos_{};
    Replay replay_{};
    std::size_t expected_events_{};
    std::size_t decoded_events_{};
    std::uint64_t timestamp_{};
    bool header_done_{};
    bool finished_{};
    std::string error_;
};

// Incrementally re-simulates one replay. The same pass verifies the replay and
// builds bounded seek checkpoints plus exact semantic indexes, so work is not
// duplicated and analysis timestamps do not depend on simulation slice size.
class ReplayIndexBuilder {
public:
    explicit ReplayIndexBuilder(const Replay& replay);
    bool step(); // Returns true when useful work was performed.
    bool finished() const { return finished_; }
    TimeUs processedThrough() const { return playhead_; }
    const ReplayIndex& index() const { return index_; }

private:
    void recordGameEvents(std::size_t next_event);
    void advanceAndRecord(TimeUs target);
    void addCheckpoint(TimeUs at);

    const Replay* replay_{};
    Game game_;
    std::size_t event_index_{};
    TimeUs playhead_{};
    TimeUs next_checkpoint_us_{};
    ReplayIndex index_;
    bool finished_{};
};

Sha256Digest stateHash(const Game& game);
void finalizeReplay(Replay& replay, const Game& game);
bool validateReplay(const Replay& replay, std::string* error=nullptr);

// Current .ftr format is compact binary. std::string is used as an owning byte
// buffer and may contain NUL bytes.
std::string serializeReplay(const Replay& replay);
bool deserializeReplay(std::string_view bytes, Replay& out, std::string* error=nullptr);
bool saveReplay(const Replay& replay, const std::string& path, std::string* error=nullptr);
bool loadReplay(const std::string& path, Replay& out, std::string* error=nullptr);
bool verifyReplay(const Replay& replay, Sha256Digest* actual_hash=nullptr);

} // namespace fasttris
