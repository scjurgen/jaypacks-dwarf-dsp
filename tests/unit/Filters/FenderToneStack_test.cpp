#include <array>
#include <cmath>
#include <complex>
#include <gtest/gtest.h>
#include <iostream>
#include <vector>

#include "dsp/Filters/FenderToneStack.h"
#include "dsp/Numbers/Random.h"
#include "dsp/Parameters/ParameterSmoothing.h"
#include "tests/unit/TestHelpers.h"

namespace jp::test
{

namespace
{
using Complex = std::complex<double>;

/// Output of the TMB stack by nodal analysis, driven by 1 V, unloaded. Nodes: a (treble cap to
/// treble pot), o (treble wiper, the output), b (treble pot bottom, bass cap, bass pot top),
/// c (slope resistor, bass cap, mid cap), d (bass pot bottom, mid pot top), w (mid wiper).
Complex nodalToneStack(const ToneStackComponents& k, const double t, const double m, const double l,
                       const double frequencyHz)
{
    constexpr size_t n = 6;
    enum Node
    {
        A,
        O,
        B,
        C,
        D,
        W,
        Input,
        Ground
    };
    const Complex s{0.0, 2.0 * c_pi * frequencyHz};
    std::array<std::array<Complex, n>, n> y{};
    std::array<Complex, n> current{};
    const auto resistor = [](const double ohms) { return Complex{1.0 / std::max(ohms, 1e-3), 0.0}; };
    const auto add = [&](const int p, const int q, const Complex admittance)
    {
        for (const auto& [from, to] : {std::pair<int, int>{p, q}, std::pair<int, int>{q, p}})
        {
            if (from >= static_cast<int>(n))
            {
                continue;
            }
            y[from][from] += admittance;
            if (to < static_cast<int>(n))
            {
                y[from][to] -= admittance;
            }
            else if (to == Input)
            {
                current[from] += admittance;
            }
        }
    };
    add(Input, A, s * k.c1);
    add(A, O, resistor((1.0 - t) * k.r1));
    add(O, B, resistor(t * k.r1));
    add(Input, C, resistor(k.r4));
    add(C, B, s * k.c2);
    add(B, D, resistor(l * k.r2));
    add(C, W, s * k.c3);
    add(W, Ground, resistor(m * k.r3));
    add(W, D, resistor((1.0 - m) * k.r3));
    // Gaussian elimination with partial pivoting
    for (size_t col = 0; col < n; ++col)
    {
        size_t pivot = col;
        for (size_t row = col + 1; row < n; ++row)
        {
            if (std::abs(y[row][col]) > std::abs(y[pivot][col]))
            {
                pivot = row;
            }
        }
        std::swap(y[col], y[pivot]);
        std::swap(current[col], current[pivot]);
        for (size_t row = col + 1; row < n; ++row)
        {
            const Complex factor = y[row][col] / y[col][col];
            for (size_t j = col; j < n; ++j)
            {
                y[row][j] -= factor * y[col][j];
            }
            current[row] -= factor * current[col];
        }
    }
    std::array<Complex, n> v{};
    for (size_t row = n; row-- > 0;)
    {
        Complex sum = current[row];
        for (size_t j = row + 1; j < n; ++j)
        {
            sum -= y[row][j] * v[j];
        }
        v[row] = sum / y[row][row];
    }
    return v[O];
}

const std::vector<double> c_probes{30.0, 60.0, 100.0, 200.0, 400.0, 800.0, 1500.0, 3000.0, 5000.0};
}

TEST(FenderToneStack, ClosedFormEqualsNodalAnalysis)
{
    Random random{17};
    double worst = 0.0;
    for (int i = 0; i < 300; ++i)
    {
        const double t = random.uniformDouble();
        const double m = random.uniformDouble();
        const double l = random.uniformDouble();
        const ToneStackAnalog h = toneStackAnalog(c_toneStackTwinReverbAB763, t, m, l);
        for (const double f : {20.0, 100.0, 500.0, 2000.0, 8000.0, 20000.0})
        {
            const Complex expected = nodalToneStack(c_toneStackTwinReverbAB763, t, m, l, f);
            const Complex actual = toneStackAnalogResponse(h, f);
            worst = std::max(worst, std::abs(toDb(std::abs(actual)) - toDb(std::abs(expected))));
            EXPECT_LT(std::abs(actual - expected), 1e-6 * std::abs(expected) + 1e-12) << t << " " << m << " " << l;
        }
    }
    std::cout << "[ stack    ] closed form vs nodal analysis: worst " << worst << " dB\n";
    EXPECT_LT(worst, 1e-6);
}

class FenderToneStackRates : public ::testing::TestWithParam<float>
{
};

TEST_P(FenderToneStackRates, DigitalIsTheBilinearImageOfTheAnalogStack)
{
    // exact at the warped frequency (checks the split and the transform); the warp itself is
    // reported against the unwarped analog response up to 5 kHz
    const float sampleRate = GetParam();
    FenderToneStack stack;
    stack.setSampleRate(sampleRate);
    double worst = 0.0;
    double worstImage = 0.0;
    for (const double bass : {0.0, 0.3, 0.5, 0.8, 1.0})
    {
        for (const double mid : {0.0, 0.5, 1.0})
        {
            for (const double treble : {0.0, 0.3, 0.5, 0.8, 1.0})
            {
                stack.setControls(bass, mid, treble);
                for (const double f : c_probes)
                {
                    const double digital = toDb(std::abs(stack.response(f)));
                    const double analog = toDb(std::abs(toneStackAnalogResponse(stack.getAnalog(), f)));
                    const double warped =
                        static_cast<double>(sampleRate) / c_pi * std::tan(c_pi * f / static_cast<double>(sampleRate));
                    const double image = toDb(std::abs(toneStackAnalogResponse(stack.getAnalog(), warped)));
                    worst = std::max(worst, std::abs(digital - analog));
                    worstImage = std::max(worstImage, std::abs(digital - image));
                }
            }
        }
    }
    std::cout << "[ stack    ] fs " << sampleRate << ": digital vs analog at the warped frequency, worst " << worstImage
              << " dB; warp up to 5 kHz, worst " << worst << " dB\n";
    EXPECT_LT(worstImage, 0.005); // float coefficients: measured 5e-4 dB (44.1, 48 kHz), 2.5e-3 dB (96 kHz)
    EXPECT_LT(worst, 0.4);        // measured 0.34 (44.1 kHz), 0.28 (48 kHz), 0.07 (96 kHz)
}

TEST_P(FenderToneStackRates, RunningFilterMatchesDesignAndStaysStable)
{
    const float sampleRate = GetParam();
    FenderToneStack stack;
    stack.setSampleRate(sampleRate);
    for (const double knob : {0.0, 0.5, 1.0})
    {
        stack.setControls(knob, 1.0 - knob, knob);
        for (const double f : {40.0, 400.0, 4000.0})
        {
            stack.reset();
            const double measured =
                measureSineGain([&stack](const float x) { return stack.process(x); }, f, sampleRate, 0.1, 0.5, 0.5);
            EXPECT_NEAR(toDb(measured), toDb(std::abs(stack.response(f))), 0.01) << knob << " " << f;
        }
        // both sections' poles inside the unit circle
        const BiquadSection& first = stack.getFirstOrderSection();
        const BiquadSection& second = stack.getSecondOrderSection();
        EXPECT_LT(std::abs(first.a1), 1.0f);
        EXPECT_EQ(first.a2, 0.0f);
        EXPECT_LT(std::abs(second.a2), 1.0f);
        EXPECT_LT(std::abs(second.a1), 1.0f + second.a2);
    }
}

INSTANTIATE_TEST_SUITE_P(SampleRates, FenderToneStackRates, ::testing::Values(44100.0f, 48000.0f, 96000.0f));

TEST(FenderToneStack, KnobsActWhereTheyShould)
{
    FenderToneStack stack;
    stack.setSampleRate(48000.0f);
    const auto gainDb = [&](const double bass, const double mid, const double treble, const double f)
    {
        stack.setControls(bass, mid, treble);
        return toDb(std::abs(stack.response(f)));
    };
    const double trebleLift = gainDb(0.5, 0.5, 1.0, 3000.0) - gainDb(0.5, 0.5, 0.0, 3000.0);
    const double bassLift = gainDb(1.0, 0.5, 0.5, 80.0) - gainDb(0.0, 0.5, 0.5, 80.0);
    const double midLift = gainDb(0.5, 1.0, 0.5, 500.0) - gainDb(0.5, 0.0, 0.5, 500.0);
    std::cout << "[ stack    ] treble 0 -> 10 at 3 kHz: " << trebleLift << " dB, bass 0 -> 10 at 80 Hz: " << bassLift
              << " dB, mid 0 -> 10 at 500 Hz: " << midLift << " dB\n";
    EXPECT_GT(trebleLift, 10.0);
    EXPECT_GT(bassLift, 10.0);
    EXPECT_GT(midLift, 5.0);
    // the Fender scoop: with everything at 5 the middle sits well below both ends
    const double scoop = gainDb(0.5, 0.5, 0.5, 500.0);
    std::cout << "[ stack    ] all at 5: 80 Hz " << gainDb(0.5, 0.5, 0.5, 80.0) << " dB, 500 Hz " << scoop
              << " dB, 4 kHz " << gainDb(0.5, 0.5, 0.5, 4000.0) << " dB\n";
    EXPECT_LT(scoop, gainDb(0.5, 0.5, 0.5, 4000.0) - 3.0);
}

TEST(FenderToneStack, RampedRedesignEqualsPerSampleRedesign)
{
    // treble jumps 0 -> 10 -> 0 (smoothed, 20 ms) under a 237 Hz sine: redesigned every 16
    // samples and ramped, the largest output step equals the one of a redesign on every
    // sample; applied at once every 16 samples it is many times larger
    const auto largestStep = [](const size_t interval, const size_t ramp)
    {
        FenderToneStack stack;
        stack.setSampleRate(48000.0f);
        stack.setRampSamples(ramp);
        stack.setControls(0.5, 0.5, 0.0);
        stack.reset();
        OnePoleSmoother treble;
        treble.setSampleRate(48000.0f);
        treble.setTimeMs(20.0f);
        treble.snapTo(0.0f);
        double previous = 0.0;
        double worst = 0.0;
        for (size_t i = 0; i < 48000; ++i)
        {
            treble.setTarget((i >= 12000 && i < 24000) ? 1.0f : 0.0f);
            const float value = treble.next();
            if (i % interval == 0)
            {
                stack.setControls(0.5, 0.5, static_cast<double>(value));
            }
            const auto x = static_cast<float>(0.25 * std::sin(2.0 * c_pi * 237.0 * static_cast<double>(i) / 48000.0));
            const double y = static_cast<double>(stack.process(x));
            if (i > 4800)
            {
                worst = std::max(worst, std::abs(y - previous));
            }
            previous = y;
        }
        return worst;
    };
    const double perSample = largestStep(1, 0);
    const double ramped = largestStep(16, 16);
    const double atOnce = largestStep(16, 0);
    std::cout << "[ stack    ] treble jumps, largest step: redesign per sample " << perSample << ", every 16 ramped "
              << ramped << ", every 16 at once " << atOnce << "\n";
    EXPECT_LT(ramped, 1.01 * perSample);
    EXPECT_GT(atOnce, 5.0 * perSample);
}

TEST(FenderToneStack, AudioTaperEndsAndMiddle)
{
    EXPECT_DOUBLE_EQ(audioTaper(0.0), 0.0);
    EXPECT_DOUBLE_EQ(audioTaper(1.0), 1.0);
    EXPECT_NEAR(audioTaper(0.5), 0.1, 1e-12);
    EXPECT_DOUBLE_EQ(audioTaper(-1.0), 0.0);
    EXPECT_DOUBLE_EQ(audioTaper(2.0), 1.0);
}

}
