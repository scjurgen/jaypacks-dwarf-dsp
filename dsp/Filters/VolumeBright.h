#pragma once

#include <algorithm>
#include <complex>

#include "dsp/Filters/BilinearTransform.h"
#include "dsp/Filters/Biquad.h"
#include "dsp/Filters/FenderToneStack.h"
#include "dsp/Filters/RampedBiquad.h"

namespace jp
{

/// The volume pot with its bright cap and the resistance of the source that drives it.
/// Fender AB763 Twin Reverb: 1M log pot, 120 pF across its upper part; the source is the tone
/// stack, whose output impedance is about 25k with bass, mid and treble at 5 (most of it the
/// lower part of the treble pot), taken as a fixed resistance.
struct BrightNetwork
{
    double potOhms{1e6};
    double capFarad{120e-12};
    double sourceOhms{25e3};
};

/**
 * @brief Treble lift of a bright cap across the upper part of a volume pot.
 *
 * With the pot at fraction v (wiper to ground v R, wiper to top (1 - v) R = Ru), the source
 * resistance Rs and the cap C across Ru, the divider is
 * H(s) = Rl / (Rs + Ru + Rl) (1 + s Ru C) / (1 + s Ru C (Rs + Rl) / (Rs + Ru + Rl)).
 * This block applies H normalised to its DC gain (the volume itself is not applied, only the
 * lift): a first-order shelf with its zero at 1 / (2 pi Ru C) and a high-frequency lift of
 * (Rs + R) / (Rs + Rl), large at low volume and vanishing at full volume, as on the amp. The
 * lift is computed as x + amount (H - 1) x, with H - 1 a first-order highpass, so amount 0 is
 * an exact pass-through and amount can be crossfaded to switch the bright cap without a click.
 * setVolume() takes the knob travel and applies audioTaper(); the fraction is limited to 0.99
 * (at 1 the highpass pole would sit at z = -1). Design at control rate, with setRampSamples()
 * spreading a new design over the samples up to the next (RampedBiquad); setSampleRate() and
 * reset() apply the current design at once. Real-time safe.
 */
class VolumeBright
{
  public:
    void setSampleRate(const float sampleRate) noexcept
    {
        m_sampleRate = static_cast<double>(sampleRate);
        design();
        m_lift.snapToTarget();
    }

    /// Samples over which a new design is reached (0: at once, the default).
    void setRampSamples(const size_t samples) noexcept
    {
        m_lift.setRampSamples(samples);
    }

    void setNetwork(const BrightNetwork& network) noexcept
    {
        m_network = network;
        design();
    }

    void setVolume(const double position) noexcept
    {
        setVolumeFraction(audioTaper(position));
    }

    void setVolumeFraction(const double fraction) noexcept
    {
        m_fraction = std::clamp(fraction, 0.0, 0.99);
        design();
    }

    /// 0: off (exact pass-through), 1: bright cap fully in.
    void setAmount(const float amount) noexcept
    {
        m_amount = amount;
    }

    [[nodiscard]] float process(const float in) noexcept
    {
        return in + m_amount * m_lift.process(in);
    }

    /// Clears the filter state and ends a running ramp.
    void reset() noexcept
    {
        m_lift.reset();
        m_lift.snapToTarget();
    }

    /// Lift at high frequencies (above the pole) as a ratio, (Rs + R) / (Rs + Rl).
    [[nodiscard]] double getHighFrequencyLift() const noexcept
    {
        const double rl = m_fraction * m_network.potOhms;
        return (m_network.sourceOhms + m_network.potOhms) / (m_network.sourceOhms + rl);
    }

    /// Analog H(s) normalised to DC, for analysis and tests.
    [[nodiscard]] AnalogSection getAnalog() const noexcept
    {
        const double ru = (1.0 - m_fraction) * m_network.potOhms;
        const double rl = m_fraction * m_network.potOhms;
        const double tauZero = ru * m_network.capFarad;
        const double tauPole = tauZero * (m_network.sourceOhms + rl) / (m_network.sourceOhms + ru + rl);
        return AnalogSection{1.0, tauZero, 0.0, 1.0, tauPole, 0.0};
    }

    /// Digital response of the design with the current amount, for analysis and tests.
    [[nodiscard]] std::complex<double> response(const double frequencyHz) const
    {
        return 1.0 +
               static_cast<double>(m_amount) * biquadSectionResponse(m_lift.getTarget(), frequencyHz, m_sampleRate);
    }

  private:
    void design() noexcept
    {
        const AnalogSection h = getAnalog();
        // H - 1 = s (tauZero - tauPole) / (1 + s tauPole)
        m_lift.setTarget(bilinearSection(AnalogSection{0.0, h.b1 - h.a1, 0.0, 1.0, h.a1, 0.0}, 2.0 * m_sampleRate));
    }

    double m_sampleRate{48000.0};
    BrightNetwork m_network{};
    double m_fraction{0.99};
    float m_amount{0.0f};
    RampedBiquad m_lift;
};

}
