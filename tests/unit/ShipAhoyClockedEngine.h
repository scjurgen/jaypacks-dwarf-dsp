#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "dsp/Generators/CutoffTrajectory.h"
#include "dsp/Generators/OrnsteinUhlenbeck.h"
#include "dsp/Numbers/Random.h"
#include "dsp/Parameters/ParameterSmoothing.h"
#include "plugins/shipahoy/ShipAhoyEngine.h"

namespace jp::test
{

/// The sample-and-hold hopper that Ship Ahoy had in its engine before the scripts took over, on top of the engine's
/// cutoff(): a position in [0, 1] glides to a random target on every hop and maps exponentially onto an octave range
/// around a centre frequency (frequency = centre * 2^(range * (position - 0.5))), the centre and the range smoothed
/// like the knobs were. It is the reference of the factory scripts hop.lua (white), toggle-trill.lua (toggle) and
/// slow-drift.lua (brown), whose notes follow the same formulas with the range and centre fixed, and it drives the
/// charts of documentation/ShipAhoy/ and the tests of the algorithms (distributions, glide, range).
///
/// It also has the hop clock the plugin once had inside: a phase accumulator at `speed` hops per second, a hop on the
/// first sample after reset(). The plugin now takes its steps from the tempo clock; the tests of the algorithms drive
/// the hopper like this.
class ClockedEngine : public jp::ShipAhoyEngine
{
  public:
    enum class RandomType
    {
        White,  ///< uniform over the range
        Toggle, ///< alternates between the lower and the upper 40 % of the range
        Violet, ///< product of three uniform values: mostly low, occasionally high
        Brown,  ///< Ornstein-Uhlenbeck walk, one step per hop: small moves around the centre
        Gauss,  ///< Gaussian around the centre, standard deviation 1/6 of the range
    };
    static constexpr size_t c_randomTypeCount{5};

    static constexpr float c_minGlidePercent{1.0f};
    static constexpr float c_toggleSpread{0.4f};
    static constexpr float c_gaussDeviation{1.0f / 6.0f};
    /// Brown: OU with sigma 0.1 (theta 3) stepped at dt = 1/30 per hop, so consecutive hops correlate with
    /// e^(-0.1) = 0.905; scaled to a standard deviation of 0.2 of the range.
    static constexpr float c_brownSigma{0.1f};
    static constexpr float c_brownStepsPerHop{30.0f};
    static constexpr float c_brownDeviation{0.2f};
    static constexpr float c_defaultGlide{5.0f};
    static constexpr float c_defaultOctaveRange{3.0f};

    explicit ClockedEngine(const uint64_t seedValue = Random::c_defaultSeed)
        : ShipAhoyEngine(seedValue)
        , m_brown(c_brownStepsPerHop)
    {
        m_brown.setSigma(c_brownSigma);
        for (auto* smoother : {&m_centreOctaves, &m_octaveRange})
        {
            smoother->setTimeMs(c_smoothingMs);
        }
        seed(seedValue);
        setGlideUp(c_defaultGlide);
        setGlideDown(c_defaultGlide);
        setFrequency(c_defaultFrequency);
        setOctaveRange(c_defaultOctaveRange);
        reset();
    }

    void setSampleRate(const float sampleRate)
    {
        ShipAhoyEngine::setSampleRate(sampleRate);
        for (auto* smoother : {&m_centreOctaves, &m_octaveRange})
        {
            smoother->setSampleRate(sampleRate);
        }
        m_rate = sampleRate;
    }

    /// Restarts the random sequences from a seed (the OU walk uses seed + 1, the drift seed + 2).
    void seed(const uint64_t seedValue) noexcept
    {
        ShipAhoyEngine::seed(seedValue);
        m_random.seed(seedValue);
        m_brown.seed(seedValue + 1u);
    }

    void setSpeed(const float hz)
    {
        m_speed = std::max(hz, 0.01f);
    }

    /// Glide time of upward hops in percent of the hop period (1..100).
    void setGlideUp(const float percent) noexcept
    {
        m_glideUp = std::clamp(percent, c_minGlidePercent, 100.0f) / 100.0f;
    }

    /// Glide time of downward hops in percent of the hop period (1..100).
    void setGlideDown(const float percent) noexcept
    {
        m_glideDown = std::clamp(percent, c_minGlidePercent, 100.0f) / 100.0f;
    }

