#include "shader_math.hpp"

#include <cassert>
#include <cmath>

int main() {
    using fasttris::app::shader_math::VignetteProfile;
    using fasttris::app::shader_math::vignetteAlphaAt;
    using fasttris::app::shader_math::vignetteProfile;

    const VignetteProfile profile = vignetteProfile(1760, 880, 50, 0);
    assert(profile.layers >= 20);
    assert(profile.max_inset > 150.0f);
    assert(profile.band_width >= 1.0f);
    assert(profile.band_width * profile.layers + 0.01f >= profile.max_inset);

    // Strength must come from the Vignette control, not from Softness.
    assert(vignetteAlphaAt(75, 0.0f, 0) == 75);
    assert(vignetteAlphaAt(75, 0.0f, 100) == 75);

    // The effect must fade smoothly toward the center instead of becoming
    // isolated one-pixel rings. Higher softness broadens that fade.
    const int hard_mid = vignetteAlphaAt(75, 0.5f, 0);
    const int soft_mid = vignetteAlphaAt(75, 0.5f, 100);
    assert(hard_mid > 0);
    assert(soft_mid > hard_mid);
    assert(vignetteAlphaAt(75, 1.0f, 50) == 0);

    return 0;
}
