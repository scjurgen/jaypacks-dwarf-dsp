#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "dsp/Numbers/Interpolation.h"
#include "dsp/Numbers/Random.h"

namespace jp
{

/**
 * @brief Granular time domain pitch shifter: two crossfaded read heads over one ring buffer.
 *
 * The write head advances one sample per input sample; a read head advances at `advance`
 * (the pitch ratio v), so it drifts against the write head and has to be picked up and put
 * back periodically. Two heads overlap and crossfade so that the reset is not heard. No
 * latency beyond the grain placement (a few samples to a few hundred), no pitch tracking.
 *
 * ## Geometry
 *
 * drift is how fast a head moves against the write head:
 *
 *   forward, v > 1    drift = v - 1    the head catches up
 *   forward, v < 1    drift = 1 - v    the head falls back
 *   reverse           drift = 1 + v    the head runs away
 *
 * A head may drift `window` before it has to be reset, so its life is window / drift: the
 * smaller the shift, the longer a grain stays valid, and at v == 1 it never expires at all.
 * MaxGrainLife (5000 samples) caps that, which also bounds how far the delay wanders.
 *
 * span = drift * life is how far a head actually travels, and is what the placement uses,
 * not window: window is the ceiling on the excursion, not the excursion itself (a half
 * semitone needs about 3 ms of runway, not a window's worth).
 *
 * A head is placed at age MinAge + jitter + (catching up ? span : 0): a catching up head
 * starts a span back and closes on the write head, every other head starts close and falls
 * back by a span. The jitter (0 .. 200 samples, from a seeded Random) always pushes away
 * from the write head, since a catching up head that started closer would end up past it,
 * interpolating across samples that have not been written yet.
 *
 * A grain's life is fadeIn + at least one plain sample + fadeOut, so the fade time is
 * clamped to (life - 1) / 2. Absorbing an over-long fade by shortening the plain part
 * instead would walk the head beyond the runway it was placed with, through the write
 * head, and the material would jump a whole buffer length at full gain. A fade-out is also
 * capped by the outgoing head's remaining runway when it is catching up.
 *
 * ## The handoff
 *
 * The handoff takes over the fade-in head's position, so that position advances before
 * the handoff; otherwise the outgoing head re-reads its last sample and the output holds
 * for two samples at every grain boundary, a crackle at the grain rate at every pitch.
 *
 * ## Measuring it
 *
 * - Never probe at a frequency commensurate with the buffer: 220 Hz fits exactly 22 times
 *   into 4800 samples, so a buffer-length jump lands on the same phase and hides the
 *   defects worth finding. 237 Hz or similar.
 * - A click is concentrated in time, so an FFT smears it over thousands of bins and no
 *   single bin stands out. Sum the energy above a cutoff instead: a stalled handoff
 *   measures about -60 dB above 5 kHz against about -89 dB for a clean one (200 Hz tone).
 * - Separate clicks from warble by bandwidth: the jitter comb and the crossfade modulate
 *   near the carrier; a step discontinuity reaches Nyquist.
 *
 * ## Known and accepted
 *
 * The jitter combs the crossfade: at 200 Hz the period is 240 samples while the jitter
 * spans 0 .. 200, so grains land at near arbitrary phase and only about 10 dB of spurious
 * free range survives at unity. MaxGrainLife makes unity retrigger every life - fade
 * samples where drift is zero (2600 samples, about 18 Hz at 48 kHz, with a fade of 2400).
 * The output pitch carries a residual error that depends on the probe frequency and
 * the retrigger period (each reset is a phase step): +0.9 .. +6.0 cents measured as the
 * spectral centroid for a 237 Hz probe over -12 .. +12 semitones. The grains spread the
 * carrier over about the grain rate with random fine structure, so measure the centroid,
 * not the highest bin.
 *
 * The buffer is allocated in the constructor; step() is real-time safe. Grain sizes are in
 * samples, so the timing scales with the sample rate.
 */
template <size_t MaxSize>
class PitchFadeWindowDelay
{
  public:
    static constexpr size_t MaxInterpolationWidth{4};

