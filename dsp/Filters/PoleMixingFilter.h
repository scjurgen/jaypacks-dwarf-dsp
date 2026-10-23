#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <iterator>
#include <string_view>

#include "dsp/Filters/OnePoleLowpass.h"

namespace jp
{

/// Response families of the pole-mixing presets, combinable as flags.
enum PoleMixingFamily : unsigned
{
    PM_OBERHEIM = 0x001, ///< one of the 15 filter modes of the Oberheim Xpander / Matrix-12 (or a variant)
    PM_LOWPASS = 0x004,
    PM_HIGHPASS = 0x008,
    PM_BANDPASS = 0x010,
    PM_NOTCH = 0x020,
    PM_ALLPASS = 0x040,
    PM_PHASER = 0x080, ///< allpass and notch mixes meant for phaser-like sweeps
    PM_MIXED = 0x100,  ///< combinations, e.g. highpass plus lowpass or notch plus lowpass
};

/**
 * @brief One pole-mixing preset: the five tap weights and the resonance shaping.
 *
 * The output is w_in * feed - w[0] * s1 + w[1] * s2 - w[2] * s3 + w[3] * s4, with s1..s4
 * the outputs of the four cascaded one-pole stages. The alternating signs are applied by
 * the filter, so the weights read like the coefficients of a polynomial in -LP: highpass n
 * is (1 - LP)^n, e.g. 2-pole highpass = {1, {2, 1, 0, 0}}.
 */
struct PoleMixingPreset
{
    float inputWeight;
    std::array<float, 4> stageWeights;
    float resonanceFeedback;  ///< scales the stage-4 feedback for this mix (how fast it reaches self-oscillation)
    float resonanceInputGain; ///< input boost in*(1 + r*gain) that compensates the level loss of resonance
    const char* name;
    unsigned family;
};

// clang-format off
/// Mix table. Entries 0..14 are the 15 filter modes of the Oberheim Xpander / Matrix-12 in
/// manual order, entry 15 repeats entry 11 with different resonance shaping, entries whose
/// name starts with '*' are additions.
/// @see https://electricdruid.net/multimode-filters-part-2-pole-mixing-filters
inline constexpr PoleMixingPreset c_poleMixingPresets[] = {
    {0,    {-1,   0,    0,    0},    1.0f,   0.5f,  "6 lp(inv)",                     PM_OBERHEIM | PM_LOWPASS},
    {0,    {0,    1,    0,    0},    1.25f,  0.7f,  "2 pole lp",                     PM_OBERHEIM | PM_LOWPASS},
    {0,    {0,    0,    -1,   0},    1.5f,   0.5f,  "3 pole lp(inv)",                PM_OBERHEIM | PM_LOWPASS},
    {0,    {0,    0,    0,    1},    1.7f,   0.5f,  "4 pole lp",                     PM_OBERHEIM | PM_LOWPASS},
    {1,    {1,    0,    0,    0},    1.6f,   0.0f,  "1 pole hp",                     PM_OBERHEIM | PM_HIGHPASS},
    {1,    {2,    1,    0,    0},    1.8f,   0.0f,  "2 pole hp",                     PM_OBERHEIM | PM_HIGHPASS},
    {1,    {3,    3,    1,    0},    1.88f,  0.0f,  "3 pole hp",                     PM_OBERHEIM | PM_HIGHPASS},
    {0,    {2,    2,    0,    0},    1.5f,   0.0f,  "2 pole bp",                     PM_OBERHEIM | PM_BANDPASS},
    {0,    {0,    4,    8,    4},    1.5f,   0.0f,  "4 pole bp",                     PM_OBERHEIM | PM_BANDPASS},
    {1,    {2,    2,    0,    0},    2.0f,   0.0f,  "2 pole notch",                  PM_OBERHEIM | PM_NOTCH},
    {-1,   {-6,   -12,  -8,   0},    1.2f,   0.2f,  "3 pole ap (inv)",               PM_OBERHEIM | PM_ALLPASS},
    {0,    {3,    6,    3,    0},    1.4f,   0.0f,  "2 pole hp + 1 pole lp",         PM_OBERHEIM | PM_MIXED},
    {0,    {3,    9,    9,    3},    1.55f,  0.0f,  "3 pole hp + 1 pole lp",         PM_OBERHEIM | PM_MIXED},
    {0,    {1,    2,    2,    0},    1.75f,  1.0f,  "2 pole notch + 1 pole lp",      PM_OBERHEIM | PM_MIXED},
    {0,    {1,    3,    6,    4},    1.5f,   0.25f, "3 pole ap + 1 pole lp",         PM_OBERHEIM | PM_MIXED},
    {0,    {3,    6,    3,    0},    1.2f,   0.25f, "2 pole hp + 1 pole lp B",       PM_OBERHEIM | PM_MIXED},
    {-1,   {-2,   0,    0,    0},    1.3f,   0.2f,  "* 1 pole ap (inv)",             PM_PHASER},
    {1,    {4,    4,    0,    0},    1.3f,   0.2f,  "* 2 pole ap",                   PM_PHASER},
    {-1,   {-6,   -12,  -8,   0},    1.3f,   0.2f,  "* 3 pole ap (inv)",             PM_PHASER},
    {1,    {8,    24,   32,   16},   1.3f,   0.2f,  "* 4 pole ap",                   PM_PHASER},
    {1,    {2,    2,    0,    0},    2.0f,   0.0f,  "* 2 pole phaser",               PM_PHASER},
    {1,    {3,    6,    4,    0},    1.5f,   0.0f,  "* 3 pole phaser",               PM_PHASER},
    {1,    {4,    12,   16,   8},    1.5f,   0.0f,  "* 4 pole phaser",               PM_PHASER},
    {1,    {4,    6,    4,    1},    1.975f, 0.0f,  "* 4 pole hp",                   PM_HIGHPASS},
    {0,    {-4,   -7,   -4,   0},    1.5f,   0.0f,  "* double lp exp(inv)",          PM_MIXED | PM_LOWPASS},
    {0,    {-4,   -8,   -5,   0},    1.3f,   0.25f, "* LP 1 + Notch -12(inv)",       PM_MIXED},
    {0,    {0,    4,    8,    5},    1.3f,   0.35f, "* LP 2 + Notch -12",            PM_MIXED},
    {0.25f,{0.5f, 1.25f,0,    0},    1.6f,   0.3f,  "* Reso before Notch",           PM_MIXED},
    {1.5f, {3,    2,    0,    0},    1.6f,   0.3f,  "* Reso after Notch",            PM_MIXED},
    {1,    {4,    10,   12,   6},    1.7f,   0.0f,  "* Double Notch A (+- Octaves)", PM_MIXED},
    {1,    {4,    10.25f,12.5f,6.25f},1.7f,  0.0f,  "* Double Notch Octaves",        PM_MIXED},
    {1,    {4,    11,   14,   7},    1.5f,   0.0f,  "* Double Notch B",              PM_MIXED},
    {1,    {4,    12,   16,   8},    1.4f,   0.0f,  "* Double Notch C (Dixon)",      PM_MIXED},
    {0.75f,{3,    10.5f,15,   7.5f}, 1.4f,   0.0f,  "* Double Notch D",              PM_MIXED},
    {1,    {0,    4,    8,    4},    1.0f,   0.0f,  "Bell",                          PM_BANDPASS},
};
// clang-format on

inline constexpr size_t c_poleMixingPresetCount{std::size(c_poleMixingPresets)};
inline constexpr size_t c_poleMixingPresetNotFound{c_poleMixingPresetCount};

/// Index of the preset with exactly this name, or c_poleMixingPresetNotFound.
[[nodiscard]] constexpr size_t findPoleMixingPreset(const std::string_view name) noexcept
{
    for (size_t i = 0; i < c_poleMixingPresetCount; ++i)
    {
        if (name == c_poleMixingPresets[i].name)
        {
            return i;
        }
    }
    return c_poleMixingPresetNotFound;
}

/// Linear (small-signal, no stage clamping) transfer function of the filter for a preset,
/// pole and resonance, evaluated at one frequency. For analysis and tests, not the audio path:
///   H = (1 + r g) M(L) / (1 + r a z^-1 L^4),  L = (1 - p) / (1 - p z^-1),
///   M(L) = w_in - w0 L + w1 L^2 - w2 L^3 + w3 L^4,
/// with g = resonanceInputGain, a = resonanceFeedback. The z^-1 in the loop is the one-sample
/// delay of the feedback (the previous stage-4 output is fed back).
[[nodiscard]] inline std::complex<double> poleMixingResponse(const PoleMixingPreset& preset, const double pole,
                                                             const double resonance, const double frequencyHz,
                                                             const double sampleRate) noexcept
{
    constexpr double twoPi{6.283185307179586476925286766559};
    const std::complex<double> zInv = std::polar(1.0, -twoPi * frequencyHz / sampleRate);
    const std::complex<double> lp = (1.0 - pole) / (1.0 - pole * zInv);
    const std::complex<double> lp2 = lp * lp;
    const std::complex<double> lp4 = lp2 * lp2;
    const auto& w = preset.stageWeights;
    const std::complex<double> mix = static_cast<double>(preset.inputWeight) - static_cast<double>(w[0]) * lp +
                                     static_cast<double>(w[1]) * lp2 - static_cast<double>(w[2]) * lp2 * lp +
                                     static_cast<double>(w[3]) * lp4;
    const double inputGain = 1.0 + resonance * preset.resonanceInputGain;
    const double feedback = resonance * preset.resonanceFeedback;
    return inputGain * mix / (1.0 + feedback * zInv * lp4);
}

/// Loop gain k = r * a at which the linear filter starts to self-oscillate, for a stage pole p
/// in (0, 1): the smallest k for which 1 + k z^-1 L^4 has a root on the unit circle. Solved
/// numerically (a scan plus bisection, about 1000 trig calls): for analysis, tests and setup
/// code, not per sample. For p <= 1/3 (cutoff above fs ln(3) / (2 pi), about 0.175 fs) the
/// loop can only oscillate at Nyquist, where k = ((1 + p) / (1 - p))^4.
[[nodiscard]] inline double poleMixingOscillationThreshold(const double pole) noexcept
{
    constexpr double pi{3.14159265358979323846};
    const double nyquistThreshold = std::pow((1.0 + pole) / (1.0 - pole), 4.0);
    // Loop phase -pi is reached where w + 4 * lag(w) = pi, lag(w) = arg of 1 / (1 - p e^-jw).
    const auto phaseError = [pole](const double w)
    { return w + 4.0 * std::atan2(pole * std::sin(w), 1.0 - pole * std::cos(w)) - pi; };
    const auto loopGainAt = [pole](const double w)
    {
        const double magnitudeSquared = (1.0 - 2.0 * pole * std::cos(w) + pole * pole) / ((1.0 - pole) * (1.0 - pole));
        return magnitudeSquared * magnitudeSquared;
    };
    constexpr int scanPoints{512};
    double threshold = nyquistThreshold;
    double previousW = 0.0;
    double previousError = phaseError(0.0);
    for (int i = 1; i < scanPoints; ++i)
    {
        const double w = pi * i / scanPoints;
        const double error = phaseError(w);
        if ((previousError < 0.0) != (error < 0.0))
        {
            double lo = previousW;
            double hi = w;
            for (int iteration = 0; iteration < 60; ++iteration)
            {
                const double mid = 0.5 * (lo + hi);
                if ((phaseError(mid) < 0.0) == (previousError < 0.0))
                {
                    lo = mid;
                }
                else
                {
                    hi = mid;
                }
            }
            threshold = std::min(threshold, loopGainAt(0.5 * (lo + hi)));
        }
        previousW = w;
        previousError = error;
    }
    return threshold;
}

/// Stage pole and oscillation threshold that put the resonance peak at a given frequency.
struct PoleMixingTuning
{
    float pole;
    float oscillationThreshold; ///< loop gain r * a at which the filter starts to self-oscillate
};

/// Tuning that makes the filter self-oscillate exactly at peakHz, for 0 < peakHz < fs / 2: the
/// inverse of poleMixingOscillationThreshold() in closed form, cheap enough per sample (four
/// trig calls). The loop phase is -pi where w + 4 phi = pi with phi = arg L(w), so for
/// w = 2 pi peakHz / fs:
///   phi = (pi - w) / 4,  p = sin(phi) / sin(w + phi),
///   k = ((1 - 2 p cos(w) + p^2) / (1 - p)^2)^2  (the inverse loop magnitude |L(w)|^-4).
/// p stays above 1/3 for every peak below Nyquist, so k is the real threshold (the Nyquist
/// branch is higher); e.g. peakHz = fs / 6 gives p = 0.5, k = 9. Below the threshold the
/// response peak sits slightly lower: 1 to 2 % at 95 % of k, 4 to 5.5 % at 85 %.
/// Matches the measured 48 kHz cutoff correction of AbacDsp's FourStageFilter (same feedback
/// topology) within 0.2 % up to 4 kHz.
[[nodiscard]] inline PoleMixingTuning poleMixingTuningForPeak(const float peakHz, const float sampleRate) noexcept
{
    constexpr float pi{3.14159265358979323846f};
    const float w = 2.0f * pi * peakHz / sampleRate;
    const float phi = 0.25f * (pi - w);
    const float sinWPlusPhi = std::sin(w + phi);
    const float pole = std::sin(phi) / sinWPlusPhi;
    // 1 - p and 1 - 2 p cos(w) + p^2 = (1 - p)^2 + 4 p sin^2(w / 2) without cancellation near p = 1
    const float sinHalfW = std::sin(0.5f * w);
    const float oneMinusPole = 2.0f * std::cos(phi + 0.5f * w) * sinHalfW / sinWPlusPhi;
    const float ratio = 2.0f * sinHalfW / oneMinusPole;
    const float magnitudeSquared = 1.0f + pole * ratio * ratio;
    return {pole, magnitudeSquared * magnitudeSquared};
}

/**
 * @brief Pole-mixing multimode filter: four cascaded one-pole lowpass stages with resonance
 * feedback from stage 4, output = weighted mix of the input and the four stage outputs.
 *
 * All responses of the preset table (lowpass 1-4 pole, highpass, bandpass, notch, allpass,
 * combinations) cost the same four stages; switching presets changes five weights and keeps
 * the stage states, so it does not reset or click the filter.
 *
 * Resonance r >= 0 feeds -r * a * s4[n-1] back into the cascade input and boosts the input by
 * (1 + r * g), a and g taken from the preset. The loop self-oscillates when r * a exceeds a
 * threshold that depends on the cutoff (linear model, fs = 48 kHz): 4.05 at 100 Hz, 4.58 at
 * 1 kHz, 6.2 at 3 kHz, 10.2 at 6 kHz, because the one-pole stages lose phase lag and the
 * one-sample feedback delay adds lag as the cutoff rises (poleMixingOscillationThreshold()).
 * For the same reason the resonance peak lies above the cutoff: 1.07 kHz at 1 kHz, 3.7 kHz at
 * 3 kHz, 9.8 kHz at 6 kHz. Above fs ln(3) / (2 pi) (about 0.175 fs) the loop oscillates at
 * Nyquist instead (above 5.4 at 12 kHz). To place the resonance peak instead of the stage
 * cutoff, set the pole from poleMixingTuningForPeak() with setPole(). Below the threshold, a silent input
 * lets the state decay towards denormal values; denormal handling is up to the caller.
 * Every stage clamps its state to [-1, 1] (see OnePoleLowpass::processClamped):
 * this bounds the output for any resonance and also clips input above full scale even at
 * r = 0, which is part of the character, not a safety margin.
 *
 * The digital "allpass" mixes use 1 - 2 L, which is exactly allpass for an analog one-pole but
 * only approximately for this impulse-invariant one-pole; the error grows with the cutoff.
 *
 * Cost: one exp() per setCutoff()/setSampleRate(), none per sample. Real-time safe.
 * @see https://electricdruid.net/multimode-filters-part-2-pole-mixing-filters
 * @see https://expeditionelectronics.com/Diy/Polemixing
 */
class PoleMixingFilter
{
  public:
    static constexpr float c_stageLimit{1.0f};

