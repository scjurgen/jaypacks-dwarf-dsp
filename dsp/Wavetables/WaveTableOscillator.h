#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "dsp/Numbers/Random.h"
#include "dsp/Wavetables/WaveTableBank.h"

namespace jp
{

/**
 * @brief Wavetable oscillator for one waveform of a WaveTableBank, with pulse-width modulation by phase
 * distortion and white noise as a waveform.
 *
 * The phase is a double in [0, 1), so the pitch is exact to rounding (worst error 1e-4 cent from 20 Hz to 12 kHz;
 * a float phase would drift by about 0.3 cent at 20 Hz). The output reads, with linear interpolation, the level of
 * the bank's set that is safe for the pitch, chosen from twice the phase increment (headroom in the bank's brief).
 * The level is chosen when the frequency or the waveform is set, not per sample. The frequency is limited to
 * [0, sampleRate / 2]; a negative or NaN value stops the oscillator. The harmonics of saw, square and shark fin
 * from 55 Hz to 11 kHz are free of aliasing: everything that is not a harmonic (interpolation images) stays below
 * -67 dB relative to the total power, and the RMS of a tonal wave at 100 Hz is 0.5 (0.499 for saw and square,
 * whose 135 harmonics lose a little to the band limit).
 *
 * Pulse-width modulation subtracts a copy of the waveform read at the phase plus an offset, out = w(p) - g w(p + o),
 * with g = 1/4 (Soft) or 1 (Strong); o is the pwm value in [0, 1] (a cycle is 1). It works on any waveform: a saw
 * minus its copy at o = 1/2 has only odd harmonics, at o = 1/4 every fourth harmonic is missing. A change of the
 * pwm value moves o linearly over 10 ms, per sample, so the output does not depend on how the calls are cut into
 * blocks. The White waveform is a noise source: uniform noise in [-2, 2] (RMS 1.15, seeded per oscillator) added to
 * its near-silent table.
 *
 * Morph: three waveform slots (setWaveset(), slot 0 is what setWave() selects) and a position in [0, 1] over them,
 * 0 the first, 0.5 the second, 1 the third, crossfaded linearly between two neighbours, each read at the level that
 * is safe for the pitch. Position 0 reads the first slot alone, exactly as an oscillator that never morphs; a
 * position that is a slot reads that slot alone. A change of the position moves linearly in 10 ms, per sample, so a
 * sweep is free of clicks and the output does not depend on how the calls are cut into blocks. A White slot adds its
 * noise in proportion to its weight. Cost: while the position is not 0 (or on its way to it) two tables are read per
 * sample instead of one.
 *
 * The oscillator does not own the tables: the bank must outlive it. Real-time safe: no allocation, no locks.
 */
class WaveTableOscillator
{
  public:
    /// Weight g of the subtracted copy.
    enum class PwmMode
    {
        Off,
        Soft,
        Strong
    };

    static constexpr double c_pwmRampSeconds{0.01};
    static constexpr double c_morphRampSeconds{0.01};
    static constexpr size_t c_slotCount{3};

    WaveTableOscillator(const float sampleRate, const WaveTableBank& bank, const uint64_t seed = Random::c_defaultSeed)
        : m_sampleRate(sampleRate)
        , m_bank(&bank)
        , m_random(seed)
    {
        setWave(BasicWave::Sine);
    }

    /// Selects the waveform (slot 0); one that the bank has not built is ignored.
    void setWave(const BasicWave wave) noexcept
    {
        setWaveset(0, wave);
    }

    /// Selects the waveform of a slot of the morph (0 to 2); one that the bank has not built is ignored. A slot that
    /// was never set reads as slot 0.
    void setWaveset(const size_t slot, const BasicWave wave) noexcept
    {
        if (slot >= c_slotCount)
        {
            return;
        }
        if (const WaveTableSet* set = m_bank->find(wave))
        {
            m_slotSets[slot] = set;
            m_slotWhite[slot] = wave == BasicWave::White;
            if (slot == 0)
            {
                m_set = set;
                m_noise = wave == BasicWave::White;
            }
            updateLevel();
            updateMorph();
        }
    }

    /// The position of the morph over the three slots, clamped to [0, 1], reached linearly in 10 ms.
    void setMorph(const float position) noexcept
    {
        const double target = std::clamp(static_cast<double>(position), 0.0, 1.0);
        const auto steps = static_cast<size_t>(c_morphRampSeconds * static_cast<double>(m_sampleRate));
        m_morphTarget = target;
        if (steps == 0 || target == m_morphPosition)
        {
            m_morphPosition = target;
            m_morphStepsLeft = 0;
            updateMorph();
            return;
        }
        m_morphStepsLeft = steps;
        m_morphStep = (target - m_morphPosition) / static_cast<double>(steps);
    }

