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
    Noise,
    HorizontalJitter,
    Distortion,
    RgbOffset,
    Direction,
    LineThickness,
    BloomAmount,
    Count
};

struct AppConfig {
    Rules rules{};
    bool vsync{false};
    bool show_inputs{true};
    int fps_cap{480}; // 0 = uncapped

    VisualPalette palette{VisualPalette::Default};
    bool palette_affects_pieces{true};
    VisualShader shader{VisualShader::None};
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

    // Presentation-only shader parameters. These never enter Rules, replay
    // serialization, deterministic state hashes, seeds, or verification.
    int shader_strength{50};
    int shader_scanlines{35};
    int shader_scanline_spacing{4};
    int shader_glow{30};
    int crt_curvature{35};
    int shader_vignette{25};
    int shader_softness{15};
    int shader_persistence{25};
    int shader_flicker{10};
    int shader_pixel_grid{20};
    int shader_grid_size{8};
    int shader_subpixel{25};
    int shader_sharpness{60};
    int shader_dot_size{2};
    int shader_dot_spacing{6};
    int shader_dot_brightness{35};
    int shader_radius{50};
    int shader_threshold{65};
    int shader_trail_length{3};
    int shader_noise{20};
    int shader_horizontal_jitter{10};
    int shader_distortion{15};
    int shader_rgb_offset{2};
    int shader_direction{0};
    int shader_line_thickness{1};
    int shader_bloom{25};

    std::array<SDL_Keycode, static_cast<std::size_t>(Action::Count)> keys{};
    std::array<int, static_cast<std::size_t>(Action::Count)> pads{};
};

std::span<const TextureControl> textureControls(VisualTexture texture);
const char* textureControlName(TextureControl control);
std::string textureControlValueText(const AppConfig& cfg, TextureControl control);
void adjustTextureControl(AppConfig& cfg, TextureControl control, int direction);

std::span<const ShaderControl> shaderControls(VisualShader shader);
const char* shaderControlName(ShaderControl control);
std::string shaderControlValueText(const AppConfig& cfg, ShaderControl control);
void adjustShaderControl(AppConfig& cfg, ShaderControl control, int direction);

AppConfig defaultConfig();
bool loadConfig(const std::string& path, AppConfig& cfg);
bool saveConfig(const std::string& path, const AppConfig& cfg);
void resetSettings(AppConfig& cfg);
void resetGraphics(AppConfig& cfg);
void resetControls(AppConfig& cfg);
const char* padName(int button);
} // namespace fasttris::app