    PoleMixingFilter() noexcept
    {
        updatePole();
        setPreset(0);
    }

    void setSampleRate(const float sampleRate) noexcept
    {
        m_sampleRate = sampleRate;
        updatePole();
    }

    /// Cutoff of each one-pole stage (not the resonance peak, see poleMixingTuningForPeak()).
    void setCutoff(const float cutoffHz) noexcept
    {
        m_cutoff = cutoffHz;
        updatePole();
    }

    /// Sets the stage pole directly, e.g. from poleMixingTuningForPeak(); a later setCutoff()
    /// or setSampleRate() replaces it.
    void setPole(const float pole) noexcept
    {
        m_pole = pole;
        for (auto& stage : m_stages)
        {
            stage.setPole(pole);
        }
    }

    /// Resonance r >= 0 (negative values are treated as 0).
    void setResonance(const float resonance) noexcept
    {
        m_resonance = resonance > 0.0f ? resonance : 0.0f;
    }

    /// Selects a row of c_poleMixingPresets; an index outside the table is ignored.
    void setPreset(const size_t index) noexcept
    {
        if (index >= c_poleMixingPresetCount)
        {
            return;
        }
        m_presetIndex = index;
        m_preset = &c_poleMixingPresets[index];
    }

    [[nodiscard]] size_t getPresetIndex() const noexcept
    {
        return m_presetIndex;
    }

