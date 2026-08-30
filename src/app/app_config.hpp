#pragma once
#include "fasttris/types.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <string>

namespace fasttris::app {
struct AppConfig {
    Rules rules{};
    bool vsync{false};
    bool show_inputs{true};
    int fps_cap{480}; // 0 = uncapped
    std::array<SDL_Keycode, static_cast<std::size_t>(Action::Count)> keys{};
    std::array<int, static_cast<std::size_t>(Action::Count)> pads{};
};
AppConfig defaultConfig();
bool loadConfig(const std::string& path, AppConfig& cfg);
bool saveConfig(const std::string& path, const AppConfig& cfg);
void resetSettings(AppConfig& cfg);
void resetControls(AppConfig& cfg);
const char* padName(int button);
} // namespace fasttris::app
