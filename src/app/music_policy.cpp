#include "music_policy.hpp"
#include "fasttris/game.hpp"
#include <algorithm>
#include <cmath>

namespace fasttris::app {
namespace {
double unit(double value) { return std::clamp(value, 0.0, 1.0); }
}

double musicPressure(const MusicPressureSnapshot& s) {
    double mode_pressure = 0.0;
    switch (s.mode) {
        case Mode::Sprint40:
        case Mode::SeedRace:
            mode_pressure = unit(static_cast<double>(s.lines) / 40.0);
            break;
        case Mode::Ultra120:
            mode_pressure = unit(s.elapsed_seconds / 120.0);
            break;
        case Mode::Marathon:
            mode_pressure = std::max(
                unit(static_cast<double>(s.lines) / 150.0),
                unit(static_cast<double>(std::max(1, s.level) - 1) / 14.0));
            break;
        case Mode::Cheese40:
            mode_pressure = unit(static_cast<double>(s.garbage_lines_cleared) / 40.0);
            break;
        case Mode::Custom: {
            if (s.custom_line_goal > 0)
                mode_pressure = std::max(mode_pressure, unit(static_cast<double>(s.lines) / s.custom_line_goal));
            if (s.custom_time_limit_s > 0)
                mode_pressure = std::max(mode_pressure, unit(s.elapsed_seconds / s.custom_time_limit_s));
            // Default/slow custom gravity contributes no pressure. Around
            // 350 ms/cell reaches the intense threshold and faster speeds ramp
            // smoothly toward 1.0.
            if (s.custom_gravity_ms > 0 && s.custom_gravity_ms < 1000)
                mode_pressure = std::max(mode_pressure, unit((1000.0 - s.custom_gravity_ms) / 900.0));
            break;
        }
        case Mode::Zen:
        case Mode::Finesse:
            break;
    }

    // A stack eight rows tall is still comfortable. Fourteen visible rows
    // reaches the intensity threshold; sixteen or more is maximum danger.
    const double stack_pressure = unit((static_cast<double>(s.visible_stack_height) - 8.0) / 8.0);
    const double garbage_pressure = unit(static_cast<double>(s.pending_garbage_lines) / 8.0);
    return std::max({mode_pressure, stack_pressure, garbage_pressure});
}

MusicPressureSnapshot musicPressureSnapshot(const Game& game) {
    MusicPressureSnapshot s{};
    s.mode = game.mode();
    s.lines = game.stats().lines;
    s.garbage_lines_cleared = game.stats().garbage_lines_cleared;
    s.level = game.level();
    s.elapsed_seconds = static_cast<double>(game.stats().elapsed_us) / 1'000'000.0;
    s.custom_line_goal = game.rules().custom_line_goal;
    s.custom_time_limit_s = game.rules().custom_time_limit_s;
    s.custom_gravity_ms = game.rules().custom_gravity_ms;
    s.pending_garbage_lines = game.pendingGarbageLines();
    s.terminal = game.gameOver() || game.complete();

    const int visible_begin = game.hiddenRows();
    int first_occupied = game.board().height();
    for (int y = visible_begin; y < game.board().height(); ++y) {
        if (game.board().rowMask(y) != 0) { first_occupied = y; break; }
    }
    if (first_occupied < game.board().height())
        s.visible_stack_height = std::clamp(kVisibleH - (first_occupied - visible_begin), 0, kVisibleH);
    return s;
}

CrossfadeGains crossfadeGains(float progress) {
    const float t = std::clamp(progress, 0.0f, 1.0f);
    return {std::sqrt(1.0f - t), std::sqrt(t)};
}

MusicTrack MusicDirector::trackForGame(const MusicPressureSnapshot& snapshot) {
    if (snapshot.terminal) {
        intense_ = false;
        return MusicTrack::Menu;
    }
    const double pressure = musicPressure(snapshot);
    if (intense_) {
        if (pressure < kMusicIntenseOffPressure) intense_ = false;
    } else if (pressure >= kMusicIntenseOnPressure) {
        intense_ = true;
    }
    return intense_ ? MusicTrack::Intense : MusicTrack::Gameplay;
}

} // namespace fasttris::app