    /// Ends a move of the position at once (a new note does not start in the middle of a move made while it was
    /// silent).
    void finishMorph() noexcept
    {
        if (m_morphStepsLeft != 0)
        {
            m_morphStepsLeft = 0;
            m_morphPosition = m_morphTarget;
            updateMorph();
        }
    }

    /// The position in use now: the one that was set, or on its way to it.
    [[nodiscard]] double getMorph() const noexcept
    {
        return m_morphPosition;
    }

    [[nodiscard]] bool hasWave() const noexcept
    {
        return m_set != nullptr;
    }

    void setFrequency(const float frequency) noexcept
    {
        if (frequency == m_frequency)
        {
            return;
        }
        m_frequency = frequency;
        // a negative or NaN frequency stops the oscillator
        m_phaseIncrement =
            frequency > 0.0f ? std::min(static_cast<double>(frequency) / static_cast<double>(m_sampleRate), 0.5) : 0.0;
        updateLevel();
    }

    void setPwmMode(const PwmMode mode) noexcept
    {
        m_pwmMode = mode;
    }

    /// The offset o of the subtracted copy, clamped to [0, 1], reached linearly in 10 ms.
    void setPwm(const float pwm) noexcept
    {
        const double target = std::clamp(static_cast<double>(pwm), 0.0, 1.0);
        const auto steps = static_cast<size_t>(c_pwmRampSeconds * static_cast<double>(m_sampleRate));
        m_pwmTarget = target;
        if (steps == 0 || target == m_pwmOffset)
        {
            m_pwmOffset = target;
            m_pwmStepsLeft = 0;
            return;
        }
        m_pwmStepsLeft = steps;
        m_pwmStep = (target - m_pwmOffset) / static_cast<double>(steps);
    }

    /// The offset o in use now: the pwm value, or on its way to it.
    [[nodiscard]] double getPwm() const noexcept
    {
        return m_pwmOffset;
    }

    void setPhase(const double phase) noexcept
    {
        m_phase = phase - std::floor(phase);
        m_phase = m_phase >= 1.0 ? 0.0 : m_phase; // a tiny negative phase rounds to 1
    }

    [[nodiscard]] double getPhase() const noexcept
    {
        return m_phase;
    }

    void processBlock(float* target, const size_t numSamples) noexcept
    {
        if (m_set == nullptr)
        {
            std::fill_n(target, numSamples, 0.0f);
            return;
        }
        if (m_morphStepsLeft != 0 || (m_morphPosition != 0.0 && m_morphWeight != 0.0 && m_morphWeight != 1.0))
        {
            processMorphing(target, numSamples);
        }
        else if (m_morphPosition == 0.0)
        {
            processSingle(target, numSamples, m_set->tables[m_level].data.data(), m_noise);
        }
        else
        {
            // the position is a slot: that slot alone, as an oscillator that never morphs would read it
            const size_t slot = m_morphFirst + (m_morphWeight == 1.0 ? 1 : 0);
            processSingle(target, numSamples, tableOf(slot),
                          (m_slotSets[slot] != nullptr ? m_slotWhite[slot] : m_noise));
        }
    }

  private:
    /// The block with one table: the oscillator that never morphs, or a morph that sits on one slot.
    void processSingle(float* target, const size_t numSamples, const float* data, const bool noise) noexcept
    {
        const bool pwmOn = m_pwmMode != PwmMode::Off;
        const float pwmGain = m_pwmMode == PwmMode::Soft ? 0.25f : 1.0f;
        for (size_t i = 0; i < numSamples; ++i)
        {
            float value = readTable(data, m_phase);
            if (pwmOn)
            {
                double copy = m_phase + m_pwmOffset;
                copy -= copy >= 1.0 ? 1.0 : 0.0;
                value -= pwmGain * readTable(data, copy);
            }
            if (m_pwmStepsLeft != 0)
            {
                --m_pwmStepsLeft;
                m_pwmOffset = m_pwmStepsLeft == 0 ? m_pwmTarget : m_pwmOffset + m_pwmStep;
            }
            if (noise)
            {
                value += 4.0f * m_random.uniform() - 2.0f;
            }
            target[i] = value;
            m_phase += m_phaseIncrement;
            m_phase -= m_phase >= 1.0 ? 1.0 : 0.0;
        }
    }

