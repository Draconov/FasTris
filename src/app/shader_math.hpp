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

} // namespace fasttris::app::shader_math
