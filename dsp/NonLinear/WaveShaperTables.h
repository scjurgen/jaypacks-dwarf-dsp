#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include "dsp/Numbers/Interpolation.h"

namespace jp
{

/// One distortion preset: a sparse transfer curve given by equidistant control points and the kernel that
/// reconstructs it between them.
struct WaveShaperPreset
{
    /// A zero-order-hold curve is a staircase: with midPoint the flats are centred on the control points, not
    /// started by them.
    bool midPoint;
    InterpolationFn interpolation;
    /// How many samples before the integer position the kernel's window starts: 0 for hold and linear, 1 for the
    /// 4-point kernels.
    int latency;
    const char* name;
    float delta; ///< distance between control points, centred on 0
    std::vector<float> controlPoints;
};

/// The 31 distortion presets (AbacDsp's documentation says 30), in the order of AbacDsp's table; built on every call
/// (allocates: call it when the plugin is created). Not a function-local static, which a plugin library would leave
/// allocated at its unload.
[[nodiscard]] inline std::vector<WaveShaperPreset> distortionPresets()
{
    std::vector<WaveShaperPreset> presets = {
        {false, linearPt2, 0, "classic silicon", 2.0f, {-1.f, 1.f}},
        {false, bspline43x, 1, "classic germanium", 0.5f, {-1.f, -0.5f, 0.f, 0.5f, 1.f}},
        {false, bspline43x, 1, "classic soft", 0.5f, {-1.f, -0.7f, 0.f, 0.7f, 1.f}},
        {false,
         hermite43x,
         1,
         "classic soft tanh",
         0.125f,
         {-0.995055f, -0.99186f,  -0.986614f, -0.978026f, -0.964028f, -0.941376f, -0.905148f, -0.848284f, -0.761594f,
          -0.635149f, -0.462117f, -0.244919f, 0.f,        0.244919f,  0.462117f,  0.635149f,  0.761594f,  0.848284f,
          0.905148f,  0.941376f,  0.964028f,  0.978026f,  0.986614f,  0.99186f,   0.995055f}},
        {false, bspline43x, 1, "classic medium", 0.2f, {-1.f, -0.5f, -1.430511475e-06f, 0.5f, 1.f}},
        {false, bspline43x, 1, "classic hard", 0.1f, {-1.f, -0.5f, 0.f, 0.5f, 1.f}},
        {true, zeroOrderHold, 0, "classic fuzz", 0.01f, {-1.f, -1.f, 0.f, 1.f, 1.f}},
        {false, linearPt2, 0, "classic rectify", 0.5f, {1.f, 0.5f, 0.f, 0.5f, 1.f}},
        {false, linearPt2, 0, "classic half wave", 0.5f, {0.0f, 0.0f, 0.f, 0.5f, 1.f}},

        {true, zeroOrderHold, 0, "bit crunch noisy", 0.1f, {-1.f, 1.f,  -1.f, 1.f,  -1.f, 1.f,  -1.f, 1.f,
                                                            -1.f, 1.f,  -1.f, 1.f,  -1.f, 1.f,  -1.f, 0.f,
                                                            1.f,  -1.f, 1.f,  -1.f, 1.f,  -1.f, 1.f,  -1.f,
                                                            1.f,  -1.f, 1.f,  -1.f, 1.f,  -1.f, 1.f}},

        {true, zeroOrderHold, 0, "bit crunch 0 narrow", 0.1f, {0.f, -1.f, 0.f, 1.f, 0.f}},
        {true,
         zeroOrderHold,
         0,
         "bit crunch 0 wide",
         0.125f,
         {0.f, -1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f}},
        {true, zeroOrderHold, 0, "bit crunch 1", 0.5f, {-1.f, -0.5f, 0.f, 0.5f, 1.f}},
        {true, zeroOrderHold, 0, "bit crunch 2", 0.25f, {-1.f, -0.75f, -0.5f, -0.25f, 0.f, 0.25f, 0.5f, 0.75f, 1.f}},
        {true, zeroOrderHold, 0, "bit crunch 3", 0.1f, {-1.f,  -0.9f, -0.8f, -0.7f, -0.6f, -0.5f, -0.4f,
                                                        -0.3f, -0.2f, -0.1f, 0.f,   0.1f,  0.2f,  0.3f,
                                                        0.4f,  0.5f,  0.6f,  0.7f,  0.8f,  0.9f,  1.f}},

        {false, linearPt2, 0, "octave noise", 0.1f, {-1.f, 1.f, -1.f, 0.f, 1.f, -1.f, 1.f}},
        {false,
         linearPt2,
         0,
         "octave-noise-more",
         0.1f,
         {-1.f, 1.f, -1.f, 1.f, -1.f, 1.f, -1.f, 1.f, -1.f, 0.f, 1.f, -1.f, 1.f, -1.f, 1.f, -1.f, 1.f, -1.f, 1.f}},
        {false,
         bspline43x,
         1,
         "octave-noisish",
         0.1f,
         {-1.f, 1.f, -1.f, 2.f, -1.f, 1.f, -1.f, 1.f, -1.f, 0.f, 1.f, -1.f, 1.f, -2.f, 1.f, -1.f, 1.f, -1.f, 1.f}},
        {false, hermite43x, 1, "octave a bit", 0.3f, {-1.f, -0.9f, 0.3f, -1.f, 0.f, 0.3f, 0.6f, 0.9f, 1.f}},
        {false, hermite43x, 1, "octave soft", 0.3f, {-1.f, -0.85f, 0.1f, -0.4f, 0.f, 0.6f, -0.1f, 0.85f, 1.f}},
        {false,
         hermite43x,
         1,
         "octave classic",
         0.3f,
         {-1.f, -0.96f, -0.8f, 0.9f, -0.95f, 0.f, 0.95f, -0.9f, 0.8f, 0.96f, 1.f}},
        {false,
         hermite43x,
         1,
         "octave x2",
         0.2f,
         {-1.f, -0.9f, 1.f, -1.f, 1.f, -1.f, -1.90735409e-06f, 1.f, -1.f, 1.f, -1.f, 0.9f, 1.f}},
        {false,
         hermite43x,
         1,
         "octave x2 half wave",
         0.2f,
         {-1.f, -0.9f, 1.f, -1.f, 1.f, -1.f, -1.621246383e-06f, 0.7f, 0.8f, 0.9f, -1.f, 0.9f, 1.f}},

        {false,
         bspline43x,
         1,
         "asymmetric medium",
         0.3f,
         {-0.9999f, -1.f, -0.86f, -0.55f, -0.075f, 0.85f, 1.f, 1.f, 0.9999f}},
        {false,
         hermite43x,
         1,
         "asymmetric harder",
         0.25f,
         {-1.f, -0.75f, -0.5f, -0.25f, 0.f, 0.9f, 1.f, 1.f, 1.f, 1.f}},
        {false, linearPt2, 0, "asymmetric linear and fuzz", 0.1f, {-1.f,  -0.9f, -0.8f, -0.7f, -0.6f, -0.5f, -0.4f,
                                                                   -0.3f, -0.2f, -0.1f, 0.f,   1.f,   1.f,   1.f,
                                                                   1.f,   1.f,   1.f,   1.f,   1.f,   1.f,   1.f}},

        {false, bspline43x, 1, "broken amp 1", 0.25f, {-0.25f, -0.5f, -1.4f, -0.5f, 0.f, 0.5f, 1.4f, 0.5f, 0.25f}},
        {false, bspline43x, 1, "broken amp 2", 0.25f, {0.f, 0.f, 0.f, -0.5f, 0.f, 0.5f, 1.4f, 0.5f, 0.15f}},
        {false, bspline43x, 1, "broken amp 3", 0.1f, {0.f, 0.f, 0.f, 0.f, -1.5f, 0.f, 1.5f, 0.f, 0.f, 0.f, 0.f}},
        {false, bspline43x, 1, "broken amp 4", 0.2f, {-1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f}},
        {false,
         hermite43x,
         1,
         "broken amp 5",
         0.2f,
         {0.f, 0.f, -1.f, -1.f, -1.f, -1.907350452e-06f, 1.f, 1.f, 1.f, 0.f, 0.f}},
    };
    return presets;
}

/**
 * @brief Resamples a sparse, equidistant control-point curve at any x through a chosen interpolation kernel,
 * padding both ends with the first and last point.
 *
 * The points are copied into the middle of a dense buffer of ceil(10 / delta) + 4 entries (so x from -5 to 5 never
 * leaves it), the rest filled with the end values; getValue() then needs no bounds check. The point i of n lies at
 * x = delta (i - (n - 1) / 2), the curve is centred on 0 (an even count straddles 0). Beyond the points the curve
 * stays at the end values. Setting the curve allocates and is proportional to 10 / delta; reading does not.
 */
class InterpolateWithEquidistantControlPoints
{
  public:
    void setInterpolationFunction(const InterpolationFn f, const int offset) noexcept
    {
        m_interpolationFunction = f;
        m_interpolationOffset = offset;
    }

