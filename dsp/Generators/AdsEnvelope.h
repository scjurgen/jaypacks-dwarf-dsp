#pragma once

#include <array>
#include <cassert>

namespace jp
{

/**
 * @brief Polynomial ease curve blended against linear, shaping one envelope segment on x in [0, 1].
 *
 * Factor 0 is a straight line; positive values bow the curve up (fast start, slow end: 0.71 at x = 0.5 for 1),
 * negative values bow it down (slow start: 0.38 at x = 0.5 for -1; the factor is divided by 1.75, so the bow is
 * gentler). The curve is
 * x + f (a x^5 + b x^3 + c x - x) with fixed a, b, c, so it always runs from 0 at x = 0 to 1 at x = 1; factors outside
 * -1 to 1 overshoot on the way.
 */
class CurvedValue
{
  public:
    CurvedValue() noexcept
    {
        setFactor(0.0f);
    }

    void setFactor(const float factor) noexcept
    {
        m_f = factor < 0.0f ? curveCoefficients(factor / 1.75f) : curveCoefficients(factor);
    }

    [[nodiscard]] float get(const float x) const noexcept
    {
        const float squareX = x * x;
        const float quartX = squareX * squareX;
        return m_f[0] * quartX * x + m_f[1] * squareX * x + m_f[2] * x + x - m_f[3] * x;
    }

  private:
    [[nodiscard]] static constexpr std::array<float, 4> curveCoefficients(const float a) noexcept
    {
        return {a * 0.0706985022721f, a * -0.640510676908f, a * 1.56981217135f, a};
    }

    std::array<float, 4> m_f{};
};

/**
 * @brief One ramp segment with a curve: from the gain it starts at to a target over N steps, then holding at the
 * target for ever.
 *
 * A segment starts from the last gain it produced (setNewFramesAndTarget), so chained segments and retriggers
 * are continuous. After the step count has run out step() returns the target and isDone() is true, so a caller
 * never needs to special-case "finished" before reading the gain. A segment of 0 steps jumps to the target.
 * The unit of a step is whatever the caller steps at.
 */
class EnvelopeShaper
{
  public:
    EnvelopeShaper() noexcept = default;

    void reset(const float startGain) noexcept
    {
        m_startGain = startGain;
        m_endGain = startGain;
        m_lastGain = startGain;
        m_numOfSamples = 0;
        m_holdValues = true;
    }

    void setHold(const size_t numOfSamples, const float value) noexcept
    {
        m_numOfSamples = numOfSamples;
        m_holdValues = true;
        m_startGain = value;
        m_endGain = value;
    }

    void setNewFramesAndTarget(const size_t numOfSamples, const float endGain, const float curve) noexcept
    {
        m_startGain = m_lastGain;
        m_curve.setFactor(curve);
        m_numOfSamples = numOfSamples;
        m_endGain = endGain;
        if (m_numOfSamples == 0)
        {
            m_startGain = endGain;
            m_gainDelta = 0.0f;
            m_x = 0.0f;
            m_dx = 0.0f;
            m_holdValues = true;
            return;
        }
        m_x = 0.0f;
        m_dx = 1.0f / static_cast<float>(m_numOfSamples);
        m_gainDelta = m_endGain - m_startGain;
        m_holdValues = false;
    }

    /// Retargets the running segment: keeps the remaining step count and restarts the curve from the current gain
    /// toward a new endGain, so a live change finishes on schedule instead of restarting the ramp.
    void modifyTarget(const float endGain) noexcept
    {
        m_startGain = m_lastGain;
        m_endGain = endGain;
        if (m_numOfSamples == 0)
        {
            m_startGain = endGain;
            m_gainDelta = 0.0f;
            m_x = 0.0f;
            m_dx = 0.0f;
            return;
        }
        m_x = 0.0f;
        m_dx = 1.0f / static_cast<float>(m_numOfSamples);
        m_gainDelta = m_endGain - m_startGain;
        m_holdValues = m_dx == 0.0f;
    }

