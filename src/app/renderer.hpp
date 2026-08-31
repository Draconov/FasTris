#pragma once
#include "app_config.hpp"
#include "fasttris/game.hpp"
#include "fasttris/replay.hpp"
#include <SDL3/SDL.h>
#include <cstdint>
#include <span>
#include <string>

namespace fasttris::app {
struct RenderInfo {
    std::uint64_t seed{};
    bool paused{};
    bool replay_mode{};
    bool replay_paused{};
    bool show_inputs{true};
    double replay_speed{1.0};
    std::string status;
    std::span<const ReplayEvent> recent_inputs;
};

enum SettingsItem : int {
    SettingDas = 0,
    SettingArr,
    SettingSdf,
    SettingDcd,
    SettingLock,
    SettingResets,
    SettingRotate180,
    SettingIrs,
    SettingIhs,
    SettingGhost,
    SettingNext,
    SettingShowInputs,
    SettingVsync,
    SettingFpsCap,
    SettingTournament,
    SettingSeedMenu,
    SettingControls,
    SettingMiscellaneous,
    SettingReset,
    SettingCount
};

enum SeedSettingsItem : int {
    SeedSettingValue = 0,
    SeedSettingRandomize,
    SeedSettingCopy,
    SeedSettingPaste,
    SeedSettingBack,
    SeedSettingCount
};

inline constexpr int kControlResetIndex = static_cast<int>(Action::Count);
inline constexpr int kControlItemCount = kControlResetIndex + 1;
inline constexpr int kMiscShadersIndex = 0;
inline constexpr int kMiscTexturesIndex = 1;
inline constexpr int kMiscPalettesIndex = 2;
inline constexpr int kMiscPalettePiecesIndex = 3;
inline constexpr int kMiscResetGraphicsIndex = 4;
inline constexpr int kMiscItemCount = 5;

void setVisualPalette(VisualPalette palette, bool affects_pieces);
void setVisualTexture(const AppConfig& cfg);
void setVisualShader(const AppConfig& cfg);
void beginVisualShaderFrame(SDL_Renderer* r);
void applyVisualShader(SDL_Renderer* r);
void shutdownVisualShaderPipeline();

void renderGame(SDL_Renderer* r, Game& g, const RenderInfo& info);
void renderMenu(SDL_Renderer* r, int selected, bool tournament);
void renderReplayMenu(SDL_Renderer* r, int selected, bool has_last_replay, const std::string& status);
void renderSettings(SDL_Renderer* r, const AppConfig& cfg, int selected,
                    bool numeric_editing, const std::string& numeric_text, const std::string& status);
void renderSeedSettings(SDL_Renderer* r, std::uint64_t seed, int selected,
                        bool numeric_editing, const std::string& numeric_text, const std::string& status);
void renderSandboxSetup(SDL_Renderer* r, const AppConfig& cfg, int selected);
void renderControls(SDL_Renderer* r, const AppConfig& cfg, int selected, bool rebinding, bool waiting_pad);
void renderMiscellaneous(SDL_Renderer* r, const AppConfig& cfg, int selected, const std::string& status);
void renderPaletteSettings(SDL_Renderer* r, const AppConfig& cfg, int selected);
void renderTextureSettings(SDL_Renderer* r, const AppConfig& cfg, int selected);
void renderShaderSettings(SDL_Renderer* r, const AppConfig& cfg, int selected, int shader_slot);
void renderHelp(SDL_Renderer* r);

} // namespace fasttris::app