    void setControlVector(const bool midPoint, const std::vector<float>& control, const float dx)
    {
        const auto maxControlPoints = static_cast<size_t>(std::ceil(10.0f / dx));
        m_control.assign(maxControlPoints + 4, 0.0f);
        for (size_t i = 0; i <= m_control.size() / 2; ++i)
        {
            m_control[i] = control.front();
            m_control[m_control.size() - i - 1] = control.back();
        }
        const size_t offset = (m_control.size() - control.size()) / 2;
        for (size_t i = 0; i < control.size(); ++i)
        {
            m_control[i + offset] = control[i];
        }
        // a zero-order-hold curve stays in the middle of an interval (0..1 -> -0.5..0.5)
        const size_t edgeMargin = midPoint ? maxControlPoints + 3 : maxControlPoints + 2;
        m_minValue = -dx * static_cast<float>(edgeMargin) / 2.0f;
        m_dx = 1.0f / dx;
    }

    /// x must lie within [-5, 5] (the dense buffer's range); outside it the read leaves the buffer.
    [[nodiscard]] float getValue(const float x) const noexcept
    {
        const float index = (x - m_minValue) * m_dx;
        const float intIndexPosition = std::floor(index);
        const float fraction = index - intIndexPosition;
        const float* window =
            &m_control[static_cast<size_t>(intIndexPosition) - static_cast<size_t>(m_interpolationOffset)];
        return m_interpolationFunction(window, fraction);
    }

