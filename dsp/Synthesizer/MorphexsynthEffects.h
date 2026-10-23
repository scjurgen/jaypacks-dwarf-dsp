#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "dsp/Delays/ModulationDelay.h"
#include "dsp/Filters/Phaser24.h"
#include "dsp/Generators/SynthLfo.h"
#include "dsp/Numbers/Convert.h"
#include "dsp/Numbers/Random.h"
#include "dsp/Parameters/ParameterSmoothing.h"
#include "dsp/Reverbs/FdnTank.h"

namespace jp
{

/**
 * @brief The master bus of the Morphexsynth, in series on a stereo block: phaser, chorus, reverb, each off at the
 * start (phaser and chorus mix 0, reverb mix -100 dB).
 *
 * Phaser: two Phaser24 per channel in series (eight poles), all four sharing one sweep and one feedback, so the
 * sweep is the same on both channels (a mono-swept phaser, not a wide one). The cutoff runs on a sine between 200
 * and 2000 Hz in log frequency, the depth (0 to 1) taking that share of the range around the geometric centre
 * (632 Hz); it is set once per block, and the filters move their pole in equal steps over that block. The LFO is
 * stepped once per block, with the rate multiplied by the block size. The mix is a linear blend with the dry signal; a
 * new mix is reached by a one-pole of 10 ms (a knob that moves the mix has no steps).
 *
 * Chorus: one ModulationDelay per channel, base delay 18 ms, depth 0 to 1 giving 0.6 of the delay's depth unit
 * (up to 300 samples, 6.25 ms of sweep at 48 kHz). The right channel starts half a sweep period later than the left
 * one (it is run through that many silent samples once, for the rate at construction, 0.6 Hz: a later rate change
 * keeps the offset in time, not in phase). The mix is a linear blend with the dry signal, smoothed like the phaser's.
 *
 * Reverb: the mono sum of the two channels (half of each) into an order-16 FdnTank (AbacDsp: 32), left and right taps
 * back. The size in metres sets the line lengths from size / 2.3 to size * 2.3 (limited to the tank's longest line, 0.5
 * s: 60 m gives 0.41 s), the decay is T60 in ms. Dry and wet are gains in dB, each smoothed by a one-pole of 3 ms
 * (AbacDsp ramps them linearly over 10 ms); the wet and dry parts are summed, so the wet path is added to a dry part
 * that is not reduced by the mix. A size or decay change fades over the tank's fade time (50 ms) instead of AbacDsp's
 * glide of the read heads.
 *
 * The block size is the tank's. No allocation in process(); the constructor allocates the tank and the chorus lines.
 */
template <size_t BlockSize>
class MorphexsynthEffects
{
  public:
    static constexpr size_t c_fdnOrder{16};
    static constexpr float c_fdnSizeSpread{2.3f};
    static constexpr float c_phaserMinHz{200.0f};
    static constexpr float c_phaserMaxHz{2000.0f};
    static constexpr float c_chorusBaseDelayMs{18.0f};
    static constexpr float c_chorusMaxModDepth{0.6f};
    static constexpr float c_defaultChorusRateHz{0.6f};
    static constexpr float c_defaultPhaserRateHz{0.3f};
    static constexpr float c_defaultReverbSizeMeters{12.0f};
    static constexpr float c_defaultReverbDecayMs{1500.0f};
    static constexpr float c_defaultReverbMixDb{-100.0f};
    static constexpr float c_smoothingMs{3.0f};
    static constexpr float c_mixSmoothingMs{10.0f};
    static constexpr size_t c_chorusMaxDelaySamples{4096};

    explicit MorphexsynthEffects(const float sampleRate, const uint64_t seed = Random::c_defaultSeed)
        : m_phaser{{Phaser24(sampleRate), Phaser24(sampleRate), Phaser24(sampleRate), Phaser24(sampleRate)}}
        , m_phaserLfo(sampleRate, seed)
        , m_chorus{{ModulationDelay<c_chorusMaxDelaySamples>(sampleRate),
                    ModulationDelay<c_chorusMaxDelaySamples>(sampleRate)}}
        , m_reverb(sampleRate, 0.5f, seed + 1)
    {
        for (auto* smoother : {&m_phaserMix, &m_chorusMix})
        {
            smoother->setSampleRate(sampleRate);
            smoother->setTimeMs(c_mixSmoothingMs);
            smoother->snapTo(0.0f);
        }
        for (auto& stage : m_phaser)
        {
            stage.setSmoothingSteps(BlockSize);
        }
        m_phaserLfo.setWaveForm(LfoType::Sine);
        for (auto& delay : m_chorus)
        {
            delay.setWidthMs(c_chorusBaseDelayMs);
        }
        setChorus(c_defaultChorusRateHz, 0.5f, 0.0f);
        const auto halfPeriodSamples = static_cast<size_t>(sampleRate / (2.0f * c_defaultChorusRateHz));
        for (size_t i = 0; i < halfPeriodSamples; ++i)
        {
            (void) m_chorus[1].process(0.0f);
        }
        setPhaser(c_defaultPhaserRateHz, 0.5f, 0.0f, 0.0f);
        for (auto* smoother : {&m_reverbDry, &m_reverbMix})
        {
            smoother->setSampleRate(sampleRate);
            smoother->setTimeMs(c_smoothingMs);
        }
        m_reverbDry.snapTo(1.0f);
        setReverbSize(c_defaultReverbSizeMeters);
        setReverbDecay(c_defaultReverbDecayMs);
        setReverbMix(c_defaultReverbMixDb);
        m_reverbMix.snapTo(dbToGain(c_defaultReverbMixDb));
    }

