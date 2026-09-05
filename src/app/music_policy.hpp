#pragma once
#include "fasttris/types.hpp"

namespace fasttris { class Game; }

namespace fasttris::app {

enum class MusicTrack : int { Menu = 0, Gameplay, Intense, Count };

struct MusicPressureSnapshot {
    Mode mode{Mode::Sprint40};
    int lines{};
    int garbage_lines_cleared{};
    int level{1};
    double elapsed_seconds{};
    int custom_line_goal{};
    int custom_time_limit_s{};
    int custom_gravity_ms{1000};
    int visible_stack_height{};
    int pending_garbage_lines{};
    bool terminal{};
};

inline constexpr double kMusicIntenseOnPressure = 0.72;
inline constexpr double kMusicIntenseOffPressure = 0.55;
inline constexpr float kMusicCrossfadeSeconds = 1.75f;

double musicPressure(const MusicPressureSnapshot& snapshot);
MusicPressureSnapshot musicPressureSnapshot(const Game& game);

struct CrossfadeGains { float from{1.0f}; float to{}; };
CrossfadeGains crossfadeGains(float progress);

class MusicDirector {
public:
    MusicTrack trackForGame(const MusicPressureSnapshot& snapshot);
    MusicTrack trackForGame(const Game& game) { return trackForGame(musicPressureSnapshot(game)); }
    void reset() { intense_ = false; }
    bool intense() const { return intense_; }
private:
    bool intense_{};
};

} // namespace fasttris::app