  private:
    std::vector<float> m_control{};
    InterpolationFn m_interpolationFunction{linearPt2};
    int m_interpolationOffset{0};
    float m_minValue{-1.0f};
    float m_dx{1.0f};
};

/**
 * @brief A transfer curve rendered once into BufferSize points over [minValue, maxValue] and read back through the
 * cubic B-spline.
 *
 * The B-spline makes the table smooth between its points at the price of a slight smoothing of corners (it is not
 * interpolating); the table has six padding points on each side for the 4-point window. With ClampValues the input
 * is limited to the range, so an overdriven input reads the end values; the input must be finite. Real-time safe.
 */
template <size_t BufferSize, bool ClampValues>
class WaveShaperInterpolated
{
  public:
    static constexpr int c_padForInterpolation{12};
    static constexpr int c_halfPad{c_padForInterpolation / 2};

    template <typename ValueFunction>
    void assignTable(const float minValue, const float maxValue, const ValueFunction& f)
    {
        m_minValue = minValue;
        m_maxValue = maxValue;
        const auto bufferSpan = static_cast<float>(BufferSize - 1);
        m_rangeDelta = bufferSpan / (maxValue - minValue);
        for (int i = 0; i < static_cast<int>(m_curve.size()); ++i)
        {
            const float x = m_minValue + (m_maxValue - m_minValue) * static_cast<float>(i - c_halfPad) / bufferSpan;
            m_curve[static_cast<size_t>(i)] = f(x);
        }
    }

    [[nodiscard]] float step(const float in) const noexcept
    {
        float value = in;
        if constexpr (ClampValues)
        {
            value = std::clamp(value, m_minValue, m_maxValue);
        }
        const float index = (value - m_minValue) * m_rangeDelta + static_cast<float>(c_halfPad);
        const float intIndexPosition = std::floor(index);
        const float fraction = index - intIndexPosition;
        return bspline43x(&m_curve[static_cast<size_t>(intIndexPosition) - 1], fraction);
    }

    void processBlock(float* inPlace, const size_t numSamples) const noexcept
    {
        for (size_t i = 0; i < numSamples; ++i)
        {
            inPlace[i] = step(inPlace[i]);
        }
    }

  private:
    std::array<float, BufferSize + c_padForInterpolation> m_curve{};
    float m_minValue{-1.0f};
    float m_maxValue{1.0f};
    float m_rangeDelta{0.1f};
};

/**
 * @brief The distortion presets, each rendered into its own table over [-4, 4] at construction.
 *
 * 31 tables of 1025 points (4148 bytes each, 128588 together) for the inputs of a shaper that is driven by a gain in
 * front of it: an input beyond +-4 reads the end value. Building allocates (the presets and a dense buffer per
 * curve): do it when the plugin is created.
 */
class WaveShaperTableStore
{
  public:
    static constexpr size_t c_tableSize{1025};
    static constexpr float c_range{4.0f};

    WaveShaperTableStore()
    {
        const auto& presets = distortionPresets();
        m_waveShapes.reserve(presets.size());
        for (const WaveShaperPreset& setting : presets)
        {
            InterpolateWithEquidistantControlPoints control;
            control.setInterpolationFunction(setting.interpolation, setting.latency);
            control.setControlVector(setting.midPoint, setting.controlPoints, setting.delta);
            WaveShaperInterpolated<c_tableSize, true> shaper;
            shaper.assignTable(-c_range, c_range, [&control](const float x) { return control.getValue(x); });
            m_waveShapes.push_back(shaper);
        }
    }

    [[nodiscard]] size_t presetCount() const noexcept
    {
        return m_waveShapes.size();
    }

    /// The value of preset index (0 to presetCount() - 1; a larger index reads the last preset) at x.
    [[nodiscard]] float getValue(const size_t index, const float x) const noexcept
    {
        return m_waveShapes[std::min(index, m_waveShapes.size() - 1)].step(x);
    }

    void processBlock(const size_t index, float* inPlace, const size_t numSamples) const noexcept
    {
        m_waveShapes[std::min(index, m_waveShapes.size() - 1)].processBlock(inPlace, numSamples);
    }

  private:
    std::vector<WaveShaperInterpolated<c_tableSize, true>> m_waveShapes;
};

}
