#pragma once

#include <array>
#include <cmath>
#include <cstddef>

#include "dsp/Filters/OnePoleLowpass.h"
#include "dsp/Filters/PoleMixingCorrections.h"
#include "dsp/Filters/PoleMixingFilter.h"

namespace jp
{

/**
 * @brief Pole-mixing multimode filter with band-pass feedback and a saturated loop, the resonance of AbacDsp's
 * Filter1Pole4StageSmooth: four cascaded one-pole lowpass stages, output a weighted mix of the loop input and
 * the stage outputs, controls smoothed per sample.
 *
 * It differs from PoleMixingFilter in how resonance works. The loop input is
 *   f = s(in - r (2 v3 - v2 - v4)),  s(x) = x / sqrt(1 + x^2),
 * with v1..v4 the stage outputs of the previous sample: the feedback is the second difference of adjacent stages, a
 * band-pass, so frequencies far below the corner are left alone (the pass band does not fall with resonance, where
 * the stage-4 feedback of PoleMixingFilter divides the low end by 1 + loop gain) and the peak rises at the
 * corner; the saturator rounds the loop's growth where it passes full scale, with no hard limit. The mix is
 * the preset's weights on f and v1..v4, taken from c_poleMixingPresets (the filter applies the alternating signs that
 * the table leaves out, so the weights {w_in, {a, b, c, d}} become {w_in, -a, b, -c, d}).
 *
 * The cutoff is a target frequency for the resonance peak: the stage pole follows from the fitted warp
 * (correctedRawCutoffForTargetFrequency) and the resonance is normalised, 1 being the measured self-oscillation
 * threshold at that cutoff (criticalResonanceForRawCutoff); both fits are for 48 kHz. Pole and resonance follow
 * their targets by a one-pole smoother (never quite arriving; reset() snaps them), which absorbs a stream of
 * control changes without stair steps. Values above 1 oscillate, bounded by the saturator. No allocation, no
 * locks. The state decays towards denormals on a silent input below the threshold: handling is up to the caller.
 */
class PoleMixingBandpassFilter
{
  public:
    explicit PoleMixingBandpassFilter(const float sampleRate) noexcept
        : m_sampleRate(sampleRate)
    {
        setPreset(3); // "4 pole lp"
        setSmoothingMs(3.0f);
        setCutoffFrequency(1000.0f);
        m_pole = m_targetPole;
    }

    /// Selects a row of c_poleMixingPresets; an index outside the table is ignored. The stage states are kept.
    void setPreset(const size_t index) noexcept
    {
        if (index >= c_poleMixingPresetCount)
        {
            return;
        }
        const PoleMixingPreset& preset = c_poleMixingPresets[index];
        m_weights = {preset.inputWeight, -preset.stageWeights[0], preset.stageWeights[1], -preset.stageWeights[2],
                     preset.stageWeights[3]};
        m_presetIndex = index;
    }

    [[nodiscard]] size_t getPresetIndex() const noexcept
    {
        return m_presetIndex;
    }

    /// Time constant of the smoother of pole and resonance in milliseconds.
    void setSmoothingMs(const float milliseconds) noexcept
    {
        m_smoothingAlpha = 1.0f - std::exp(-1.0f / (milliseconds * 1e-3f * m_sampleRate));
    }

    /// Normalised resonance: 0 none, 1 the self-oscillation threshold at the current cutoff.
    void setResonance(const float normalized) noexcept
    {
        m_normalizedResonance = normalized;
        m_targetResonance = normalized * criticalResonanceForRawCutoff(m_rawCutoff);
    }

    /// Frequency of the resonance peak in Hz.
    void setCutoffFrequency(const float cutoffHz) noexcept
    {
        m_rawCutoff = correctedRawCutoffForTargetFrequency(cutoffHz);
        m_targetPole = onePoleLowpassPole(m_rawCutoff, m_sampleRate);
        m_targetResonance = m_normalizedResonance * criticalResonanceForRawCutoff(m_rawCutoff);
    }

    /// Clears the stages and puts pole and resonance at their targets.
    void reset() noexcept
    {
        m_v = {};
        m_pole = m_targetPole;
        m_reso = m_targetResonance;
    }

    [[nodiscard]] float process(const float in) noexcept
    {
        m_pole += m_smoothingAlpha * (m_targetPole - m_pole);
        m_reso += m_smoothingAlpha * (m_targetResonance - m_reso);
        const float feedback = saturate(in - bandpassTap() * m_reso);
        m_v[0] = feedback + m_pole * (m_v[0] - feedback);
        m_v[1] = m_v[0] + m_pole * (m_v[1] - m_v[0]);
        m_v[2] = m_v[1] + m_pole * (m_v[2] - m_v[1]);
        m_v[3] = m_v[2] + m_pole * (m_v[3] - m_v[2]);
        return m_weights[0] * feedback + m_weights[1] * m_v[0] + m_weights[2] * m_v[1] + m_weights[3] * m_v[2] +
               m_weights[4] * m_v[3];
    }

    void process(const float* in, float* out, const size_t numSamples) noexcept
    {
        for (size_t i = 0; i < numSamples; ++i)
        {
            out[i] = process(in[i]);
        }
    }

  private:
    [[nodiscard]] static float saturate(const float x) noexcept
    {
        return x / std::sqrt(1.0f + x * x);
    }

    /// The second difference of adjacent stage outputs, -v4 + 2 v3 - v2.
    [[nodiscard]] float bandpassTap() const noexcept
    {
        return -m_v[3] + 2.0f * m_v[2] - m_v[1];
    }

    float m_sampleRate;
    std::array<float, 5> m_weights{};
    size_t m_presetIndex{0};
    float m_smoothingAlpha{0.01f};
    float m_rawCutoff{1000.0f};
    float m_targetPole{0.0f};
    float m_pole{0.0f};
    float m_normalizedResonance{0.0f};
    float m_targetResonance{0.0f};
    float m_reso{0.0f};
    std::array<float, 4> m_v{};
};

}