    [[nodiscard]] float step() noexcept
    {
        if (m_holdValues)
        {
            if (m_numOfSamples > 0)
            {
                --m_numOfSamples;
            }
            else
            {
                m_lastGain = m_startGain = m_endGain;
                m_gainDelta = 0.0f;
            }
            return m_endGain;
        }
        if (m_numOfSamples > 0)
        {
            --m_numOfSamples;
            m_x += m_dx;
        }
        else
        {
            m_startGain = m_endGain;
            m_gainDelta = 0.0f;
            m_holdValues = true;
        }
        m_lastGain = m_startGain + m_curve.get(m_x) * m_gainDelta;
        return m_lastGain;
    }

    [[nodiscard]] bool isDone() const noexcept
    {
        return m_numOfSamples == 0;
    }

    [[nodiscard]] float currentGain() const noexcept
    {
        return m_lastGain;
    }

  private:
    size_t m_numOfSamples{0};
    float m_startGain{0.0f};
    float m_gainDelta{0.0f};
    float m_endGain{0.0f};
    float m_lastGain{0.0f};
    bool m_holdValues{true};
    float m_dx{0.0f};
    float m_x{0.0f};
    CurvedValue m_curve;
};

/**
 * @brief Multi-segment envelope: a chain of SegmentCount EnvelopeShaper segments, one of which may be the sustain
 * point.
 *
 * Each segment has a time in milliseconds, a target gain and a curve; trigger() starts the first from the
 * current gain, and every segment starts where the one before ended. Reaching the sustain segment holds there
 * until release() starts the segment after it; with no sustain point (noSustain()), in one-shot mode, or with the
 * sustain point last, the chain runs through and stays at the last target. markSustainAt() copies the target of
 * the segment before into the sustain segment, so the sustain segment is a zero-time hold after the decay and
 * must not be the decay segment itself (it would overwrite its target). The sustain segment keeps the target it
 * copied: a later change of the decay target needs setSegmentHoldPreviousValue() and markSustainAt() again, or the
 * envelope jumps to the old sustain level when the decay ends. Times are converted to steps at the
 * rate given to the constructor and truncated. Real-time safe.
 */
template <size_t SegmentCount>
class Envelope
{
  public:
    struct SegmentValues
    {
        size_t frames{0};
        float targetGain{0.0f};
        float curve{0.0f};
    };

    explicit Envelope(const float sampleRate) noexcept
        : m_samplesPerMillisecond(sampleRate / 1000.0f)
    {
    }

    void setOneshot(const bool oneshot) noexcept
    {
        m_oneshot = oneshot;
    }

    void setSegment(const size_t index, const float timeInMilliseconds, const float targetGain,
                    const float curve = 0.0f) noexcept
    {
        assert(index < SegmentCount);
        m_segments[index].frames = static_cast<size_t>(timeInMilliseconds * m_samplesPerMillisecond);
        m_segments[index].targetGain = targetGain;
        m_segments[index].curve = curve;
    }

    void setSegmentHoldPreviousValue(const size_t index, const float timeInMilliseconds) noexcept
    {
        assert(index > 0 && index < SegmentCount);
        m_segments[index].frames = static_cast<size_t>(timeInMilliseconds * m_samplesPerMillisecond);
        m_segments[index].targetGain = m_segments[index - 1].targetGain;
        m_segments[index].curve = 0.0f;
    }

    void markSustainAt(const size_t index) noexcept
    {
        assert(index < SegmentCount);
        m_sustainIndex = index;
        if (index > 0)
        {
            m_segments[index].targetGain = m_segments[index - 1].targetGain;
        }
    }

    void noSustain() noexcept
    {
        m_sustainIndex = SegmentCount;
    }

    /// If the segment is the one playing, retargets it (keeping its remaining time) to the target gain it is now
    /// configured with. Nothing for any other segment: a live change reaches only a segment in flight.
    void modifyTargetIfActive(const size_t index) noexcept
    {
        if (m_currentIndex == index)
        {
            m_currentSegment.modifyTarget(m_segments[m_currentIndex].targetGain);
        }
    }

