#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "dsp/Filters/BiquadCascade.h"
#include "dsp/Filters/FenderToneStack.h"
#include "dsp/Filters/TwinCabinet.h"
#include "dsp/Filters/VolumeBright.h"
#include "dsp/NonLinear/AtanDrive.h"
#include "dsp/Parameters/ParameterSmoothing.h"

namespace jp
{

/**
 * @brief A simple Fender Twin Reverb (AB763) for guitar: tone stack, bright cap, atan drive,
 * 2x12 cabinet, level.
 *
 * in -> tone stack (x c_toneStackMakeupDb) -> volume bright -> atan drive -> cabinet
 *    -> level (x c_outputDb) -> out
 *
 * The order is the amp's (the tone stack follows the first triode, the volume pot with its
 * bright cap follows the tone stack). The tone stack is the AB763 circuit with its own losses;
 * the fixed makeup gain lifts its maximum at bass, mid and treble 5 to about 0 dB, so the
 * middle stays 10 to 15 dB below the bass, as on the amp. The gain knob stands for the volume
 * pot: its travel steers the bright cap (strong lift at low gain, none at 10) and sets the
 * drive d = 10^(c_driveDbAtTen / 20 * gain / 10) of y = atan(d x) / atan(d) (d from 1 at 0 to
 * 63 at 10 and, as the knob goes up to eleven (c_maxGain), 95 at 11; the bright cap is gone
 * from 10 on, it follows the volume pot of the amp's 1-to-10 scale):
 * small signals gain d / atan(d), so turning up is louder and more compressed. The cabinet is
 * the Jensen C12K fit with the open-back highpass (dsp/Filters/TwinCabinet.h), unity in the
 * middle band. c_outputDb makes the defaults (gain 3, level 0 dB) about as loud as the dry
 * input (a guitar chord peaking at -12 dBFS: 0.2 dB apart in RMS); turning the gain up is
 * louder, as on the amp (gain 0 to 10: +21 dB in RMS), and from gain 10 the output peaks reach
 * full scale and above, so Level is the trim. Not modelled: the second triode, phase inverter and power
 * amp (no sag, no power-amp clipping), reverb and tremolo.
 *
 * All knobs are smoothed per sample (c_smoothingMs; bright as a crossfade of its lift). The
 * tone stack and the bright cap are redesigned every c_designInterval samples while their
 * knobs move (the tone stack's design factors a cubic; per sample it would cost more than the
 * audio), and their coefficients ramp linearly to each new design over the interval
 * (RampedBiquad: applied at once, a fast treble move clicked). The countdown runs per sample,
 * so the output does not depend on how the host splits the audio into blocks. Real-time safe
 * after construction.
 *
 * Setters take the plugin port values: bass, mid and treble 0 to 10, gain 0 to 11, level in dB,
 * bright 0 or 1.
 */
class JaysTwinPedal
{
  public:
    static constexpr float c_smoothingMs{20.0f};
    static constexpr size_t c_designInterval{16};
    static constexpr float c_toneStackMakeupDb{14.2f};
    static constexpr float c_driveDbAtTen{36.0f};
    static constexpr float c_maxGain{11.0f};
    static constexpr float c_outputDb{-4.0f};

    static constexpr float c_defaultBass{5.0f};
    static constexpr float c_defaultMid{5.0f};
    static constexpr float c_defaultTreble{5.0f};
    static constexpr float c_defaultGain{3.0f};
    static constexpr float c_defaultLevel{0.0f};
    static constexpr float c_defaultBright{0.0f};

    JaysTwinPedal() noexcept
    {
        for (auto* smoother : smoothers())
        {
            smoother->setTimeMs(c_smoothingMs);
        }
        setSampleRate(48000.0f);
        setBass(c_defaultBass);
        setMid(c_defaultMid);
        setTreble(c_defaultTreble);
        setGain(c_defaultGain);
        setLevel(c_defaultLevel);
        setBright(c_defaultBright);
        reset();
    }

    void setSampleRate(const float sampleRate) noexcept
    {
        for (auto* smoother : smoothers())
        {
            smoother->setSampleRate(sampleRate);
        }
        m_toneStack.setSampleRate(sampleRate);
        m_toneStack.setRampSamples(c_designInterval);
        m_bright.setSampleRate(sampleRate);
        m_bright.setRampSamples(c_designInterval);
        designTwinCabinet(m_cabinet, sampleRate);
        m_designedBass = -1.0f;
        m_designedGain = -1.0f;
    }

    void setBass(const float value) noexcept
    {
        m_bass.setTarget(knob(value));
    }

    void setMid(const float value) noexcept
    {
        m_mid.setTarget(knob(value));
    }

    void setTreble(const float value) noexcept
    {
        m_treble.setTarget(knob(value));
    }

    void setGain(const float value) noexcept
    {
        m_gain.setTarget(gainTravel(value));
    }

    void setLevel(const float db) noexcept
    {
        m_level.setTarget(std::pow(10.0f, db / 20.0f));
    }

