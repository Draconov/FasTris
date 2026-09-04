#pragma once

#include <algorithm>
#include <cmath>

namespace fasttris::app::shader_math {

struct VignetteProfile {
    float max_inset{};
    float band_width{};
    int layers{};
};

inline VignetteProfile vignetteProfile(int width, int height, int radius, int softness) {
    const float radius01 = std::clamp(radius, 0, 100) / 100.0f;
    const float reach = (1.0f - radius01) * 0.30f + 0.035f;
    const float max_inset = std::max(1.0f, std::min(width, height) * reach);
    const int layers = std::clamp(24 + std::clamp(softness, 0, 100) / 5, 24, 44);
    return {max_inset, max_inset / static_cast<float>(layers), layers};
}

inline int vignetteStrengthAlpha(int vignette_percent, int shader_strength) {
    const float local = std::clamp(vignette_percent, 0, 100) / 100.0f;
    const float strength = std::clamp(shader_strength, 0, 100) / 100.0f;
    if (local <= 0.0f || strength <= 0.0f) return 0;

    // Preserve the familiar low/mid response (0-50% maps exactly to the old
    // 150-alpha scale), then ramp the upper half more aggressively so 100%
    // reaches a near-black 245 alpha instead of topping out around 150.
    float alpha = 0.0f;
    if (local <= 0.5f) {
        alpha = local * 150.0f;
    } else {
        const float upper = (local - 0.5f) * 2.0f;
        alpha = 75.0f + 170.0f * std::pow(upper, 1.35f);
    }
    return std::clamp(static_cast<int>(std::lround(alpha * strength)), 0, 245);
}

inline int vignetteAlphaAt(int base_alpha, float normalized_inset, int softness) {
    if (base_alpha <= 0) return 0;
    const float t = std::clamp(normalized_inset, 0.0f, 1.0f);
    if (t >= 1.0f) return 0;

    // Softness controls how broadly the edge darkness fades inward. It must
    // never weaken the requested edge intensity itself.
    const float soft = std::clamp(softness, 0, 100) / 100.0f;
    const float exponent = 3.0f - 1.65f * soft;
    const float shaped = std::pow(1.0f - t, exponent);
    return std::clamp(static_cast<int>(std::lround(base_alpha * shaped)), 0, 255);
}

inline int vignetteAlphaAtPoint(int width, int height, float x, float y,
                                int base_alpha, int radius, int softness) {
    if (base_alpha <= 0 || width <= 0 || height <= 0) return 0;

    const float half_w = std::max(1.0f, width * 0.5f);
    const float half_h = std::max(1.0f, height * 0.5f);
    const float nx = std::abs((x - half_w) / half_w);
    const float ny = std::abs((y - half_h) / half_h);
    const float distance = std::sqrt(nx * nx + ny * ny);

    // Radius controls how far inward the vignette begins. A larger radius
    // keeps the center clearer; corners still reach the requested strength.
    const float radius01 = std::clamp(radius, 0, 100) / 100.0f;
    const float inner = 0.42f + radius01 * 0.48f;
    constexpr float outer = 1.41421356237f;
    float t = (distance - inner) / std::max(0.001f, outer - inner);
    t = std::clamp(t, 0.0f, 1.0f);

    // Smoothstep removes contour discontinuities. Softness broadens the
    // transition without changing the maximum corner opacity.
    t = t * t * (3.0f - 2.0f * t);
    const float soft = std::clamp(softness, 0, 100) / 100.0f;
    const float exponent = 2.35f - 1.35f * soft;
    const float shaped = std::pow(t, exponent);
    return std::clamp(static_cast<int>(std::lround(base_alpha * shaped)), 0, 255);
}

} // namespace fasttris::app::shader_math