    /// Rate in Hz, depth 0..1 (share of the 200 to 2000 Hz range), feedback (0 none, see Phaser24), mix 0..1.
    void setPhaser(const float rateHz, const float depth, const float feedback, const float mix) noexcept
    {
        m_phaserDepth = depth;
        m_phaserMix.setTarget(mix);
        m_phaserLfo.setFrequency(rateHz * static_cast<float>(BlockSize));
        for (auto& stage : m_phaser)
        {
            stage.setResonance(feedback);
        }
    }

    /// Rate in Hz, depth 0..1, mix 0..1.
    void setChorus(const float rateHz, const float depth, const float mix) noexcept
    {
        m_chorusMix.setTarget(mix);
        for (auto& delay : m_chorus)
        {
            delay.setModSpeed(rateHz);
            delay.setModDepth(depth * c_chorusMaxModDepth);
        }
    }

    void setReverbSize(const float meters) noexcept
    {
        m_reverb.setMinSize(meters / c_fdnSizeSpread);
        m_reverb.setMaxSize(meters * c_fdnSizeSpread);
    }

    void setReverbDecay(const float milliseconds) noexcept
    {
        m_reverb.setDecay(milliseconds);
    }

    void setReverbMix(const float decibels) noexcept
    {
        m_reverbMix.setTarget(dbToGain(decibels));
    }

    void setReverbDry(const float decibels) noexcept
    {
        m_reverbDry.setTarget(dbToGain(decibels));
    }

    /// Clears the phaser, the chorus lines and the tank.
    void reset() noexcept
    {
        for (auto& stage : m_phaser)
        {
            stage.reset();
        }
        for (auto& delay : m_chorus)
        {
            delay.reset();
        }
        m_reverb.reset();
    }

    /// One block of BlockSize samples in place.
    void process(float* left, float* right) noexcept
    {
        processPhaser(left, right);
        processChorus(left, right);
        processReverb(left, right);
    }

    [[nodiscard]] const FdnTank<c_fdnOrder, BlockSize>& getReverb() const noexcept
    {
        return m_reverb;
    }

  private:
    void processPhaser(float* left, float* right) noexcept
    {
        const float lfo = m_phaserLfo.step();
        const float logMin = std::log(c_phaserMinHz);
        const float logMax = std::log(c_phaserMaxHz);
        const float halfRange = 0.5f * (logMax - logMin) * m_phaserDepth;
        const float cutoffHz = std::exp(0.5f * (logMin + logMax) + halfRange * lfo);
        for (auto& stage : m_phaser)
        {
            stage.setCutoff(cutoffHz);
        }
        std::array<float, BlockSize> dryLeft;
        std::array<float, BlockSize> dryRight;
        std::copy_n(left, BlockSize, dryLeft.begin());
        std::copy_n(right, BlockSize, dryRight.begin());
        std::array<float, BlockSize> tmp;
        m_phaser[0].process(left, tmp.data(), BlockSize);
        m_phaser[1].process(tmp.data(), left, BlockSize);
        m_phaser[2].process(right, tmp.data(), BlockSize);
        m_phaser[3].process(tmp.data(), right, BlockSize);
        for (size_t s = 0; s < BlockSize; ++s)
        {
            const float mix = m_phaserMix.next();
            left[s] = dryLeft[s] * (1.0f - mix) + left[s] * mix;
            right[s] = dryRight[s] * (1.0f - mix) + right[s] * mix;
        }
    }

    void processChorus(float* left, float* right) noexcept
    {
        for (size_t s = 0; s < BlockSize; ++s)
        {
            const float wetLeft = m_chorus[0].process(left[s]);
            const float wetRight = m_chorus[1].process(right[s]);
            const float mix = m_chorusMix.next();
            left[s] = left[s] * (1.0f - mix) + wetLeft * mix;
            right[s] = right[s] * (1.0f - mix) + wetRight * mix;
        }
    }

    void processReverb(float* left, float* right) noexcept
    {
        std::array<float, BlockSize> mono;
        for (size_t s = 0; s < BlockSize; ++s)
        {
            mono[s] = 0.5f * (left[s] + right[s]);
        }
        std::array<float, BlockSize> wetLeft;
        std::array<float, BlockSize> wetRight;
        m_reverb.processBlockSplit(mono.data(), wetLeft.data(), wetRight.data());
        for (size_t s = 0; s < BlockSize; ++s)
        {
            const float dry = m_reverbDry.next();
            const float wet = m_reverbMix.next();
            left[s] = left[s] * dry + wetLeft[s] * wet;
            right[s] = right[s] * dry + wetRight[s] * wet;
        }
    }

    std::array<Phaser24, 4> m_phaser;
    LfoGenerators m_phaserLfo;
    float m_phaserDepth{0.5f};
    OnePoleSmoother m_phaserMix;
    std::array<ModulationDelay<c_chorusMaxDelaySamples>, 2> m_chorus;
    OnePoleSmoother m_chorusMix;
    FdnTank<c_fdnOrder, BlockSize> m_reverb;
    OnePoleSmoother m_reverbDry;
    OnePoleSmoother m_reverbMix;
};

}