    void setBright(const float toggle) noexcept
    {
        m_brightAmount.setTarget(toggle >= 0.5f ? 1.0f : 0.0f);
    }

    /// Snaps the smoothed knobs to their targets, designs the filters for them and clears the
    /// filter states.
    void reset() noexcept
    {
        for (auto* smoother : smoothers())
        {
            smoother->snapTo(smoother->getTarget());
        }
        design();
        updateDrive(m_gain.getCurrent());
        m_bright.setAmount(m_brightAmount.getCurrent());
        m_toneStack.reset();
        m_bright.reset();
        m_cabinet.reset();
        m_countdown = c_designInterval;
    }

    void process(const float* in, float* out, const size_t numSamples) noexcept
    {
        for (size_t i = 0; i < numSamples; ++i)
        {
            out[i] = processSample(in[i]);
        }
    }

    /// Drive d of the atan stage for a gain knob value (0 to 11).
    [[nodiscard]] static float driveForGain(const float gain) noexcept
    {
        return driveForTravel(gainTravel(gain));
    }

    [[nodiscard]] const FenderToneStack& getToneStack() const noexcept
    {
        return m_toneStack;
    }

    [[nodiscard]] const VolumeBright& getBright() const noexcept
    {
        return m_bright;
    }

    [[nodiscard]] const AtanDrive& getDrive() const noexcept
    {
        return m_drive;
    }

    [[nodiscard]] const BiquadCascade<c_twinCabinetSections>& getCabinet() const noexcept
    {
        return m_cabinet;
    }

  private:
    /// Gain knob value 0 to 11 as travel 0 to 1.1 (1 at 10).
    [[nodiscard]] static float gainTravel(const float value) noexcept
    {
        return std::clamp(value, 0.0f, c_maxGain) / 10.0f;
    }

    /// Knob value 0 to 10 as travel 0 to 1.
    [[nodiscard]] static float knob(const float value) noexcept
    {
        return std::clamp(value, 0.0f, 10.0f) / 10.0f;
    }

    [[nodiscard]] static float driveForTravel(const float travel) noexcept
    {
        return std::pow(10.0f, c_driveDbAtTen / 20.0f * travel);
    }

    void updateDrive(const float gain) noexcept
    {
        if (gain != m_drivenGain)
        {
            m_drive.setDrive(driveForTravel(gain));
            m_drivenGain = gain;
        }
    }

    [[nodiscard]] std::array<OnePoleSmoother*, 6> smoothers() noexcept
    {
        return {&m_bass, &m_mid, &m_treble, &m_gain, &m_level, &m_brightAmount};
    }

    void design() noexcept
    {
        const float bass = m_bass.getCurrent();
        const float mid = m_mid.getCurrent();
        const float treble = m_treble.getCurrent();
        if (bass != m_designedBass || mid != m_designedMid || treble != m_designedTreble)
        {
            m_toneStack.setControls(static_cast<double>(bass), static_cast<double>(mid), static_cast<double>(treble));
            m_designedBass = bass;
            m_designedMid = mid;
            m_designedTreble = treble;
        }
        const float gain = m_gain.getCurrent();
        if (gain != m_designedGain)
        {
            // the volume pot ends at 10; 10 to 11 only drives harder
            m_bright.setVolume(static_cast<double>(std::min(gain, 1.0f)));
            m_designedGain = gain;
        }
    }

    [[nodiscard]] float processSample(const float in) noexcept
    {
        // advanced per sample, read by design() every c_designInterval samples
        static_cast<void>(m_bass.next());
        static_cast<void>(m_mid.next());
        static_cast<void>(m_treble.next());
        const float gain = m_gain.next();
        if (--m_countdown == 0)
        {
            m_countdown = c_designInterval;
            design();
        }
        updateDrive(gain);
        m_bright.setAmount(m_brightAmount.next());
        const float stacked = m_toneStack.process(in) * m_makeup;
        const float driven = m_drive.process(m_bright.process(stacked));
        return m_cabinet.process(driven) * m_level.next() * m_outputGain;
    }

    const float m_makeup{std::pow(10.0f, c_toneStackMakeupDb / 20.0f)};
    const float m_outputGain{std::pow(10.0f, c_outputDb / 20.0f)};

    OnePoleSmoother m_bass;
    OnePoleSmoother m_mid;
    OnePoleSmoother m_treble;
    OnePoleSmoother m_gain;
    OnePoleSmoother m_level;
    OnePoleSmoother m_brightAmount;

    FenderToneStack m_toneStack;
    VolumeBright m_bright;
    AtanDrive m_drive;
    BiquadCascade<c_twinCabinetSections> m_cabinet;

    size_t m_countdown{c_designInterval};
    float m_designedBass{-1.0f};
    float m_designedMid{-1.0f};
    float m_designedTreble{-1.0f};
    float m_designedGain{-1.0f};
    float m_drivenGain{-1.0f};
};

}