    /// If the segment is the one playing, restarts it over numSamples toward its configured target: unlike
    /// modifyTargetIfActive() this changes the remaining time too, for a change that should land quickly.
    void quickModifyIfSegmentActive(const size_t index, const size_t numSamples = 100) noexcept
    {
        if (m_currentIndex == index)
        {
            m_currentSegment.setNewFramesAndTarget(numSamples, m_segments[m_currentIndex].targetGain,
                                                   m_segments[m_currentIndex].curve);
        }
    }

    /// Jumps to the last segment with a given, typically short, time, whatever segment was playing: a declick
    /// for a voice that is stolen, which does not wait for the normal release.
    void emergencyRelease(const float timeInMilliseconds) noexcept
    {
        m_currentIndex = SegmentCount - 1;
        m_currentSegment.setNewFramesAndTarget(static_cast<size_t>(timeInMilliseconds * m_samplesPerMillisecond),
                                               m_segments[m_currentIndex].targetGain, m_segments[m_currentIndex].curve);
    }

    void triggerFrom(const float startValue) noexcept
    {
        m_currentIndex = 0;
        m_currentSegment.reset(startValue);
        m_currentSegment.setNewFramesAndTarget(m_segments[0].frames, m_segments[0].targetGain, m_segments[0].curve);
        m_isDone = false;
    }

    /// Starts from the current gain: a retrigger does not jump to 0.
    void trigger() noexcept
    {
        triggerFrom(m_currentSegment.currentGain());
    }

    void release() noexcept
    {
        if (m_oneshot || m_sustainIndex >= SegmentCount - 1)
        {
            return;
        }
        m_currentIndex = m_sustainIndex + 1;
        m_currentSegment.setNewFramesAndTarget(m_segments[m_currentIndex].frames, m_segments[m_currentIndex].targetGain,
                                               m_segments[m_currentIndex].curve);
    }

    [[nodiscard]] float step() noexcept
    {
        if (m_isDone)
        {
            return m_currentSegment.currentGain();
        }
        const float result = m_currentSegment.step();
        if (m_currentIndex == m_sustainIndex && !m_oneshot)
        {
            return result;
        }
        if (m_currentSegment.isDone())
        {
            ++m_currentIndex;
            if (m_currentIndex == SegmentCount)
            {
                m_isDone = true;
                return result;
            }
            if (m_currentIndex == m_sustainIndex && !m_oneshot)
            {
                m_currentSegment.setHold(0, m_segments[m_currentIndex].targetGain);
            }
            else
            {
                m_currentSegment.setNewFramesAndTarget(m_segments[m_currentIndex].frames,
                                                       m_segments[m_currentIndex].targetGain,
                                                       m_segments[m_currentIndex].curve);
            }
        }
        return result;
    }

    [[nodiscard]] bool isDone() const noexcept
    {
        return m_isDone;
    }

    [[nodiscard]] size_t currentSegmentIndex() const noexcept
    {
        return m_currentIndex;
    }

  private:
    float m_samplesPerMillisecond;
    std::array<SegmentValues, SegmentCount> m_segments{};
    size_t m_currentIndex{0};
    size_t m_sustainIndex{SegmentCount};
    EnvelopeShaper m_currentSegment{};
    bool m_isDone{true};
    bool m_oneshot{false};
};

/**
 * @brief Attack, decay, sustain: ramps to 1, decays to the sustain level, holds there for ever.
 *
 * No release segment (the sustain point is the third, zero-time segment), for a voice without a note-off, such
 * as a plucked string: it keeps ringing or is triggered again.
 */
class AdsEnvelope : public Envelope<3>
{
  public:
    explicit AdsEnvelope(const float sampleRate) noexcept
        : Envelope<3>(sampleRate)
    {
    }

    void setAttackDecaySustain(const float attackTimeMsecs, const float decayTimeMsecs, const float sustainLevel,
                               const float curve = 0.0f) noexcept
    {
        setSegment(0, attackTimeMsecs, 1.0f, curve);
        setSegment(1, decayTimeMsecs, sustainLevel, curve);
        setSegmentHoldPreviousValue(2, 0.0f);
        markSustainAt(2);
    }
};

}
