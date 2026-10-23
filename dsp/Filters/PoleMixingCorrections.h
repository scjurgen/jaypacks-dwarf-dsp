#pragma once

#include <algorithm>
#include <cmath>

namespace jp
{

/**
 * @brief The raw one-pole cutoff, in Hz, that places the resonance peak of a four-stage pole-mixing filter with
 * band-pass feedback (PoleMixingBandpassFilter) at a target frequency, at 90 % of critical resonance, at 48 kHz.
 *
 * A degree-7 polynomial in log(targetHz) to log(rawCutoff), fitted to measurements of AbacDsp's
 * Filter1Pole4StageSmooth (largest relative error 1.55 % over the fit domain 20 Hz to 8564.1 Hz, mean 0.52 %).
 * The target is clamped to the domain (above it the value stays that of its upper edge, 11020 Hz), the result to
 * [10, 22000] Hz. The fit is for 48 kHz only.
 * @see https://github.com/scjurgen/AbacDsp (PoleMixingCorrections_generated.h)
 */
[[nodiscard]] inline float correctedRawCutoffForTargetFrequency(const float targetHz) noexcept
{
    const float clampedHz = std::clamp(targetHz, 20.0f, 8564.1f);
    const float x = std::log(clampedHz);
    float y = 9.6349029396e-05f;
    y = y * x + -3.8678729667e-03f;
    y = y * x + 6.5635779915e-02f;
    y = y * x + -6.0948291407e-01f;
    y = y * x + 3.3407285201e+00f;
    y = y * x + -1.0797447481e+01f;
    y = y * x + 2.0035427289e+01f;
    y = y * x + -1.4090183062e+01f;
    return std::clamp(std::exp(y), 10.0f, 22000.0f);
}

/**
 * @brief The raw feedback amount at which PoleMixingBandpassFilter self-oscillates, for a raw cutoff, at 48 kHz.
 *
 * A degree-7 polynomial in log(rawCutoffHz) to log(criticalResonance), fitted to measurements of AbacDsp's
 * Filter1Pole4StageSmooth (largest relative error 0.90 % over the fit domain 20 Hz to 19706.4 Hz, mean 0.25 %);
 * the cutoff is clamped to the domain. Multiplied by a normalised resonance (0 none, 1 at the threshold) it gives
 * the feedback, so a value means about the same at any cutoff.
 */
[[nodiscard]] inline float criticalResonanceForRawCutoff(const float rawCutoffHz) noexcept
{
    const float clampedHz = std::clamp(rawCutoffHz, 20.0f, 19706.4f);
    const float x = std::log(clampedHz);
    float y = 1.5652016830e-04f;
    y = y * x + -6.3862819811e-03f;
    y = y * x + 1.1006970626e-01f;
    y = y * x + -1.0354941443e+00f;
    y = y * x + 5.7326806873e+00f;
    y = y * x + -1.8653801684e+01f;
    y = y * x + 3.3017260953e+01f;
    y = y * x + -2.3147311425e+01f;
    return std::exp(y);
}

}