    [[nodiscard]] float getPole() const noexcept
    {
        return m_pole;
    }

    void reset() noexcept
    {
        for (auto& stage : m_stages)
        {
            stage.reset();
        }
        m_lastStage4 = 0.0f;
    }

    [[nodiscard]] float process(const float in) noexcept
    {
        const auto& p = *m_preset;
        const float feed =
            in * (1.0f + m_resonance * p.resonanceInputGain) - m_lastStage4 * m_resonance * p.resonanceFeedback;
        const float s1 = m_stages[0].processClamped(feed, c_stageLimit);
        const float s2 = m_stages[1].processClamped(s1, c_stageLimit);
        const float s3 = m_stages[2].processClamped(s2, c_stageLimit);
        const float s4 = m_stages[3].processClamped(s3, c_stageLimit);
        m_lastStage4 = s4;
        const auto& w = p.stageWeights;
        return p.inputWeight * feed - w[0] * s1 + w[1] * s2 - w[2] * s3 + w[3] * s4;
    }

    void process(const float* in, float* out, const size_t numSamples) noexcept
    {
        for (size_t i = 0; i < numSamples; ++i)
        {
            out[i] = process(in[i]);
        }
    }

  private:
    void updatePole() noexcept
    {
        m_pole = onePoleLowpassPole(m_cutoff, m_sampleRate);
        for (auto& stage : m_stages)
        {
            stage.setPole(m_pole);
        }
    }

    float m_sampleRate{48000.0f};
    float m_cutoff{1000.0f};
    float m_resonance{0.0f};
    float m_pole{0.0f};
    float m_lastStage4{0.0f};
    size_t m_presetIndex{0};
    const PoleMixingPreset* m_preset{&c_poleMixingPresets[0]};
    std::array<OnePoleLowpass, 4> m_stages{};
};

}