    /// RandomType as menu index; rounded and clamped.
    void setRandomType(const float index) noexcept
    {
        const float clamped = std::clamp(index, 0.0f, static_cast<float>(c_randomTypeCount - 1));
        m_randomType = static_cast<RandomType>(static_cast<size_t>(std::lround(clamped)));
    }

    /// Centre of the range in Hz.
    void setFrequency(const float hz) noexcept
    {
        m_centreOctaves.setTarget(std::log2(std::max(hz, c_minFrequency)));
    }

    /// Width of the range in octaves, half below and half above the centre; 0 holds the frequency at the centre.
    void setOctaveRange(const float octaves) noexcept
    {
        m_octaveRange.setTarget(std::max(octaves, 0.0f));
    }

    void reset()
    {
        ShipAhoyEngine::reset();
        for (auto* smoother : {&m_centreOctaves, &m_octaveRange})
        {
            smoother->snapTo(smoother->getTarget());
        }
        m_brown.setValue(m_brown.getMu());
        m_trajectory.setValue(0.5f);
        m_toggleHigh = false;
        m_hopCount = 0;
        m_phase = 1.0;
        place();
    }

    /// A step of the clock: the position glides to a new random target; the glide lasts the set fraction of
    /// periodSamples (the length of the step).
    void hop(const double periodSamples) noexcept
    {
        ++m_hopCount;
        const float target = nextTarget();
        const float glide = target > m_trajectory.value() ? m_glideUp : m_glideDown;
        m_trajectory.retarget(target, std::max<int64_t>(1, std::llround(static_cast<double>(glide) * periodSamples)));
    }

    void process(const float* in, float* out, const size_t numSamples)
    {
        for (size_t i = 0; i < numSamples; ++i)
        {
            m_phase += static_cast<double>(m_speed) / static_cast<double>(m_rate);
            if (m_phase >= 1.0)
            {
                m_phase = std::fmod(m_phase, 1.0);
                hop(static_cast<double>(m_rate) / static_cast<double>(m_speed));
            }
            if (m_trajectory.isGliding())
            {
                m_trajectory.next();
                place();
            }
            else if (!m_centreOctaves.isSettled() || !m_octaveRange.isSettled())
            {
                place();
            }
            ShipAhoyEngine::process(in + i, out + i, 1);
        }
    }

    /// Position of the frequency in the range, [0, 1].
    [[nodiscard]] float getPosition() const noexcept
    {
        return m_trajectory.value();
    }

    /// Target position of the current hop, [0, 1].
    [[nodiscard]] float getTarget() const noexcept
    {
        return m_trajectory.target();
    }

    /// Hops since construction or reset().
    [[nodiscard]] uint64_t getHopCount() const noexcept
    {
        return m_hopCount;
    }

  private:
    /// The engine's note for the present position, centre and range.
    void place() noexcept
    {
        const float octaves = m_centreOctaves.next() + m_octaveRange.next() * (m_trajectory.value() - 0.5f);
        cutoff(69.0f + 12.0f * (octaves - std::log2(440.0f)), 0);
    }

    [[nodiscard]] float nextTarget() noexcept
    {
        switch (m_randomType)
        {
            case RandomType::White:
                return m_random.uniform();
            case RandomType::Toggle:
            {
                m_toggleHigh = !m_toggleHigh;
                const float offset = m_random.uniform() * c_toggleSpread;
                return m_toggleHigh ? 1.0f - offset : offset;
            }
            case RandomType::Violet:
                return m_random.uniform() * m_random.uniform() * m_random.uniform();
            case RandomType::Brown:
            {
                const float scale = c_brownDeviation / m_brown.getStationaryDeviation();
                return std::clamp(0.5f + (m_brown.step() - m_brown.getMu()) * scale, 0.0f, 1.0f);
            }
            case RandomType::Gauss:
                return std::clamp(0.5f + m_random.gaussian() * c_gaussDeviation, 0.0f, 1.0f);
        }
        return 0.5f;
    }

    float m_rate{48000.0f};
    float m_speed{7.0f};
    double m_phase{1.0};
    float m_glideUp{0.05f};
    float m_glideDown{0.05f};
    RandomType m_randomType{RandomType::White};
    OnePoleSmoother m_centreOctaves;
    OnePoleSmoother m_octaveRange;
    Random m_random;
    OrnsteinUhlenbeck m_brown;
    CutoffTrajectory m_trajectory; ///< position in [0, 1]
    uint64_t m_hopCount{0};
    bool m_toggleHigh{false};
};

}
