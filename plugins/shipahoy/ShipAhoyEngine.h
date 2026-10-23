#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "dsp/Filters/PoleMixingFilter.h"
#include "dsp/Generators/CutoffTrajectory.h"
#include "dsp/Generators/OrnsteinUhlenbeck.h"
#include "dsp/Numbers/Convert.h"
#include "dsp/Numbers/PanLaw.h"
#include "dsp/Numbers/Random.h"
#include "dsp/Parameters/ParameterSmoothing.h"

namespace jp
{

/**
 * @brief Pole-mixing filter whose frequency is steered by a script: cutoff(note, glideSamples) glides it to a MIDI
 * note, the other setters set what a script may set besides (resonance, filter type, drift) and the plugin's Mix.
 *
 * The engine has no clock and no random numbers of its own; it plays what it is told. A script target is an absolute
 * MIDI note (69 = 440 Hz, fractional notes are cents) and the glide is linear in semitones (jp::CutoffTrajectory), so
 * a command during a glide continues from where the frequency is and never jumps. Before the first cutoff() the
 * frequency is c_defaultFrequency.
 *
 * The note is the frequency where the resonance peaks (poleMixingTuningForPeak), not the cutoff of the one-pole
 * stages, which lies below it (939 Hz for 1 kHz, 4.3 kHz for 6 kHz at fs = 48 kHz): the steps are heard as the pitch of
 * the resonance, so that is what lands on the note, at any sample rate. The frequency is clamped to [c_minFrequency,
 * c_maxFrequencyRatio * fs]. Without resonance a lowpass therefore sounds somewhat darker than the note suggests.
 *
 * Resonance is a fraction of the self-oscillation threshold at the current frequency (from the same tuning), so 100 %
 * is the edge of self-oscillation at every frequency and for every filter type; the threshold rises from 4 to 9 up to
 * the clamp at fs / 6, which a fixed feedback amount would turn into a resonance that weakens as the frequency goes up.
 * At 85 % the response peak sits 4 to 5.5 % below the frequency, at 95 % 1 to 2 % below.
 *
 * Drift: an Ornstein-Uhlenbeck process, independent of the script's notes, is added to the note in semitones. Depth is
 * the standard deviation of the drift in semitones (0 = off, then the output is that of an engine without drift); the
 * drift is normalised to unit standard deviation, so peaks reach about three times the depth. Rate is its corner
 * frequency in Hz: the process has a fixed sigma (theta 3, a corner of theta / (2 pi) = 0.477 Hz per time unit) and
 * runs on a grid of c_driftGrid samples with dt = rate / 0.477 / (fs / c_driftGrid), linearly interpolated between grid
 * points. The grid counts samples since reset(), so the drift does not depend on block sizes; it moves whether or not a
 * clock runs, and stands still (costing nothing) while the depth is 0.
 *
 * Mix, resonance and drift depth are smoothed per sample with one-pole smoothers (c_smoothingMs); mix uses the
 * constant-power law. The tuning is recomputed (one exp2, four trig calls) only on samples where the note or a smoothed
 * parameter moves. Output for a given seed and parameter sequence does not depend on how the audio is split into
 * blocks. Real-time safe after construction.
 *
 * Setters take the values in the units of the script commands (percent, Hz, semitones, menu index).
 */
class ShipAhoyEngine
{
  public:
    /// Filter type menu (port values 0..14), each entry mapped by name to a pole-mixing preset.
    static constexpr size_t c_filterTypeCount{15};
    static constexpr std::array<const char*, c_filterTypeCount> c_filterTypeLabels{
        "lp 6 dB",  "lp 12 dB",      "lp 18 dB",      "lp 24 dB",      "bp 6 dB",
        "bp 12 dB", "hp 6 dB",       "hp 12 dB",      "hp 18 dB",      "hp 24 dB",
        "notch",    "notch octaves", "2 pole phaser", "3 pole phaser", "4 pole phaser"};
    static constexpr std::array<size_t, c_filterTypeCount> c_filterTypePresets{
        findPoleMixingPreset("6 lp(inv)"),       findPoleMixingPreset("2 pole lp"),
        findPoleMixingPreset("3 pole lp(inv)"),  findPoleMixingPreset("4 pole lp"),
        findPoleMixingPreset("2 pole bp"),       findPoleMixingPreset("4 pole bp"),
        findPoleMixingPreset("1 pole hp"),       findPoleMixingPreset("2 pole hp"),
        findPoleMixingPreset("3 pole hp"),       findPoleMixingPreset("* 4 pole hp"),
        findPoleMixingPreset("2 pole notch"),    findPoleMixingPreset("* Double Notch Octaves"),
        findPoleMixingPreset("* 2 pole phaser"), findPoleMixingPreset("* 3 pole phaser"),
        findPoleMixingPreset("* 4 pole phaser")};

