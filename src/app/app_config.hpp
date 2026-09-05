#pragma once
#include "fasttris/types.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <span>
#include <string>

namespace fasttris::app {

enum class VisualPalette : int {
    Default = 0,
    Hacker,
    Amber,
    BlackWhite,
    MintBlue,
    LofiWarm,
    LofiCool,
    PastelBlue,
    Halloween,
    Sunset,
    Sunrise,
    Count
};

inline constexpr int kVisualPaletteCount = static_cast<int>(VisualPalette::Count);
const char* paletteName(VisualPalette palette);

enum class VisualShader : int {
    None = 0,
    CRT,
    Terminal,
    LCD,
    DotMatrix,
    Bloom,
    Scanlines,
    Vignette,
    Analog,
    Chromatic,
    Ghosting,
    Arcade,
    Count
};

inline constexpr int kVisualShaderCount = static_cast<int>(VisualShader::Count);
const char* shaderName(VisualShader shader);

enum class VisualTexture : int {
    Default = 0,
    Flat,
    Beveled,
    SoftBevel,
    Glass,
    Neon,
    Metallic,
    Pixel,
    Dots,
    Stripes,
    Grid,
    Wireframe,
    Outline,
    Recessed,
    Arcade,
    RetroLCD,
    Count
};

inline constexpr int kVisualTextureCount = static_cast<int>(VisualTexture::Count);
const char* textureName(VisualTexture texture);

enum class TextureControl : int {
    CellGap = 0,
    Depth,
    Highlight,
    Shadow,
    Border,
    Softness,
    Reflection,
    EdgeLight,
    InnerDarken,
    Transparency,
    Contrast,
    PatternScale,
    DotSize,
    DotSpacing,
    Angle,
    Spacing,
    LineThickness,
    GridSize,
    Count
};

enum class ShaderControl : int {
    Strength = 0,
    Scanlines,
    ScanlineSpacing,
    Glow,
    Curvature,
    Vignette,
    EdgeSoftness,
    Softness,
    Persistence,
    Flicker,
    PixelGrid,
    GridSize,
    Subpixel,
    Sharpness,
    DotSize,
    DotSpacing,
    DotBrightness,
    Radius,
    Threshold,
    TrailLength,
    GhostGlow,
    GhostLifetime,
    ColoredGhosts,
    Noise,
    HorizontalJitter,
    Distortion,
    RgbOffset,
    Direction,
    LineThickness,
    BloomAmount,
    Count
};

inline constexpr std::size_t kShaderSlotCount = 8;

struct ShaderSettings {
    int strength{50};
    int scanlines{35};
    int scanline_spacing{4};
    int glow{30};
    int curvature{35};
    int vignette{25};
    int edge_softness{15};
    int softness{15};
    int persistence{25};
    int flicker{10};
    int pixel_grid{20};
    int grid_size{8};
    int subpixel{25};
    int sharpness{60};
    int dot_size{2};
    int dot_spacing{6};
    int dot_brightness{35};
    int radius{50};
    int threshold{65};
    int trail_length{3};
    int ghost_glow{0};
    int ghost_lifetime_ms{1200};
    int colored_ghosts{1};
    int noise{20};
    int horizontal_jitter{10};
    int distortion{15};
    int rgb_offset{2};
    int direction{0};
    int line_thickness{1};
    int bloom{25};
};

struct ShaderSlotConfig {
    VisualShader shader{VisualShader::None};
    ShaderSettings settings{};
};

struct AppConfig {
    Rules rules{};
    bool vsync{false};
    bool show_inputs{true};
    int fps_cap{480}; // 0 = uncapped
    int music_volume{70}; // 0 = muted

    VisualPalette palette{VisualPalette::Default};
    bool palette_affects_pieces{true};
    std::array<ShaderSlotConfig, kShaderSlotCount> shader_slots{};
    VisualTexture texture{VisualTexture::Default};

    // Presentation-only procedural texture parameters. Rendering is generated
    // from cheap SDL primitives: no image assets, decoding, or per-frame heap work.
    int texture_cell_gap{2};
    int texture_depth{35};
    int texture_highlight{35};
    int texture_shadow{30};
    int texture_border{2};
    int texture_softness{35};
    int texture_reflection{40};
    int texture_edge_light{40};
    int texture_inner_darken{20};
    int texture_transparency{10};
    int texture_contrast{30};
    int texture_pattern_scale{4};
    int texture_dot_size{2};
    int texture_dot_spacing{6};
    int texture_angle{1};
    int texture_spacing{5};
    int texture_line_thickness{1};
    int texture_grid_size{6};

    // Each shader slot owns its parameters. Slots are fully independent, so
    // the same shader can even be used twice with different values.

    std::array<SDL_Keycode, static_cast<std::size_t>(Action::Count)> keys{};
    std::array<int, static_cast<std::size_t>(Action::Count)> pads{};
};

std::span<const TextureControl> textureControls(VisualTexture texture);
const char* textureControlName(TextureControl control);
std::string textureControlValueText(const AppConfig& cfg, TextureControl control);
int textureControlNumericValue(const AppConfig& cfg, TextureControl control);
bool setTextureControlNumericValue(AppConfig& cfg, TextureControl control, int value);
std::string textureControlNumericRangeText(TextureControl control);
void adjustTextureControl(AppConfig& cfg, TextureControl control, int direction);

std::span<const ShaderControl> shaderControls(VisualShader shader);
const char* shaderControlName(ShaderControl control);
std::string shaderControlValueText(const AppConfig& cfg, std::size_t slot, ShaderControl control);
bool shaderControlAcceptsNumericInput(ShaderControl control);
int shaderControlNumericValue(const AppConfig& cfg, std::size_t slot, ShaderControl control);
bool setShaderControlNumericValue(AppConfig& cfg, std::size_t slot, ShaderControl control, int value);
std::string shaderControlNumericRangeText(ShaderControl control);
void adjustShaderControl(AppConfig& cfg, std::size_t slot, ShaderControl control, int direction);

AppConfig defaultConfig();
bool loadConfig(const std::string& path, AppConfig& cfg);
bool saveConfig(const std::string& path, const AppConfig& cfg);
void resetSettings(AppConfig& cfg);
void resetGraphics(AppConfig& cfg);
void resetControls(AppConfig& cfg);
const char* padName(int button);
} // namespace fasttris::app
