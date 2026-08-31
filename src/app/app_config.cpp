#include "app_config.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>

namespace fasttris::app {

const char* paletteName(VisualPalette palette){
    switch(palette){
        case VisualPalette::Default:return "DEFAULT";
        case VisualPalette::Hacker:return "HACKER";
        case VisualPalette::Amber:return "AMBER";
        case VisualPalette::BlackWhite:return "BLACK & WHITE";
        case VisualPalette::MintBlue:return "MINT BLUE";
        case VisualPalette::LofiWarm:return "LO-FI WARM";
        case VisualPalette::LofiCool:return "LO-FI COOL";
        case VisualPalette::PastelBlue:return "PASTEL BLUE";
        case VisualPalette::Halloween:return "HALLOWEEN";
        case VisualPalette::Sunset:return "SUNSET";
        case VisualPalette::Sunrise:return "SUNRISE";
        default:return "DEFAULT";
    }
}

const char* textureName(VisualTexture texture){
    switch(texture){
        case VisualTexture::Default:return "DEFAULT";
        case VisualTexture::Flat:return "FLAT";
        case VisualTexture::Beveled:return "BEVELED";
        case VisualTexture::SoftBevel:return "SOFT BEVEL";
        case VisualTexture::Glass:return "GLASS";
        case VisualTexture::Neon:return "NEON";
        case VisualTexture::Metallic:return "METALLIC";
        case VisualTexture::Pixel:return "PIXEL";
        case VisualTexture::Dots:return "DOTS";
        case VisualTexture::Stripes:return "STRIPES";
        case VisualTexture::Grid:return "GRID";
        case VisualTexture::Wireframe:return "WIREFRAME";
        case VisualTexture::Outline:return "OUTLINE";
        case VisualTexture::Recessed:return "RECESSED";
        case VisualTexture::Arcade:return "ARCADE";
        case VisualTexture::RetroLCD:return "RETRO LCD";
        default:return "DEFAULT";
    }
}

const char* shaderName(VisualShader shader){
    switch(shader){
        case VisualShader::None:return "NONE";
        case VisualShader::CRT:return "CRT";
        case VisualShader::Terminal:return "TERMINAL";
        case VisualShader::LCD:return "LCD";
        case VisualShader::DotMatrix:return "DOT MATRIX";
        case VisualShader::Bloom:return "BLOOM";
        case VisualShader::Scanlines:return "SCANLINES";
        case VisualShader::Vignette:return "VIGNETTE";
        case VisualShader::Analog:return "ANALOG";
        case VisualShader::Chromatic:return "CHROMATIC";
        case VisualShader::Ghosting:return "GHOSTING";
        case VisualShader::Arcade:return "ARCADE";
        default:return "NONE";
    }
}

namespace {
constexpr std::size_t ix(Action a){return static_cast<std::size_t>(a);}

constexpr std::array<TextureControl,1> kTextureDefaultControls={TextureControl::CellGap};
constexpr std::array<TextureControl,1> kTextureFlatControls={TextureControl::CellGap};
constexpr std::array<TextureControl,6> kTextureBeveledControls={
    TextureControl::Depth,TextureControl::Highlight,TextureControl::Shadow,
    TextureControl::Border,TextureControl::Softness,TextureControl::CellGap};
constexpr std::array<TextureControl,5> kTextureSoftBevelControls={
    TextureControl::Depth,TextureControl::Highlight,TextureControl::Shadow,TextureControl::Softness,TextureControl::CellGap};
constexpr std::array<TextureControl,5> kTextureGlassControls={
    TextureControl::Reflection,TextureControl::EdgeLight,TextureControl::InnerDarken,TextureControl::Transparency,TextureControl::CellGap};
constexpr std::array<TextureControl,4> kTextureNeonControls={
    TextureControl::EdgeLight,TextureControl::InnerDarken,TextureControl::Border,TextureControl::CellGap};
constexpr std::array<TextureControl,5> kTextureMetallicControls={
    TextureControl::Highlight,TextureControl::Contrast,TextureControl::Spacing,TextureControl::LineThickness,TextureControl::CellGap};
constexpr std::array<TextureControl,3> kTexturePixelControls={
    TextureControl::PatternScale,TextureControl::Contrast,TextureControl::CellGap};
constexpr std::array<TextureControl,4> kTextureDotsControls={
    TextureControl::DotSize,TextureControl::DotSpacing,TextureControl::Contrast,TextureControl::CellGap};
constexpr std::array<TextureControl,5> kTextureStripesControls={
    TextureControl::Angle,TextureControl::Spacing,TextureControl::LineThickness,TextureControl::Contrast,TextureControl::CellGap};
constexpr std::array<TextureControl,4> kTextureGridControls={
    TextureControl::GridSize,TextureControl::LineThickness,TextureControl::Contrast,TextureControl::CellGap};
constexpr std::array<TextureControl,5> kTextureWireframeControls={
    TextureControl::Border,TextureControl::EdgeLight,TextureControl::InnerDarken,
    TextureControl::Transparency,TextureControl::CellGap};
constexpr std::array<TextureControl,3> kTextureOutlineControls={
    TextureControl::Border,TextureControl::InnerDarken,TextureControl::CellGap};
constexpr std::array<TextureControl,4> kTextureRecessedControls={
    TextureControl::Depth,TextureControl::Highlight,TextureControl::Shadow,TextureControl::CellGap};
constexpr std::array<TextureControl,5> kTextureArcadeControls={
    TextureControl::Depth,TextureControl::Highlight,TextureControl::EdgeLight,TextureControl::Border,TextureControl::CellGap};
constexpr std::array<TextureControl,5> kTextureRetroLcdControls={
    TextureControl::GridSize,TextureControl::DotSpacing,TextureControl::Contrast,TextureControl::InnerDarken,TextureControl::CellGap};

int& textureControlRef(AppConfig& c,TextureControl control){
    switch(control){
        case TextureControl::CellGap:return c.texture_cell_gap;
        case TextureControl::Depth:return c.texture_depth;
        case TextureControl::Highlight:return c.texture_highlight;
        case TextureControl::Shadow:return c.texture_shadow;
        case TextureControl::Border:return c.texture_border;
        case TextureControl::Softness:return c.texture_softness;
        case TextureControl::Reflection:return c.texture_reflection;
        case TextureControl::EdgeLight:return c.texture_edge_light;
        case TextureControl::InnerDarken:return c.texture_inner_darken;
        case TextureControl::Transparency:return c.texture_transparency;
        case TextureControl::Contrast:return c.texture_contrast;
        case TextureControl::PatternScale:return c.texture_pattern_scale;
        case TextureControl::DotSize:return c.texture_dot_size;
        case TextureControl::DotSpacing:return c.texture_dot_spacing;
        case TextureControl::Angle:return c.texture_angle;
        case TextureControl::Spacing:return c.texture_spacing;
        case TextureControl::LineThickness:return c.texture_line_thickness;
        case TextureControl::GridSize:return c.texture_grid_size;
        default:return c.texture_cell_gap;
    }
}

int textureControlValue(const AppConfig& c,TextureControl control){
    switch(control){
        case TextureControl::CellGap:return c.texture_cell_gap;
        case TextureControl::Depth:return c.texture_depth;
        case TextureControl::Highlight:return c.texture_highlight;
        case TextureControl::Shadow:return c.texture_shadow;
        case TextureControl::Border:return c.texture_border;
        case TextureControl::Softness:return c.texture_softness;
        case TextureControl::Reflection:return c.texture_reflection;
        case TextureControl::EdgeLight:return c.texture_edge_light;
        case TextureControl::InnerDarken:return c.texture_inner_darken;
        case TextureControl::Transparency:return c.texture_transparency;
        case TextureControl::Contrast:return c.texture_contrast;
        case TextureControl::PatternScale:return c.texture_pattern_scale;
        case TextureControl::DotSize:return c.texture_dot_size;
        case TextureControl::DotSpacing:return c.texture_dot_spacing;
        case TextureControl::Angle:return c.texture_angle;
        case TextureControl::Spacing:return c.texture_spacing;
        case TextureControl::LineThickness:return c.texture_line_thickness;
        case TextureControl::GridSize:return c.texture_grid_size;
        default:return 0;
    }
}

std::pair<int,int> textureControlBounds(TextureControl control){
    switch(control){
        case TextureControl::CellGap:return {0,6};
        case TextureControl::Border:return {1,6};
        case TextureControl::Transparency:return {0,70};
        case TextureControl::PatternScale:return {2,10};
        case TextureControl::DotSize:return {1,5};
        case TextureControl::DotSpacing:return {4,12};
        case TextureControl::Angle:return {0,3};
        case TextureControl::Spacing:return {3,12};
        case TextureControl::LineThickness:return {1,4};
        case TextureControl::GridSize:return {3,12};
        default:return {0,100};
    }
}

int textureControlStep(TextureControl control){
    switch(control){
        case TextureControl::CellGap:
        case TextureControl::Border:
        case TextureControl::PatternScale:
        case TextureControl::DotSize:
        case TextureControl::DotSpacing:
        case TextureControl::Angle:
        case TextureControl::Spacing:
        case TextureControl::LineThickness:
        case TextureControl::GridSize:
            return 1;
        default:return 5;
    }
}

void clampTextureControls(AppConfig& c){
    c.texture_cell_gap=std::clamp(c.texture_cell_gap,0,6);
    c.texture_depth=std::clamp(c.texture_depth,0,100);
    c.texture_highlight=std::clamp(c.texture_highlight,0,100);
    c.texture_shadow=std::clamp(c.texture_shadow,0,100);
    c.texture_border=std::clamp(c.texture_border,1,6);
    c.texture_softness=std::clamp(c.texture_softness,0,100);
    c.texture_reflection=std::clamp(c.texture_reflection,0,100);
    c.texture_edge_light=std::clamp(c.texture_edge_light,0,100);
    c.texture_inner_darken=std::clamp(c.texture_inner_darken,0,100);
    c.texture_transparency=std::clamp(c.texture_transparency,0,70);
    c.texture_contrast=std::clamp(c.texture_contrast,0,100);
    c.texture_pattern_scale=std::clamp(c.texture_pattern_scale,2,10);
    c.texture_dot_size=std::clamp(c.texture_dot_size,1,5);
    c.texture_dot_spacing=std::clamp(c.texture_dot_spacing,4,12);
    c.texture_angle=std::clamp(c.texture_angle,0,3);
    c.texture_spacing=std::clamp(c.texture_spacing,3,12);
    c.texture_line_thickness=std::clamp(c.texture_line_thickness,1,4);
    c.texture_grid_size=std::clamp(c.texture_grid_size,3,12);
}

constexpr std::array<ShaderControl,7> kCrtControls={
    ShaderControl::Strength,ShaderControl::Scanlines,ShaderControl::ScanlineSpacing,
    ShaderControl::Glow,ShaderControl::Curvature,ShaderControl::Vignette,ShaderControl::Softness};
constexpr std::array<ShaderControl,6> kTerminalControls={
    ShaderControl::Strength,ShaderControl::Glow,ShaderControl::Persistence,
    ShaderControl::TrailLength,ShaderControl::Scanlines,ShaderControl::Flicker};
constexpr std::array<ShaderControl,6> kLcdControls={
    ShaderControl::Strength,ShaderControl::PixelGrid,ShaderControl::GridSize,
    ShaderControl::LineThickness,ShaderControl::Subpixel,ShaderControl::Softness};
constexpr std::array<ShaderControl,4> kDotMatrixControls={
    ShaderControl::Strength,ShaderControl::DotSize,ShaderControl::DotSpacing,ShaderControl::DotBrightness};
constexpr std::array<ShaderControl,4> kBloomControls={
    ShaderControl::Strength,ShaderControl::Radius,ShaderControl::Threshold,ShaderControl::Softness};
constexpr std::array<ShaderControl,3> kScanlineControls={
    ShaderControl::Strength,ShaderControl::ScanlineSpacing,ShaderControl::LineThickness};
constexpr std::array<ShaderControl,3> kVignetteControls={
    ShaderControl::Strength,ShaderControl::Radius,ShaderControl::Softness};
constexpr std::array<ShaderControl,5> kAnalogControls={
    ShaderControl::Strength,ShaderControl::Noise,ShaderControl::Flicker,
    ShaderControl::HorizontalJitter,ShaderControl::Distortion};
constexpr std::array<ShaderControl,3> kChromaticControls={
    ShaderControl::Strength,ShaderControl::RgbOffset,ShaderControl::Direction};
constexpr std::array<ShaderControl,3> kGhostingControls={
    ShaderControl::Strength,ShaderControl::Persistence,ShaderControl::TrailLength};
constexpr std::array<ShaderControl,5> kArcadeControls={
    ShaderControl::Strength,ShaderControl::BloomAmount,ShaderControl::Scanlines,
    ShaderControl::Vignette,ShaderControl::PixelGrid};
constexpr std::array<ShaderControl,0> kNoControls={};

int& controlRef(AppConfig& c,ShaderControl control){
    switch(control){
        case ShaderControl::Strength:return c.shader_strength;
        case ShaderControl::Scanlines:return c.shader_scanlines;
        case ShaderControl::ScanlineSpacing:return c.shader_scanline_spacing;
        case ShaderControl::Glow:return c.shader_glow;
        case ShaderControl::Curvature:return c.crt_curvature;
        case ShaderControl::Vignette:return c.shader_vignette;
        case ShaderControl::Softness:return c.shader_softness;
        case ShaderControl::Persistence:return c.shader_persistence;
        case ShaderControl::Flicker:return c.shader_flicker;
        case ShaderControl::PixelGrid:return c.shader_pixel_grid;
        case ShaderControl::GridSize:return c.shader_grid_size;
        case ShaderControl::Subpixel:return c.shader_subpixel;
        case ShaderControl::Sharpness:return c.shader_sharpness;
        case ShaderControl::DotSize:return c.shader_dot_size;
        case ShaderControl::DotSpacing:return c.shader_dot_spacing;
        case ShaderControl::DotBrightness:return c.shader_dot_brightness;
        case ShaderControl::Radius:return c.shader_radius;
        case ShaderControl::Threshold:return c.shader_threshold;
        case ShaderControl::TrailLength:return c.shader_trail_length;
        case ShaderControl::Noise:return c.shader_noise;
        case ShaderControl::HorizontalJitter:return c.shader_horizontal_jitter;
        case ShaderControl::Distortion:return c.shader_distortion;
        case ShaderControl::RgbOffset:return c.shader_rgb_offset;
        case ShaderControl::Direction:return c.shader_direction;
        case ShaderControl::LineThickness:return c.shader_line_thickness;
        case ShaderControl::BloomAmount:return c.shader_bloom;
        default:return c.shader_strength;
    }
}

int controlValue(const AppConfig& c,ShaderControl control){
    switch(control){
        case ShaderControl::Strength:return c.shader_strength;
        case ShaderControl::Scanlines:return c.shader_scanlines;
        case ShaderControl::ScanlineSpacing:return c.shader_scanline_spacing;
        case ShaderControl::Glow:return c.shader_glow;
        case ShaderControl::Curvature:return c.crt_curvature;
        case ShaderControl::Vignette:return c.shader_vignette;
        case ShaderControl::Softness:return c.shader_softness;
        case ShaderControl::Persistence:return c.shader_persistence;
        case ShaderControl::Flicker:return c.shader_flicker;
        case ShaderControl::PixelGrid:return c.shader_pixel_grid;
        case ShaderControl::GridSize:return c.shader_grid_size;
        case ShaderControl::Subpixel:return c.shader_subpixel;
        case ShaderControl::Sharpness:return c.shader_sharpness;
        case ShaderControl::DotSize:return c.shader_dot_size;
        case ShaderControl::DotSpacing:return c.shader_dot_spacing;
        case ShaderControl::DotBrightness:return c.shader_dot_brightness;
        case ShaderControl::Radius:return c.shader_radius;
        case ShaderControl::Threshold:return c.shader_threshold;
        case ShaderControl::TrailLength:return c.shader_trail_length;
        case ShaderControl::Noise:return c.shader_noise;
        case ShaderControl::HorizontalJitter:return c.shader_horizontal_jitter;
        case ShaderControl::Distortion:return c.shader_distortion;
        case ShaderControl::RgbOffset:return c.shader_rgb_offset;
        case ShaderControl::Direction:return c.shader_direction;
        case ShaderControl::LineThickness:return c.shader_line_thickness;
        case ShaderControl::BloomAmount:return c.shader_bloom;
        default:return 0;
    }
}

void clampShaderControls(AppConfig& c){
    c.shader_strength=std::clamp(c.shader_strength,0,100);
    c.shader_scanlines=std::clamp(c.shader_scanlines,0,100);
    c.shader_scanline_spacing=std::clamp(c.shader_scanline_spacing,2,16);
    c.shader_glow=std::clamp(c.shader_glow,0,100);
    c.crt_curvature=std::clamp(c.crt_curvature,0,100);
    c.shader_vignette=std::clamp(c.shader_vignette,0,100);
    c.shader_softness=std::clamp(c.shader_softness,0,100);
    c.shader_persistence=std::clamp(c.shader_persistence,0,100);
    c.shader_flicker=std::clamp(c.shader_flicker,0,100);
    c.shader_pixel_grid=std::clamp(c.shader_pixel_grid,0,100);
    c.shader_grid_size=std::clamp(c.shader_grid_size,4,24);
    c.shader_subpixel=std::clamp(c.shader_subpixel,0,100);
    c.shader_sharpness=std::clamp(c.shader_sharpness,0,100);
    c.shader_dot_size=std::clamp(c.shader_dot_size,1,6);
    c.shader_dot_spacing=std::clamp(c.shader_dot_spacing,4,20);
    c.shader_dot_brightness=std::clamp(c.shader_dot_brightness,0,100);
    c.shader_radius=std::clamp(c.shader_radius,0,100);
    c.shader_threshold=std::clamp(c.shader_threshold,0,100);
    c.shader_trail_length=std::clamp(c.shader_trail_length,1,8);
    c.shader_noise=std::clamp(c.shader_noise,0,100);
    c.shader_horizontal_jitter=std::clamp(c.shader_horizontal_jitter,0,100);
    c.shader_distortion=std::clamp(c.shader_distortion,0,100);
    c.shader_rgb_offset=std::clamp(c.shader_rgb_offset,0,12);
    c.shader_direction=std::clamp(c.shader_direction,0,3);
    c.shader_line_thickness=std::clamp(c.shader_line_thickness,1,4);
    c.shader_bloom=std::clamp(c.shader_bloom,0,100);
}

void clampConfig(AppConfig&c){
    auto&h=c.rules.handling;
    h.das_ms=std::clamp(h.das_ms,0,1000);
    h.arr_ms=std::clamp(h.arr_ms,0,500);
    h.sdf=std::clamp(h.sdf,0,200);
    h.dcd_ms=std::clamp(h.dcd_ms,0,1000);
    h.lock_delay_ms=std::clamp(h.lock_delay_ms,0,2000);
    h.max_lock_resets=std::clamp(h.max_lock_resets,0,100);
    c.rules.next_count=std::clamp(c.rules.next_count,1,8);
    c.rules.custom_gravity_ms=std::clamp(c.rules.custom_gravity_ms,0,5000);
    c.rules.custom_line_goal=std::clamp(c.rules.custom_line_goal,0,1000);
    c.rules.custom_time_limit_s=std::clamp(c.rules.custom_time_limit_s,0,3600);
    c.rules.custom_start_garbage=std::clamp(c.rules.custom_start_garbage,0,12);
    c.fps_cap=std::clamp(c.fps_cap,0,1000);
    c.palette=static_cast<VisualPalette>(std::clamp(static_cast<int>(c.palette),0,kVisualPaletteCount-1));
    c.shader=static_cast<VisualShader>(std::clamp(static_cast<int>(c.shader),0,kVisualShaderCount-1));
    c.texture=static_cast<VisualTexture>(std::clamp(static_cast<int>(c.texture),0,kVisualTextureCount-1));
    clampTextureControls(c);
    clampShaderControls(c);
}

std::pair<int,int> controlBounds(ShaderControl control){
    switch(control){
        case ShaderControl::ScanlineSpacing:return {2,16};
        case ShaderControl::GridSize:return {4,24};
        case ShaderControl::DotSize:return {1,6};
        case ShaderControl::DotSpacing:return {4,20};
        case ShaderControl::TrailLength:return {1,8};
        case ShaderControl::RgbOffset:return {0,12};
        case ShaderControl::Direction:return {0,3};
        case ShaderControl::LineThickness:return {1,4};
        default:return {0,100};
    }
}

int controlStep(ShaderControl control){
    switch(control){
        case ShaderControl::ScanlineSpacing:
        case ShaderControl::GridSize:
        case ShaderControl::DotSize:
        case ShaderControl::DotSpacing:
        case ShaderControl::TrailLength:
        case ShaderControl::RgbOffset:
        case ShaderControl::Direction:
        case ShaderControl::LineThickness:
            return 1;
        default:
            return 5;
    }
}
}

std::span<const TextureControl> textureControls(VisualTexture texture){
    switch(texture){
        case VisualTexture::Default:return kTextureDefaultControls;
        case VisualTexture::Flat:return kTextureFlatControls;
        case VisualTexture::Beveled:return kTextureBeveledControls;
        case VisualTexture::SoftBevel:return kTextureSoftBevelControls;
        case VisualTexture::Glass:return kTextureGlassControls;
        case VisualTexture::Neon:return kTextureNeonControls;
        case VisualTexture::Metallic:return kTextureMetallicControls;
        case VisualTexture::Pixel:return kTexturePixelControls;
        case VisualTexture::Dots:return kTextureDotsControls;
        case VisualTexture::Stripes:return kTextureStripesControls;
        case VisualTexture::Grid:return kTextureGridControls;
        case VisualTexture::Wireframe:return kTextureWireframeControls;
        case VisualTexture::Outline:return kTextureOutlineControls;
        case VisualTexture::Recessed:return kTextureRecessedControls;
        case VisualTexture::Arcade:return kTextureArcadeControls;
        case VisualTexture::RetroLCD:return kTextureRetroLcdControls;
        default:return kTextureDefaultControls;
    }
}

const char* textureControlName(TextureControl control){
    switch(control){
        case TextureControl::CellGap:return "CELL GAP";
        case TextureControl::Depth:return "DEPTH";
        case TextureControl::Highlight:return "HIGHLIGHT";
        case TextureControl::Shadow:return "SHADOW";
        case TextureControl::Border:return "BORDER";
        case TextureControl::Softness:return "SOFTNESS";
        case TextureControl::Reflection:return "REFLECTION";
        case TextureControl::EdgeLight:return "EDGE LIGHT";
        case TextureControl::InnerDarken:return "INNER DARKEN";
        case TextureControl::Transparency:return "TRANSPARENCY";
        case TextureControl::Contrast:return "CONTRAST";
        case TextureControl::PatternScale:return "PATTERN SCALE";
        case TextureControl::DotSize:return "DOT SIZE";
        case TextureControl::DotSpacing:return "DOT SPACING";
        case TextureControl::Angle:return "ANGLE";
        case TextureControl::Spacing:return "SPACING";
        case TextureControl::LineThickness:return "LINE THICKNESS";
        case TextureControl::GridSize:return "GRID SIZE";
        default:return "SETTING";
    }
}

std::string textureControlValueText(const AppConfig& cfg,TextureControl control){
    const int value=textureControlValue(cfg,control);
    switch(control){
        case TextureControl::CellGap:
        case TextureControl::Border:
        case TextureControl::PatternScale:
        case TextureControl::DotSize:
        case TextureControl::DotSpacing:
        case TextureControl::Spacing:
        case TextureControl::LineThickness:
        case TextureControl::GridSize:
            return std::to_string(value)+" PX";
        case TextureControl::Angle:
            switch(value){
                case 0:return "0 DEG";
                case 1:return "45 DEG";
                case 2:return "90 DEG";
                case 3:return "135 DEG";
                default:return "45 DEG";
            }
        default:return std::to_string(value)+"%";
    }
}

void adjustTextureControl(AppConfig& cfg,TextureControl control,int direction){
    if(direction==0)return;
    auto [lo,hi]=textureControlBounds(control);
    int& value=textureControlRef(cfg,control);
    if(control==TextureControl::Angle){
        const int span=hi-lo+1;
        value=lo+(value-lo+(direction>0?1:-1)+span)%span;
        return;
    }
    value=std::clamp(value+(direction>0?1:-1)*textureControlStep(control),lo,hi);
}

std::span<const ShaderControl> shaderControls(VisualShader shader){
    switch(shader){
        case VisualShader::CRT:return kCrtControls;
        case VisualShader::Terminal:return kTerminalControls;
        case VisualShader::LCD:return kLcdControls;
        case VisualShader::DotMatrix:return kDotMatrixControls;
        case VisualShader::Bloom:return kBloomControls;
        case VisualShader::Scanlines:return kScanlineControls;
        case VisualShader::Vignette:return kVignetteControls;
        case VisualShader::Analog:return kAnalogControls;
        case VisualShader::Chromatic:return kChromaticControls;
        case VisualShader::Ghosting:return kGhostingControls;
        case VisualShader::Arcade:return kArcadeControls;
        case VisualShader::None:
        default:return kNoControls;
    }
}

const char* shaderControlName(ShaderControl control){
    switch(control){
        case ShaderControl::Strength:return "STRENGTH";
        case ShaderControl::Scanlines:return "SCANLINES";
        case ShaderControl::ScanlineSpacing:return "SCANLINE SPACING";
        case ShaderControl::Glow:return "GLOW";
        case ShaderControl::Curvature:return "CURVATURE";
        case ShaderControl::Vignette:return "VIGNETTE";
        case ShaderControl::Softness:return "SOFTNESS";
        case ShaderControl::Persistence:return "PERSISTENCE";
        case ShaderControl::Flicker:return "FLICKER";
        case ShaderControl::PixelGrid:return "PIXEL GRID";
        case ShaderControl::GridSize:return "GRID SIZE";
        case ShaderControl::Subpixel:return "SUBPIXEL";
        case ShaderControl::Sharpness:return "SHARPNESS";
        case ShaderControl::DotSize:return "DOT SIZE";
        case ShaderControl::DotSpacing:return "DOT SPACING";
        case ShaderControl::DotBrightness:return "DOT BRIGHTNESS";
        case ShaderControl::Radius:return "RADIUS";
        case ShaderControl::Threshold:return "THRESHOLD";
        case ShaderControl::TrailLength:return "TRAIL LENGTH";
        case ShaderControl::Noise:return "NOISE";
        case ShaderControl::HorizontalJitter:return "HORIZONTAL JITTER";
        case ShaderControl::Distortion:return "DISTORTION";
        case ShaderControl::RgbOffset:return "RGB OFFSET";
        case ShaderControl::Direction:return "DIRECTION";
        case ShaderControl::LineThickness:return "LINE THICKNESS";
        case ShaderControl::BloomAmount:return "BLOOM";
        default:return "SETTING";
    }
}

std::string shaderControlValueText(const AppConfig& cfg,ShaderControl control){
    const int value=controlValue(cfg,control);
    switch(control){
        case ShaderControl::ScanlineSpacing:
        case ShaderControl::GridSize:
        case ShaderControl::DotSize:
        case ShaderControl::DotSpacing:
        case ShaderControl::RgbOffset:
        case ShaderControl::LineThickness:
            return std::to_string(value)+" PX";
        case ShaderControl::TrailLength:
            return std::to_string(value);
        case ShaderControl::Direction:
            switch(value){
                case 0:return "HORIZONTAL";
                case 1:return "VERTICAL";
                case 2:return "DIAGONAL A";
                case 3:return "DIAGONAL B";
                default:return "HORIZONTAL";
            }
        default:
            return std::to_string(value)+"%";
    }
}

void adjustShaderControl(AppConfig& cfg,ShaderControl control,int direction){
    if(direction==0)return;
    auto [lo,hi]=controlBounds(control);
    int& value=controlRef(cfg,control);
    if(control==ShaderControl::Direction){
        const int span=hi-lo+1;
        value=lo+(value-lo+(direction>0?1:-1)+span)%span;
        return;
    }
    value=std::clamp(value+(direction>0?1:-1)*controlStep(control),lo,hi);
}

AppConfig defaultConfig(){
    AppConfig c;c.keys.fill(SDLK_UNKNOWN);c.pads.fill(-1);
    c.keys[ix(Action::Left)]=SDLK_LEFT;c.keys[ix(Action::Right)]=SDLK_RIGHT;c.keys[ix(Action::SoftDrop)]=SDLK_DOWN;c.keys[ix(Action::HardDrop)]=SDLK_SPACE;c.keys[ix(Action::RotateCW)]=SDLK_UP;c.keys[ix(Action::RotateCCW)]=SDLK_Z;c.keys[ix(Action::Rotate180)]=SDLK_A;c.keys[ix(Action::Hold)]=SDLK_C;c.keys[ix(Action::Restart)]=SDLK_F5;c.keys[ix(Action::Pause)]=SDLK_P;
    c.pads[ix(Action::Left)]=SDL_GAMEPAD_BUTTON_DPAD_LEFT;c.pads[ix(Action::Right)]=SDL_GAMEPAD_BUTTON_DPAD_RIGHT;c.pads[ix(Action::SoftDrop)]=SDL_GAMEPAD_BUTTON_DPAD_DOWN;c.pads[ix(Action::HardDrop)]=SDL_GAMEPAD_BUTTON_DPAD_UP;c.pads[ix(Action::RotateCW)]=SDL_GAMEPAD_BUTTON_SOUTH;c.pads[ix(Action::RotateCCW)]=SDL_GAMEPAD_BUTTON_WEST;c.pads[ix(Action::Rotate180)]=SDL_GAMEPAD_BUTTON_NORTH;c.pads[ix(Action::Hold)]=SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;c.pads[ix(Action::Restart)]=SDL_GAMEPAD_BUTTON_START;c.pads[ix(Action::Pause)]=SDL_GAMEPAD_BUTTON_BACK;
    return c;
}

bool loadConfig(const std::string&path,AppConfig&c){
    std::ifstream f(path);if(!f)return false;
    std::string line;
    while(std::getline(f,line)){
        auto p=line.find('=');if(p==std::string::npos)continue;auto k=line.substr(0,p),v=line.substr(p+1);
        try{
            int n=std::stoi(v);auto&h=c.rules.handling;
            if(k=="das")h.das_ms=n;
            else if(k=="arr")h.arr_ms=n;
            else if(k=="sdf")h.sdf=n;
            else if(k=="dcd")h.dcd_ms=n;
            else if(k=="lock")h.lock_delay_ms=n;
            else if(k=="resets")h.max_lock_resets=n;
            else if(k=="allow180")h.allow_180=n!=0;
            else if(k=="irs")h.irs=n!=0;
            else if(k=="ihs")h.ihs=n!=0;
            else if(k=="ghost")c.rules.ghost=n!=0;
            else if(k=="next")c.rules.next_count=n;
            else if(k=="customgrav")c.rules.custom_gravity_ms=n;
            else if(k=="customlines")c.rules.custom_line_goal=n;
            else if(k=="customtime")c.rules.custom_time_limit_s=n;
            else if(k=="customgarbage")c.rules.custom_start_garbage=n;
            else if(k=="vsync")c.vsync=n!=0;
            else if(k=="showinputs")c.show_inputs=n!=0;
            else if(k=="fps")c.fps_cap=n;
            else if(k=="palette")c.palette=static_cast<VisualPalette>(n);
            else if(k=="palettepieces")c.palette_affects_pieces=n!=0;
            else if(k=="texture")c.texture=static_cast<VisualTexture>(n);
            else if(k=="texturecellgap")c.texture_cell_gap=n;
            else if(k=="texturedepth")c.texture_depth=n;
            else if(k=="texturehighlight")c.texture_highlight=n;
            else if(k=="textureshadow")c.texture_shadow=n;
            else if(k=="textureborder")c.texture_border=n;
            else if(k=="texturesoftness")c.texture_softness=n;
            else if(k=="texturereflection")c.texture_reflection=n;
            else if(k=="textureedgelight")c.texture_edge_light=n;
            else if(k=="textureinnerdarken")c.texture_inner_darken=n;
            else if(k=="texturetransparency")c.texture_transparency=n;
            else if(k=="texturecontrast")c.texture_contrast=n;
            else if(k=="texturepatternscale")c.texture_pattern_scale=n;
            else if(k=="texturedotsize")c.texture_dot_size=n;
            else if(k=="texturedotspacing")c.texture_dot_spacing=n;
            else if(k=="textureangle")c.texture_angle=n;
            else if(k=="texturespacing")c.texture_spacing=n;
            else if(k=="texturelinethickness")c.texture_line_thickness=n;
            else if(k=="texturegridsize")c.texture_grid_size=n;
            else if(k=="shader")c.shader=static_cast<VisualShader>(n);
            else if(k=="shaderstrength")c.shader_strength=n;
            else if(k=="shaderscanlines")c.shader_scanlines=n;
            else if(k=="shaderscanlinespacing")c.shader_scanline_spacing=n;
            else if(k=="shaderglow")c.shader_glow=n;
            else if(k=="crtcurvature")c.crt_curvature=n;
            else if(k=="shadervignette")c.shader_vignette=n;
            else if(k=="shadersoftness")c.shader_softness=n;
            else if(k=="shaderpersistence")c.shader_persistence=n;
            else if(k=="shaderflicker")c.shader_flicker=n;
            else if(k=="shaderpixelgrid")c.shader_pixel_grid=n;
            else if(k=="shadergridsize")c.shader_grid_size=n;
            else if(k=="shadersubpixel")c.shader_subpixel=n;
            else if(k=="shadersharpness")c.shader_sharpness=n;
            else if(k=="shaderdotsize")c.shader_dot_size=n;
            else if(k=="shaderdotspacing")c.shader_dot_spacing=n;
            else if(k=="shaderdotbrightness")c.shader_dot_brightness=n;
            else if(k=="shaderradius")c.shader_radius=n;
            else if(k=="shaderthreshold")c.shader_threshold=n;
            else if(k=="shadertraillength")c.shader_trail_length=n;
            else if(k=="shadernoise")c.shader_noise=n;
            else if(k=="shaderjitter")c.shader_horizontal_jitter=n;
            else if(k=="shaderdistortion")c.shader_distortion=n;
            else if(k=="shaderrgboffset")c.shader_rgb_offset=n;
            else if(k=="shaderdirection")c.shader_direction=n;
            else if(k=="shaderlinethickness")c.shader_line_thickness=n;
            else if(k=="shaderbloom")c.shader_bloom=n;
            else if(k=="tournament")c.rules.tournament=n!=0;
            else if(k.rfind("key",0)==0){int i=std::stoi(k.substr(3));if(i>=0&&i<(int)c.keys.size())c.keys[i]=static_cast<SDL_Keycode>(n);}
            else if(k.rfind("pad",0)==0){int i=std::stoi(k.substr(3));if(i>=0&&i<(int)c.pads.size())c.pads[i]=n;}
        }catch(...){}
    }
    clampConfig(c);return true;
}

bool saveConfig(const std::string&path,const AppConfig&c){
    std::ofstream f(path);if(!f)return false;const auto&h=c.rules.handling;
    f<<"das="<<h.das_ms
     <<"\narr="<<h.arr_ms
     <<"\nsdf="<<h.sdf
     <<"\ndcd="<<h.dcd_ms
     <<"\nlock="<<h.lock_delay_ms
     <<"\nresets="<<h.max_lock_resets
     <<"\nallow180="<<h.allow_180
     <<"\nirs="<<h.irs
     <<"\nihs="<<h.ihs
     <<"\nghost="<<c.rules.ghost
     <<"\nnext="<<c.rules.next_count
     <<"\ncustomgrav="<<c.rules.custom_gravity_ms
     <<"\ncustomlines="<<c.rules.custom_line_goal
     <<"\ncustomtime="<<c.rules.custom_time_limit_s
     <<"\ncustomgarbage="<<c.rules.custom_start_garbage
     <<"\nvsync="<<c.vsync
     <<"\nshowinputs="<<c.show_inputs
     <<"\nfps="<<c.fps_cap
     <<"\npalette="<<static_cast<int>(c.palette)
     <<"\npalettepieces="<<c.palette_affects_pieces
     <<"\ntexture="<<static_cast<int>(c.texture)
     <<"\ntexturecellgap="<<c.texture_cell_gap
     <<"\ntexturedepth="<<c.texture_depth
     <<"\ntexturehighlight="<<c.texture_highlight
     <<"\ntextureshadow="<<c.texture_shadow
     <<"\ntextureborder="<<c.texture_border
     <<"\ntexturesoftness="<<c.texture_softness
     <<"\ntexturereflection="<<c.texture_reflection
     <<"\ntextureedgelight="<<c.texture_edge_light
     <<"\ntextureinnerdarken="<<c.texture_inner_darken
     <<"\ntexturetransparency="<<c.texture_transparency
     <<"\ntexturecontrast="<<c.texture_contrast
     <<"\ntexturepatternscale="<<c.texture_pattern_scale
     <<"\ntexturedotsize="<<c.texture_dot_size
     <<"\ntexturedotspacing="<<c.texture_dot_spacing
     <<"\ntextureangle="<<c.texture_angle
     <<"\ntexturespacing="<<c.texture_spacing
     <<"\ntexturelinethickness="<<c.texture_line_thickness
     <<"\ntexturegridsize="<<c.texture_grid_size
     <<"\nshader="<<static_cast<int>(c.shader)
     <<"\nshaderstrength="<<c.shader_strength
     <<"\nshaderscanlines="<<c.shader_scanlines
     <<"\nshaderscanlinespacing="<<c.shader_scanline_spacing
     <<"\nshaderglow="<<c.shader_glow
     <<"\ncrtcurvature="<<c.crt_curvature
     <<"\nshadervignette="<<c.shader_vignette
     <<"\nshadersoftness="<<c.shader_softness
     <<"\nshaderpersistence="<<c.shader_persistence
     <<"\nshaderflicker="<<c.shader_flicker
     <<"\nshaderpixelgrid="<<c.shader_pixel_grid
     <<"\nshadergridsize="<<c.shader_grid_size
     <<"\nshadersubpixel="<<c.shader_subpixel
     <<"\nshadersharpness="<<c.shader_sharpness
     <<"\nshaderdotsize="<<c.shader_dot_size
     <<"\nshaderdotspacing="<<c.shader_dot_spacing
     <<"\nshaderdotbrightness="<<c.shader_dot_brightness
     <<"\nshaderradius="<<c.shader_radius
     <<"\nshaderthreshold="<<c.shader_threshold
     <<"\nshadertraillength="<<c.shader_trail_length
     <<"\nshadernoise="<<c.shader_noise
     <<"\nshaderjitter="<<c.shader_horizontal_jitter
     <<"\nshaderdistortion="<<c.shader_distortion
     <<"\nshaderrgboffset="<<c.shader_rgb_offset
     <<"\nshaderdirection="<<c.shader_direction
     <<"\nshaderlinethickness="<<c.shader_line_thickness
     <<"\nshaderbloom="<<c.shader_bloom
     <<"\ntournament="<<c.rules.tournament<<"\n";
    for(std::size_t i=0;i<c.keys.size();++i)f<<"key"<<i<<'='<<static_cast<long long>(c.keys[i])<<"\n";
    for(std::size_t i=0;i<c.pads.size();++i)f<<"pad"<<i<<'='<<c.pads[i]<<"\n";
    return bool(f);
}

void resetGraphics(AppConfig& c){
    const AppConfig d=defaultConfig();
    c.palette=d.palette;
    c.palette_affects_pieces=d.palette_affects_pieces;
    c.texture=d.texture;
    c.texture_cell_gap=d.texture_cell_gap;
    c.texture_depth=d.texture_depth;
    c.texture_highlight=d.texture_highlight;
    c.texture_shadow=d.texture_shadow;
    c.texture_border=d.texture_border;
    c.texture_softness=d.texture_softness;
    c.texture_reflection=d.texture_reflection;
    c.texture_edge_light=d.texture_edge_light;
    c.texture_inner_darken=d.texture_inner_darken;
    c.texture_transparency=d.texture_transparency;
    c.texture_contrast=d.texture_contrast;
    c.texture_pattern_scale=d.texture_pattern_scale;
    c.texture_dot_size=d.texture_dot_size;
    c.texture_dot_spacing=d.texture_dot_spacing;
    c.texture_angle=d.texture_angle;
    c.texture_spacing=d.texture_spacing;
    c.texture_line_thickness=d.texture_line_thickness;
    c.texture_grid_size=d.texture_grid_size;
    c.shader=d.shader;
    c.shader_strength=d.shader_strength;
    c.shader_scanlines=d.shader_scanlines;
    c.shader_scanline_spacing=d.shader_scanline_spacing;
    c.shader_glow=d.shader_glow;
    c.crt_curvature=d.crt_curvature;
    c.shader_vignette=d.shader_vignette;
    c.shader_softness=d.shader_softness;
    c.shader_persistence=d.shader_persistence;
    c.shader_flicker=d.shader_flicker;
    c.shader_pixel_grid=d.shader_pixel_grid;
    c.shader_grid_size=d.shader_grid_size;
    c.shader_subpixel=d.shader_subpixel;
    c.shader_sharpness=d.shader_sharpness;
    c.shader_dot_size=d.shader_dot_size;
    c.shader_dot_spacing=d.shader_dot_spacing;
    c.shader_dot_brightness=d.shader_dot_brightness;
    c.shader_radius=d.shader_radius;
    c.shader_threshold=d.shader_threshold;
    c.shader_trail_length=d.shader_trail_length;
    c.shader_noise=d.shader_noise;
    c.shader_horizontal_jitter=d.shader_horizontal_jitter;
    c.shader_distortion=d.shader_distortion;
    c.shader_rgb_offset=d.shader_rgb_offset;
    c.shader_direction=d.shader_direction;
    c.shader_line_thickness=d.shader_line_thickness;
    c.shader_bloom=d.shader_bloom;
}

void resetSettings(AppConfig& c){
    const AppConfig d=defaultConfig();
    c.rules=d.rules;
    c.vsync=d.vsync;
    c.show_inputs=d.show_inputs;
    c.fps_cap=d.fps_cap;
    resetGraphics(c);
}

void resetControls(AppConfig& c){
    const AppConfig defaults=defaultConfig();
    c.keys=defaults.keys;
    c.pads=defaults.pads;
}

const char* padName(int b){
    switch(static_cast<SDL_GamepadButton>(b)){
        case SDL_GAMEPAD_BUTTON_SOUTH:return "A/SOUTH";case SDL_GAMEPAD_BUTTON_EAST:return "B/EAST";case SDL_GAMEPAD_BUTTON_WEST:return "X/WEST";case SDL_GAMEPAD_BUTTON_NORTH:return "Y/NORTH";case SDL_GAMEPAD_BUTTON_BACK:return "BACK";case SDL_GAMEPAD_BUTTON_GUIDE:return "GUIDE";case SDL_GAMEPAD_BUTTON_START:return "START";case SDL_GAMEPAD_BUTTON_LEFT_STICK:return "L3";case SDL_GAMEPAD_BUTTON_RIGHT_STICK:return "R3";case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:return "LB";case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:return "RB";case SDL_GAMEPAD_BUTTON_DPAD_UP:return "DPAD UP";case SDL_GAMEPAD_BUTTON_DPAD_DOWN:return "DPAD DOWN";case SDL_GAMEPAD_BUTTON_DPAD_LEFT:return "DPAD LEFT";case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:return "DPAD RIGHT";default:return "UNBOUND";
    }
}
} // namespace fasttris::app