    static constexpr float c_smoothingMs{20.0f};
    static constexpr float c_minFrequency{10.0f};
    static constexpr float c_maxFrequencyRatio{1.0f / 6.0f};
    /// Drift: OU with sigma 0.1 (theta 3), stepped every c_driftGrid samples.
    static constexpr float c_driftSigma{0.1f};
    static constexpr float c_driftTheta{c_driftSigma * 20.0f + 1.0f};
    static constexpr size_t c_driftGrid{16};
    static constexpr float c_minModRate{0.05f};
    static constexpr float c_maxModRate{20.0f};
    static constexpr float c_maxModDepth{12.0f};

    /// Defaults of what a script may set: it gets these until it calls the command, and a new script starts with them.
    static constexpr float c_defaultMix{100.0f};
    static constexpr float c_defaultResonance{85.0f};
    static constexpr float c_defaultFilterType{1.0f};
    static constexpr float c_defaultModDepth{0.0f};
    static constexpr float c_defaultModRate{1.0f};
    static constexpr float c_defaultFrequency{1000.0f};

    explicit ShipAhoyEngine(const uint64_t seedValue = Random::c_defaultSeed) noexcept
        : m_drift(1.0f)
    {
        m_drift.setSigma(c_driftSigma);
        seed(seedValue);
        for (auto* smoother : {&m_dry, &m_wet, &m_resonance, &m_modDepth})
        {
            smoother->setTimeMs(c_smoothingMs);
        }
        setSampleRate(48000.0f);
        setMix(c_defaultMix);
        setScriptDefaults();
        reset();
    }

    void setSampleRate(const float sampleRate) noexcept
    {
        m_sampleRate = sampleRate;
        m_filter.setSampleRate(sampleRate);
        for (auto* smoother : {&m_dry, &m_wet, &m_resonance, &m_modDepth})
        {
            smoother->setSampleRate(sampleRate);
        }
        updateDriftRate();
        m_parametersMoved = true;
    }

    /// Restarts the drift from a seed.
    void seed(const uint64_t seedValue) noexcept
    {
        m_driftSeed = seedValue + 2u;
        m_drift.seed(m_driftSeed);
    }

    /// Mix in percent, 0 (dry only) to 100 (filter only), constant power (the law of constantPowerMix() with
    /// mix = 2 percent / 100 - 1, so 50 % has equal dry and wet gains).
    void setMix(const float percent) noexcept
    {
        const DryWet gains = constantPowerMix(std::clamp(percent, 0.0f, 100.0f) / 50.0f - 1.0f);
        m_dry.setTarget(gains.dry);
        m_wet.setTarget(gains.wet);
    }

    /// Resonance in percent of the self-oscillation threshold (0..100).
    void setResonance(const float percent) noexcept
    {
        m_resonance.setTarget(std::clamp(percent, 0.0f, 100.0f) / 100.0f);
    }

    /// Drift depth in semitones, the standard deviation of the drift (0 = off); smoothed.
    void setModDepth(const float semitones) noexcept
    {
        m_modDepth.setTarget(std::clamp(semitones, 0.0f, c_maxModDepth));
    }

    /// Drift rate in Hz, its corner frequency.
    void setModRate(const float hz) noexcept
    {
        m_modRateHz = std::clamp(hz, c_minModRate, c_maxModRate);
        updateDriftRate();
    }

    /// Filter type as menu index (c_filterTypeLabels); rounded and clamped.
    void setFilterType(const float index) noexcept
    {
        m_filterType = menuIndex(index, c_filterTypeCount);
        m_filter.setPreset(c_filterTypePresets[m_filterType]);
        m_parametersMoved = true;
    }

    /// Resonance, filter type and drift to what a script gets before it sets them (smoothed, not snapped). The
    /// frequency and the mix stay.
    void setScriptDefaults() noexcept
    {
        setResonance(c_defaultResonance);
        setFilterType(c_defaultFilterType);
        setModDepth(c_defaultModDepth);
        setModRate(c_defaultModRate);
    }

    /// Clears the filter, snaps all smoothed parameters to their targets, puts the frequency at c_defaultFrequency and
    /// restarts the drift.
    void reset() noexcept
    {
        m_filter.reset();
        for (auto* smoother : {&m_dry, &m_wet, &m_resonance, &m_modDepth})
        {
            smoother->snapTo(smoother->getTarget());
        }
        m_drift.seed(m_driftSeed);
        m_drift.setValue(m_drift.getMu());
        m_driftValue = 0.0f;
        m_driftTo = 0.0f;
        m_driftSlope = 0.0f;
        m_driftPhase = 0;
        m_modSemitones = 0.0f;
        m_note.setValue(noteOfFrequency(c_defaultFrequency));
        updateFilter(m_resonance.getTarget());
        m_parametersMoved = true;
    }

    /// The frequency glides to the MIDI note in glideSamples samples (0 or fewer: at once), linear in semitones.
    void cutoff(const float note, const int64_t glideSamples) noexcept
    {
        m_note.retarget(note, glideSamples);
        m_parametersMoved = true;
    }

    /// The drift added to the frequency at present, in semitones.
    [[nodiscard]] float getModulation() const noexcept
    {
        return m_modSemitones;
    }

    /// The base note of the script, in semitones (MIDI note numbers).
    [[nodiscard]] float getNote() const noexcept
    {
        return m_note.value();
    }

