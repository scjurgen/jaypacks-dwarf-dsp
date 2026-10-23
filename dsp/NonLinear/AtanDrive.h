#pragma once

#include <algorithm>
#include <cmath>

namespace jp
{

/**
 * @brief Symmetric atan soft clipper with a pre-gain, y = atan(d x) / atan(d).
 *
 * atan compresses: the curve flattens towards its limit pi / 2, so the clipping sets in
 * gradually and the harmonics are mostly odd and fall off quickly (the soft, amp-like
 * counterpart of an atanh or a hard clip). The normalisation by atan(d) keeps x = +-1 at
 * y = +-1 for every drive, so the output stays bounded by about pi / (2 atan(d)) (at most 2 for
 * d >= 1) and small signals get the gain d / atan(d): higher drive is louder and more
 * compressed, as when an amp's volume goes up. d is limited to [1e-3, 1e4]; as d -> 0 the curve
 * becomes the identity.
 *
 * Memoryless, so it aliases: the harmonics it creates are not band-limited.
 * @see https://ccrma.stanford.edu/~jos/pasp/Soft_Clipping.html
 */
class AtanDrive
{
  public:
    void setDrive(const float drive) noexcept
    {
        m_drive = std::clamp(drive, 1e-3f, 1e4f);
        m_norm = 1.0f / std::atan(m_drive);
    }

    [[nodiscard]] float getDrive() const noexcept
    {
        return m_drive;
    }

    /// Gain for signals small enough to stay linear, d / atan(d).
    [[nodiscard]] float getSmallSignalGain() const noexcept
    {
        return m_drive * m_norm;
    }

    [[nodiscard]] float process(const float in) const noexcept
    {
        return std::atan(m_drive * in) * m_norm;
    }

  private:
    float m_drive{1.0f};
    float m_norm{1.27323954f};
};

}