    explicit PitchFadeWindowDelay(const uint64_t seed = Random::c_defaultSeed)
        : m_buffer(MaxSize + MaxInterpolationWidth, 0.0f)
        , m_random(seed)
    {
        updateGeometry();
    }

    /// Window: how far a head may drift before it is reset (clamped to leave head room).
    void setSize(const size_t newSize) noexcept
    {
        m_requestedWindow = newSize;
        updateGeometry();
    }

    void setFadeTime(const size_t samples) noexcept
    {
        m_requestedFadeTime = samples;
    }

    void setReverse(const bool reverse) noexcept
    {
        m_reverse = reverse;
    }

    void setPitch(const float semitones) noexcept
    {
        setPitchRatio(std::pow(2.0f, semitones / 12.0f));
    }

    /// Read rate; 0 is ignored.
    void setPitchRatio(const float ratio) noexcept
    {
        if (std::fpclassify(ratio) == FP_ZERO)
        {
            return;
        }
        m_readHeads.advance = ratio;
    }

    void clear() noexcept
    {
        std::fill(m_buffer.begin(), m_buffer.end(), 0.0f);
    }

    [[nodiscard]] float step(const float in) noexcept
    {
        float returnValue = 0.0f;
        auto& heads = m_readHeads;
        if (!heads.fade)
        {
            returnValue = getFractional(heads.fadeInPos);
            advance(heads.fadeInPos);
            if (--heads.plainSteps == 0)
            {
                triggerFade();
            }
        }
        else
        {
            const auto fadeInGain = static_cast<float>(heads.fadeTime - heads.fadeCount) * heads.fadeStep;
            returnValue =
                getFractional(heads.fadeInPos) * fadeInGain + getFractional(heads.fadeOutPos) * (1.0f - fadeInGain);
            advance(heads.fadeInPos);
            advance(heads.fadeOutPos);
            if (--heads.fadeCount == 0)
            {
                heads.fade = false;
                heads.plainSteps = heads.plainAfterFade;
            }
        }

        if (m_head < MaxInterpolationWidth)
        {
            m_buffer[m_head + MaxSize] = in;
        }
        m_buffer[m_head++] = in;
        if (m_head >= MaxSize)
        {
            m_head = 0;
        }
        return returnValue;
    }

    void processBlock(const float* source, float* target, const size_t numSamples) noexcept
    {
        for (size_t i = 0; i < numSamples; ++i)
        {
            target[i] = step(source[i]);
        }
    }

  private:
    struct Grain
    {
        size_t life;
        float span;
    };

    struct ReadHeads
    {
        float fadeInPos{MaxSize * 0.25f};
        float fadeOutPos{MaxSize * 0.75f};
        float advance{1.0f};
        size_t fadeTime{2};
        size_t fadeCount{0};
        size_t plainSteps{1};
        size_t plainAfterFade{1};
        float fadeStep{1.0f};
        bool fade{false};
    };

    void triggerFade() noexcept
    {
        const auto grain = planGrain();
        const auto outgoingAge = wrapPosition(static_cast<float>(m_head) - m_readHeads.fadeInPos);
        m_readHeads.fade = true;
        m_readHeads.fadeOutPos = m_readHeads.fadeInPos;

        auto fadeTimeCeiling = (grain.life - 1) / 2;
        if (!m_reverse && m_readHeads.advance > 1.0f)
        {
            const auto drift = std::max(driftFactor(), c_minDrift);
            const auto safeSamples = (outgoingAge - static_cast<float>(MinAge)) / drift;
            const auto maxOutgoingFadeTime = safeSamples > 0.0f ? static_cast<size_t>(safeSamples) : size_t{0};
            fadeTimeCeiling = std::min(fadeTimeCeiling, maxOutgoingFadeTime);
        }

        m_readHeads.fadeTime = std::max(std::min(m_requestedFadeTime, fadeTimeCeiling), size_t{2});
        m_readHeads.fadeCount = m_readHeads.fadeTime;
        m_readHeads.fadeStep = 1.0f / static_cast<float>(m_readHeads.fadeTime - 1);
        m_readHeads.plainAfterFade = grain.life - m_readHeads.fadeTime * 2;
        m_readHeads.fadeInPos = materialInPosition(grain.span);
    }