    void process(const float* in, float* out, const size_t numSamples) noexcept
    {
        for (size_t i = 0; i < numSamples; ++i)
        {
            out[i] = processSample(in[i]);
        }
    }

    /// Frequency the resonance peak is tuned to, in Hz.
    [[nodiscard]] float getPeakFrequency() const noexcept
    {
        return m_peakFrequency;
    }

    /// Pole of the filter's one-pole stages.
    [[nodiscard]] float getFilterPole() const noexcept
    {
        return m_filter.getPole();
    }

    [[nodiscard]] size_t getFilterPresetIndex() const noexcept
    {
        return m_filter.getPresetIndex();
    }

    /// Resonance r passed to the filter (loop gain r * a = fraction * threshold).
    [[nodiscard]] float getFilterResonance() const noexcept
    {
        return m_filterResonance;
    }

  private:
    [[nodiscard]] static size_t menuIndex(const float value, const size_t count) noexcept
    {
        const float clamped = std::clamp(value, 0.0f, static_cast<float>(count - 1));
        return static_cast<size_t>(std::lround(clamped));
    }

    [[nodiscard]] static float noteOfFrequency(const float hz) noexcept
    {
        return 69.0f + 12.0f * std::log2(hz / 440.0f);
    }

    [[nodiscard]] float processSample(const float in) noexcept
    {
        bool moved = m_parametersMoved;
        m_parametersMoved = false;
        if (m_note.isGliding())
        {
            m_note.next();
            moved = true;
        }
        moved |= !m_resonance.isSettled();
        advanceDrift();
        moved |= m_modSemitones != 0.0f || !m_modDepth.isSettled();
        if (moved)
        {
            updateFilter(m_resonance.next());
        }
        const float wet = m_filter.process(in);
        return m_dry.next() * in + m_wet.next() * wet;
    }

    /// One sample of the drift: the grid advances and the value is interpolated; the depth scales it. With the depth
    /// at 0 (settled) nothing runs: the drift stands still and the filter is not retuned for it.
    void advanceDrift() noexcept
    {
        const bool depthSettled = m_modDepth.isSettled();
        if (depthSettled && m_modDepth.getTarget() == 0.0f)
        {
            m_modSemitones = 0.0f;
            return;
        }
        if (m_driftPhase == c_driftGrid)
        {
            m_driftPhase = 0;
            m_driftValue = m_driftTo;
            m_driftTo = (m_drift.step() - m_drift.getMu()) / m_drift.getStationaryDeviation();
            m_driftSlope = (m_driftTo - m_driftValue) * (1.0f / static_cast<float>(c_driftGrid));
        }
        ++m_driftPhase;
        m_driftValue += m_driftSlope;
        const float depth = depthSettled ? m_modDepth.getTarget() : m_modDepth.next();
        m_modSemitones = depth * m_driftValue;
    }

    void updateDriftRate() noexcept
    {
        constexpr float cornerPerTimeUnit = c_driftTheta / (2.0f * 3.14159265358979f);
        const float timeUnitsPerSecond = m_modRateHz / cornerPerTimeUnit;
        m_drift.setStepRate(m_sampleRate / static_cast<float>(c_driftGrid) / timeUnitsPerSecond);
    }

    void updateFilter(const float resonanceFraction) noexcept
    {
        const float frequency = noteToFrequency(m_note.value() + m_modSemitones);
        const float maxFrequency = c_maxFrequencyRatio * m_sampleRate;
        m_peakFrequency = std::clamp(frequency, c_minFrequency, maxFrequency);
        const PoleMixingTuning tuning = poleMixingTuningForPeak(m_peakFrequency, m_sampleRate);
        m_filter.setPole(tuning.pole);
        const float threshold = tuning.oscillationThreshold;
        const float feedback = c_poleMixingPresets[c_filterTypePresets[m_filterType]].resonanceFeedback;
        m_filterResonance = resonanceFraction * threshold / feedback;
        m_filter.setResonance(m_filterResonance);
    }

    float m_sampleRate{48000.0f};
    size_t m_filterType{1};

    OnePoleSmoother m_dry;
    OnePoleSmoother m_wet;
    OnePoleSmoother m_resonance;
    OnePoleSmoother m_modDepth;

    OrnsteinUhlenbeck m_drift;
    uint64_t m_driftSeed{0};
    float m_modRateHz{c_defaultModRate};
    float m_driftValue{0.0f};
    float m_driftTo{0.0f};
    float m_driftSlope{0.0f};
    size_t m_driftPhase{0};
    float m_modSemitones{0.0f};
    PoleMixingFilter m_filter;

    CutoffTrajectory m_note; ///< MIDI note
    bool m_parametersMoved{true};
    float m_peakFrequency{c_defaultFrequency};
    float m_filterResonance{0.0f};
};

namespace detail
{
constexpr bool allShipAhoyFilterTypesFound() noexcept
{
    for (const size_t index : ShipAhoyEngine::c_filterTypePresets)
    {
        if (index == c_poleMixingPresetNotFound)
        {
            return false;
        }
    }
    return true;
}
}
static_assert(detail::allShipAhoyFilterTypesFound(), "a filter type names a preset that is not in the table");

}
