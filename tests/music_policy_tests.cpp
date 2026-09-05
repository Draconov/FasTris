#include "music_policy.hpp"
#include "fasttris/game.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

using namespace fasttris;
using namespace fasttris::app;

namespace {
MusicPressureSnapshot snapshot(Mode mode) {
    MusicPressureSnapshot s{};
    s.mode = mode;
    return s;
}
}

int main() {
    {
        auto s = snapshot(Mode::Marathon);
        s.lines = 110;
        s.level = 12;
        assert(musicPressure(s) >= 0.72);
    }
    {
        auto s = snapshot(Mode::Sprint40);
        s.lines = 30;
        assert(musicPressure(s) >= 0.72);
    }
    {
        auto s = snapshot(Mode::Ultra120);
        s.elapsed_seconds = 90.0;
        assert(musicPressure(s) >= 0.72);
    }
    {
        auto s = snapshot(Mode::Zen);
        s.visible_stack_height = 14;
        assert(musicPressure(s) >= 0.72);
    }
    {
        auto s = snapshot(Mode::Finesse);
        s.pending_garbage_lines = 6;
        assert(musicPressure(s) >= 0.72);
    }
    {
        MusicDirector director;
        auto s = snapshot(Mode::Sprint40);
        s.lines = 30;
        assert(director.trackForGame(s) == MusicTrack::Intense);
        s.lines = 24; // pressure 0.60: stay intense because of hysteresis.
        assert(director.trackForGame(s) == MusicTrack::Intense);
        s.lines = 21; // pressure 0.525: drop back below the 0.55 release threshold.
        assert(director.trackForGame(s) == MusicTrack::Gameplay);
        s.terminal = true;
        assert(director.trackForGame(s) == MusicTrack::Menu);
    }
    {
        const auto a = crossfadeGains(0.0f);
        const auto m = crossfadeGains(0.5f);
        const auto b = crossfadeGains(1.0f);
        assert(std::abs(a.from - 1.0f) < 0.0001f && std::abs(a.to) < 0.0001f);
        assert(std::abs(m.from - 0.7071067f) < 0.001f && std::abs(m.to - 0.7071067f) < 0.001f);
        assert(std::abs(b.from) < 0.0001f && std::abs(b.to - 1.0f) < 0.0001f);
    }
    {
        Game game(1, Mode::Sprint40, Rules{});
        assert(game.pendingGarbageLines() == 0);
        game.enqueueGarbage(5, 10'000'000);
        assert(game.pendingGarbageLines() == 5);
    }
    std::cout << "music policy tests passed\n";
}