    void advance(float& position) const noexcept
    {
        position += m_reverse ? -m_readHeads.advance : m_readHeads.advance;
        while (position < 0.0f)
        {
            position += static_cast<float>(MaxSize);
        }
        while (position >= static_cast<float>(MaxSize))
        {
            position -= static_cast<float>(MaxSize);
        }
    }

    [[nodiscard]] float getFractional(const float position) const noexcept
    {
        // positions are kept in [0, MaxSize), so truncation is floor and no modulo is needed
        const auto idx = static_cast<size_t>(position);
        const float fractional = position - static_cast<float>(idx);
        // hermite43x interpolates between y[1] and y[2], so the window starts one sample early
        const auto base = idx == 0 ? MaxSize - 1 : idx - 1;
        return hermite43x(&m_buffer[base], fractional);
    }

    [[nodiscard]] float driftFactor() const noexcept
    {
        const float v = m_readHeads.advance;
        if (m_reverse)
        {
            return 1.0f + v;
        }
        return v > 1.0f ? v - 1.0f : 1.0f - v;
    }

    [[nodiscard]] Grain planGrain() const noexcept
    {
        const float drift = driftFactor();
        const float lifeLimit = static_cast<float>(m_window) / std::max(drift, c_minDrift);
        const auto life = std::max(static_cast<size_t>(std::min(lifeLimit, static_cast<float>(MaxGrainLife))), MinLife);
        return {life, drift * static_cast<float>(life)};
    }

    void updateGeometry() noexcept
    {
        constexpr size_t headRoom{c_randomVariation + MinAge + MaxInterpolationWidth};
        m_window = std::clamp(m_requestedWindow, MinWindow, MaxSize - headRoom);
    }

    [[nodiscard]] float wrapPosition(float position) const noexcept
    {
        while (position >= static_cast<float>(MaxSize))
        {
            position -= static_cast<float>(MaxSize);
        }
        while (position < 0.0f)
        {
            position += static_cast<float>(MaxSize);
        }
        return position;
    }

    // Where to drop a new head, as an age behind the write head; jitter only ever pushes
    // away from the write head.
    [[nodiscard]] float materialInPosition(const float span) noexcept
    {
        const bool catchingUp = !m_reverse && m_readHeads.advance > 1.0f;
        // top bits: the low bits of xoshiro256** are its weakest
        const auto jitter = static_cast<float>((m_random.next() >> 32) % (c_randomVariation + 1));
        const float age = static_cast<float>(MinAge) + jitter + (catchingUp ? span : 0.0f);
        return std::round(wrapPosition(static_cast<float>(m_head) - age));
    }

    static_assert(MaxSize > 1000, "the buffer must hold the jitter range and a useful window");

    // hermite43x reads one sample either side, so a head any closer would interpolate
    // across samples the write head has not reached yet
    static constexpr size_t MinAge{4};
    static constexpr size_t MinWindow{5};
    // a grain must be fadeIn + at least one plain sample + fadeOut
    static constexpr size_t MinLife{5};
    static constexpr size_t MaxGrainLife{5000};
    static constexpr size_t c_randomVariation{200};
    static constexpr float c_minDrift{1.0f / 1024.0f};

    ReadHeads m_readHeads{};
    std::vector<float> m_buffer;
    Random m_random;
    size_t m_requestedWindow{MaxSize};
    size_t m_requestedFadeTime{MaxSize / 4};
    size_t m_window{MaxSize};
    size_t m_head{0};
    bool m_reverse{false};
};

}