    /// The block with the morph away from slot 0 (or moving): two tables per sample.
    void processMorphing(float* target, const size_t numSamples) noexcept
    {
        const bool pwmOn = m_pwmMode != PwmMode::Off;
        const float pwmGain = m_pwmMode == PwmMode::Soft ? 0.25f : 1.0f;
        for (size_t i = 0; i < numSamples; ++i)
        {
            const float* first = tableOf(m_morphFirst);
            const float* second = tableOf(m_morphFirst + 1);
            const auto weight = static_cast<float>(m_morphWeight);
            const float other = 1.0f - weight;
            float value = other * readTable(first, m_phase) + weight * readTable(second, m_phase);
            if (pwmOn)
            {
                double copy = m_phase + m_pwmOffset;
                copy -= copy >= 1.0 ? 1.0 : 0.0;
                value -= pwmGain * (other * readTable(first, copy) + weight * readTable(second, copy));
            }
            if (m_pwmStepsLeft != 0)
            {
                --m_pwmStepsLeft;
                m_pwmOffset = m_pwmStepsLeft == 0 ? m_pwmTarget : m_pwmOffset + m_pwmStep;
            }
            if (m_noiseGain != 0.0f)
            {
                value += m_noiseGain * (4.0f * m_random.uniform() - 2.0f);
            }
            target[i] = value;
            m_phase += m_phaseIncrement;
            m_phase -= m_phase >= 1.0 ? 1.0 : 0.0;
            if (m_morphStepsLeft != 0)
            {
                --m_morphStepsLeft;
                m_morphPosition = m_morphStepsLeft == 0 ? m_morphTarget : m_morphPosition + m_morphStep;
                updateMorph();
            }
        }
    }

    [[nodiscard]] const WaveTableSet* setOf(const size_t slot) const noexcept
    {
        return m_slotSets[slot] != nullptr ? m_slotSets[slot] : m_set;
    }

    [[nodiscard]] const float* tableOf(const size_t slot) const noexcept
    {
        return setOf(slot)->tables[m_slotLevels[slot]].data.data();
    }

    /// The two slots the position lies between, the weight of the second, and the noise that the white slots add.
    void updateMorph() noexcept
    {
        const double scaled = std::clamp(m_morphPosition, 0.0, 1.0) * 2.0;
        m_morphFirst = scaled >= 1.0 ? 1 : 0;
        m_morphWeight = scaled - static_cast<double>(m_morphFirst);
        const float weight = static_cast<float>(m_morphWeight);
        const auto white = [this](const size_t slot)
        { return (m_slotSets[slot] != nullptr ? m_slotWhite[slot] : m_noise) ? 1.0f : 0.0f; };
        m_noiseGain = (1.0f - weight) * white(m_morphFirst) + weight * white(m_morphFirst + 1);
    }

    [[nodiscard]] static float readTable(const float* data, const double phase) noexcept
    {
        const double position = phase * static_cast<double>(c_waveTableSize);
        const auto index = static_cast<size_t>(position);
        const auto fraction = static_cast<float>(position - static_cast<double>(index));
        return data[index] + (data[index + 1] - data[index]) * fraction;
    }

    void updateLevel() noexcept
    {
        if (m_set != nullptr)
        {
            m_level = m_set->levelFor(static_cast<float>(2.0 * m_phaseIncrement));
            for (size_t slot = 0; slot < c_slotCount; ++slot)
            {
                m_slotLevels[slot] = setOf(slot)->levelFor(static_cast<float>(2.0 * m_phaseIncrement));
            }
        }
    }

    float m_sampleRate;
    const WaveTableBank* m_bank;
    const WaveTableSet* m_set{nullptr};
    size_t m_level{0};
    double m_phase{0.0};
    double m_phaseIncrement{0.0};
    float m_frequency{0.0f};
    PwmMode m_pwmMode{PwmMode::Off};
    double m_pwmOffset{0.5};
    double m_pwmTarget{0.5};
    double m_pwmStep{0.0};
    size_t m_pwmStepsLeft{0};
    bool m_noise{false};
    std::array<const WaveTableSet*, c_slotCount> m_slotSets{};
    std::array<size_t, c_slotCount> m_slotLevels{};
    std::array<bool, c_slotCount> m_slotWhite{};
    double m_morphPosition{0.0};
    double m_morphTarget{0.0};
    double m_morphStep{0.0};
    size_t m_morphStepsLeft{0};
    size_t m_morphFirst{0};
    double m_morphWeight{0.0};
    float m_noiseGain{0.0f};
    Random m_random;
};

}
