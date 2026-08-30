#pragma once
#include "app_config.hpp"
#include "fasttris/game.hpp"
#include "fasttris/replay.hpp"
#include <SDL3/SDL.h>
#include <cstdint>
#include <string>
#include <vector>

namespace fasttris::app {
struct RenderInfo {
    std::uint64_t seed{};
    bool paused{};
    bool replay_mode{};
    double replay_speed{1.0};
    bool replay_paused{};
    std::string status;
    std::vector<ReplayEvent> recent_inputs;
};

void renderGame(SDL_Renderer* r, Game& g, const RenderInfo& info);
void renderMenu(SDL_Renderer* r, int selected, std::uint64_t seed, bool tournament, bool has_replay);
void renderSettings(SDL_Renderer* r, const AppConfig& cfg, int selected);
void renderControls(SDL_Renderer* r, const AppConfig& cfg, int selected, bool rebinding, bool waiting_pad);
void renderSeedEntry(SDL_Renderer* r, const std::string& text, const std::string& error);
void renderHelp(SDL_Renderer* r);

} // namespace fasttris::app
