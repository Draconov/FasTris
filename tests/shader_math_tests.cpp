#include "shader_math.hpp"

#include <cassert>
#include <cmath>

int main() {
    using fasttris::app::shader_math::VignetteProfile;
    using fasttris::app::shader_math::vignetteAlphaAt;
    using fasttris::app::shader_math::vignetteAlphaAtPoint;
    using fasttris::app::shader_math::vignetteProfile;
    using fasttris::app::shader_math::vignetteStrengthAlpha;


    // Vignette darkness keeps the lower half close to the old response, but
    // the upper half ramps strongly so 100% reaches near-black.
    assert(vignetteStrengthAlpha(0, 100) == 0);
    assert(vignetteStrengthAlpha(25, 100) >= 35 && vignetteStrengthAlpha(25, 100) <= 40);
    assert(vignetteStrengthAlpha(50, 100) >= 73 && vignetteStrengthAlpha(50, 100) <= 77);
    assert(vignetteStrengthAlpha(75, 100) > 125);
    assert(vignetteStrengthAlpha(100, 100) >= 240);
    assert(vignetteStrengthAlpha(100, 50) >= 118 && vignetteStrengthAlpha(100, 50) <= 125);

    // Edge softness changes only the inward falloff. It must not weaken the
    // fully dark outer corner or touch the clear center.
    const int tight_mid = vignetteAlphaAtPoint(1600, 900, 180.0f, 120.0f, 245, 50, 0);
    const int broad_mid = vignetteAlphaAtPoint(1600, 900, 180.0f, 120.0f, 245, 50, 100);
    assert(broad_mid > tight_mid);
    assert(vignetteAlphaAtPoint(1600, 900, 0.0f, 0.0f, 245, 50, 0) == 245);
    assert(vignetteAlphaAtPoint(1600, 900, 0.0f, 0.0f, 245, 50, 100) == 245);

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

    // A real vignette is elliptical/radial, not a stack of rectangular rings.
    // The center is untouched, corners are darkest, and samples along a
    // diagonal increase smoothly without plateaus or frame-like steps.
    const int center = vignetteAlphaAtPoint(1600, 900, 800.0f, 450.0f, 180, 50, 50);
    const int edge = vignetteAlphaAtPoint(1600, 900, 800.0f, 0.0f, 180, 50, 50);
    const int corner = vignetteAlphaAtPoint(1600, 900, 0.0f, 0.0f, 180, 50, 50);
    assert(center == 0);
    assert(edge > 0);
    assert(corner > edge);
    assert(corner == 180);

    int previous = 0;
    for (int i = 1; i <= 10; ++i) {
        const float t = i / 10.0f;
        const int sample = vignetteAlphaAtPoint(1600, 900,
            800.0f * (1.0f - t), 450.0f * (1.0f - t), 180, 50, 50);
        assert(sample >= previous);
        previous = sample;
    }
    assert(previous == 180);

    return 0;
}
