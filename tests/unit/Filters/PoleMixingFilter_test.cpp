#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "tests/unit/TestHelpers.h"
#include "dsp/Filters/PoleMixingFilter.h"
#include "dsp/Numbers/Random.h"

namespace jp::test
{

namespace
{
constexpr float c_sampleRate{48000.0f};
constexpr float c_cutoff{1000.0f};

double theoryDb(const size_t preset, const double frequency, const double resonance = 0.0)
{
    const double pole = onePoleLowpassPole(c_cutoff, c_sampleRate);
    return toDb(std::abs(poleMixingResponse(c_poleMixingPresets[preset], pole, resonance, frequency, c_sampleRate)));
}

double measuredDb(const size_t preset, const double frequency, const float resonance = 0.0f,
                  const float sampleRate = c_sampleRate)
{
    PoleMixingFilter filter;
    filter.setSampleRate(sampleRate);
    filter.setCutoff(c_cutoff);
    filter.setResonance(resonance);
    filter.setPreset(preset);
    return toDb(measureSineGain([&filter](const float x) { return filter.process(x); }, frequency, sampleRate));
}

size_t presetIndex(const char* name)
{
    const size_t index = findPoleMixingPreset(name);
    EXPECT_NE(index, c_poleMixingPresetNotFound) << name;
    return index;
}
}

TEST(PoleMixingFilter, TableHasThirtyFiveUniquelyNamedPresets)
{
    EXPECT_EQ(c_poleMixingPresetCount, 35u);
    std::set<std::string> names;
    for (const auto& preset : c_poleMixingPresets)
    {
        EXPECT_TRUE(names.insert(preset.name).second) << "duplicate name " << preset.name;
    }
}

TEST(PoleMixingFilter, FindsPresetsByName)
{
    EXPECT_EQ(findPoleMixingPreset("6 lp(inv)"), 0u);
    EXPECT_EQ(findPoleMixingPreset("4 pole lp"), 3u);
    EXPECT_EQ(findPoleMixingPreset("Bell"), 34u);
    EXPECT_EQ(findPoleMixingPreset("no such preset"), c_poleMixingPresetNotFound);
    static_assert(findPoleMixingPreset("2 pole notch") == 9, "lookup works at compile time");
}

TEST(PoleMixingFilter, ProcessMatchesTheoryForEveryPreset)
{
    for (size_t preset = 0; preset < c_poleMixingPresetCount; ++preset)
    {
        for (const double frequency : {100.0, 1000.0, 5000.0})
        {
            const double theory = theoryDb(preset, frequency);
            const double measured = measuredDb(preset, frequency);
            if (theory > -60.0)
            {
                EXPECT_NEAR(measured, theory, 0.05) << c_poleMixingPresets[preset].name << " at " << frequency;
            }
            else
            {
                EXPECT_LT(measured, -55.0) << c_poleMixingPresets[preset].name << " at " << frequency;
            }
        }
    }
}

TEST(PoleMixingFilter, ProcessMatchesTheoryWithResonance)
{
    // the theory models the one-sample delay of the feedback; small signals keep the stage clamps idle
    for (const char* name : {"4 pole lp", "2 pole bp", "2 pole notch", "2 pole hp"})
    {
        const size_t preset = presetIndex(name);
        for (const float resonance : {0.5f, 1.5f})
        {
            for (const double frequency : {300.0, 1000.0, 1100.0, 3000.0})
            {
                EXPECT_NEAR(measuredDb(preset, frequency, resonance), theoryDb(preset, frequency, resonance), 0.05)
                    << name << " r=" << resonance << " at " << frequency;
            }
        }
    }
}

TEST(PoleMixingFilter, ResponseFamiliesHaveTheirShape)
{
    for (size_t preset = 0; preset < c_poleMixingPresetCount; ++preset)
    {
        const auto& p = c_poleMixingPresets[preset];
        const double low = theoryDb(preset, 100.0);
        const double center = theoryDb(preset, 1000.0);
        const double high = theoryDb(preset, 10000.0);
        const double top = theoryDb(preset, 20000.0);
        if (p.family & PM_LOWPASS)
        {
            EXPECT_GT(low - top, 10.0) << p.name;
        }
        if (p.family & PM_HIGHPASS)
        {
            EXPECT_GT(high - low, 15.0) << p.name;
        }
        if (p.family & PM_BANDPASS)
        {
            EXPECT_GT(center - low, 5.0) << p.name;
            EXPECT_GT(center - high, 5.0) << p.name;
        }
        if (p.family & PM_NOTCH)
        {
            double minimum = 0.0;
            for (double f = 20.0; f < 20000.0; f *= 1.01)
            {
                minimum = std::min(minimum, theoryDb(preset, f));
            }
            EXPECT_LT(minimum, -20.0) << p.name;
            EXPECT_GT(low, -2.0) << p.name;
            EXPECT_GT(top, -2.0) << p.name;
        }
    }
}

TEST(PoleMixingFilter, AllpassMixesAreFlatWithinTheDigitalOnePoleError)
{
    // 1 - 2 L is only approximately allpass for the impulse-invariant one-pole (see header):
    // at fc = 1 kHz the loss reaches about 1.2 dB per order towards Nyquist
    const std::vector<std::pair<const char*, int>> allpasses{{"3 pole ap (inv)", 3},
                                                             {"* 1 pole ap (inv)", 1},
                                                             {"* 2 pole ap", 2},
                                                             {"* 3 pole ap (inv)", 3},
                                                             {"* 4 pole ap", 4}};
    for (const auto& [name, order] : allpasses)
    {
        const size_t preset = presetIndex(name);
        for (double f = 20.0; f < 20000.0; f *= 1.05)
        {
            const double gain = theoryDb(preset, f);
            EXPECT_LT(std::abs(gain), 1.25 * order) << name << " at " << f;
            if (f < c_cutoff / 4.0)
            {
                EXPECT_LT(std::abs(gain), 0.1 * order) << name << " at " << f;
            }
        }
    }
}

TEST(PoleMixingFilter, OutputStaysFiniteAndBoundedForAnyResonance)
{
    constexpr float amplitude = 2.0f; // overdriven input engages the stage clamps
    for (size_t preset = 0; preset < c_poleMixingPresetCount; ++preset)
    {
        const auto& p = c_poleMixingPresets[preset];
        for (const float resonance : {0.0f, 1.0f, 2.0f, 4.0f, 8.0f})
        {
            PoleMixingFilter filter;
            filter.setPreset(preset);
            filter.setResonance(resonance);
            Random random(preset * 10 + static_cast<size_t>(resonance));
            const float feedBound =
                amplitude * (1.0f + resonance * p.resonanceInputGain) + resonance * p.resonanceFeedback;
            const float bound = std::abs(p.inputWeight) * feedBound + std::abs(p.stageWeights[0]) +
                                std::abs(p.stageWeights[1]) + std::abs(p.stageWeights[2]) + std::abs(p.stageWeights[3]);
            for (int i = 0; i < 24000; ++i)
            {
                if (i % 4800 == 0)
                {
                    filter.setCutoff(20.0f + 2000.0f * static_cast<float>(i / 4800));
                }
                const float y = filter.process(amplitude * (2.0f * random.uniform() - 1.0f));
                ASSERT_TRUE(std::isfinite(y)) << p.name << " r=" << resonance;
                ASSERT_LE(std::abs(y), bound * 1.0001f) << p.name << " r=" << resonance;
            }
        }
    }
}

TEST(PoleMixingFilter, SelfOscillatesOnlyAboveTheThreshold)
{
    // 4 pole lp has resonanceFeedback 1.7; at 1 kHz the threshold is r * a = 4.58
    const size_t preset = presetIndex("4 pole lp");
    auto tailRms = [preset](const float resonance)
    {
        PoleMixingFilter filter;
        filter.setCutoff(c_cutoff);
        filter.setPreset(preset);
        filter.setResonance(resonance);
        double sum = 0.0;
        constexpr int length = 96000;
        constexpr int tail = 24000;
        for (int i = 0; i < length; ++i)
        {
            const float y = filter.process(i == 0 ? 1.0f : 0.0f);
            if (i >= length - tail)
            {
                sum += static_cast<double>(y) * y;
            }
        }
        return std::sqrt(sum / tail);
    };
    EXPECT_LT(tailRms(2.0f), 1e-9); // r * a = 3.4
    EXPECT_GT(tailRms(3.0f), 0.05); // r * a = 5.1
}

TEST(PoleMixingFilter, OscillationThresholdMatchesTheStatedValues)
{
    auto thresholdAt = [](const float cutoff)
    { return poleMixingOscillationThreshold(onePoleLowpassPole(cutoff, 48000.0f)); };
    EXPECT_NEAR(thresholdAt(100.0f), 4.05, 0.01);
    EXPECT_NEAR(thresholdAt(1000.0f), 4.58, 0.01);
    EXPECT_NEAR(thresholdAt(3000.0f), 6.16, 0.01);
    EXPECT_NEAR(thresholdAt(6000.0f), 10.20, 0.01);
    EXPECT_NEAR(thresholdAt(12000.0f), 5.41, 0.01); // Nyquist branch
    // the two branches meet at p = 1/3, where both give 16
    EXPECT_NEAR(poleMixingOscillationThreshold(1.0 / 3.0 + 1e-6), 16.0, 0.01);
    EXPECT_NEAR(poleMixingOscillationThreshold(1.0 / 3.0 - 1e-6), 16.0, 0.01);
}

TEST(PoleMixingFilter, OscillationThresholdSeparatesDecayFromGrowth)
{
    const size_t preset = presetIndex("4 pole lp");
    const float a = c_poleMixingPresets[preset].resonanceFeedback;
    // rms of the impulse response over [start, start + 0.25 s)
    auto rmsWindow = [preset, a](const float cutoff, const double loopGain, const int start)
    {
        PoleMixingFilter filter;
        filter.setCutoff(cutoff);
        filter.setPreset(preset);
        filter.setResonance(static_cast<float>(loopGain) / a);
        double sum = 0.0;
        constexpr int window = 12000;
        for (int i = 0; i < start + window; ++i)
        {
            const float y = filter.process(i == 0 ? 1e-6f : 0.0f);
            if (i >= start)
            {
                sum += static_cast<double>(y) * y;
            }
        }
        return std::sqrt(sum / window);
    };
    for (const float cutoff : {200.0f, 1000.0f, 3000.0f, 5000.0f})
    {
        const double threshold = poleMixingOscillationThreshold(onePoleLowpassPole(cutoff, c_sampleRate));
        EXPECT_LT(rmsWindow(cutoff, 0.97 * threshold, 72000), rmsWindow(cutoff, 0.97 * threshold, 12000)) << cutoff;
        // growth from a 1e-6 impulse to full scale
        EXPECT_GT(rmsWindow(cutoff, 1.03 * threshold, 72000), 1e-2) << cutoff;
    }
}

TEST(PoleMixingFilter, TuningForPeakInvertsTheThresholdSolver)
{
    for (const float sampleRate : {44100.0f, 48000.0f, 96000.0f})
    {
        for (float peak = 20.0f; peak < 0.48f * sampleRate; peak *= 1.25f)
        {
            const PoleMixingTuning tuning = poleMixingTuningForPeak(peak, sampleRate);
            ASSERT_GT(tuning.pole, 1.0f / 3.0f) << peak;
            ASSERT_LT(tuning.pole, 1.0f) << peak;
            EXPECT_NEAR(tuning.oscillationThreshold / poleMixingOscillationThreshold(tuning.pole), 1.0, 1e-3)
                << sampleRate << " " << peak;
        }
    }
    // fs / 6 has an exact solution: phi = pi / 6, w + phi = pi / 2
    const PoleMixingTuning sixth = poleMixingTuningForPeak(8000.0f, 48000.0f);
    EXPECT_NEAR(sixth.pole, 0.5f, 1e-6f);
    EXPECT_NEAR(sixth.oscillationThreshold, 9.0f, 1e-4f);
}

TEST(PoleMixingFilter, TunedFilterSelfOscillatesAtThePeakFrequency)
{
    const size_t preset = presetIndex("4 pole lp");
    const float a = c_poleMixingPresets[preset].resonanceFeedback;
    for (const float sampleRate : {44100.0f, 48000.0f, 96000.0f})
    {
        for (const float peak : {200.0f, 1000.0f, 3000.0f, 6000.0f})
        {
            const PoleMixingTuning tuning = poleMixingTuningForPeak(peak, sampleRate);
            PoleMixingFilter filter;
            filter.setSampleRate(sampleRate);
            filter.setPreset(preset);
            filter.setPole(tuning.pole);
            // just above the threshold: grows from a tiny impulse, the clamps hold it small
            filter.setResonance(1.01f * tuning.oscillationThreshold / a);
            const auto length = static_cast<int>(3.0f * sampleRate);
            const auto measureFrom = static_cast<int>(2.0f * sampleRate);
            int firstCrossing = -1;
            int lastCrossing = -1;
            int crossings = 0;
            float previous = 0.0f;
            for (int i = 0; i < length; ++i)
            {
                const float y = filter.process(i == 0 ? 1e-6f : 0.0f);
                if (i > measureFrom && previous < 0.0f && y >= 0.0f)
                {
                    if (firstCrossing < 0)
                    {
                        firstCrossing = i;
                    }
                    else
                    {
                        ++crossings;
                    }
                    lastCrossing = i;
                }
                previous = y;
            }
            ASSERT_GT(crossings, 10) << sampleRate << " " << peak;
            const double measured = sampleRate * crossings / static_cast<double>(lastCrossing - firstCrossing);
            EXPECT_NEAR(measured / peak, 1.0, 0.005) << sampleRate << " " << peak;
        }
    }
}

TEST(PoleMixingFilter, TunedResponsePeaksJustBelowThePeakFrequency)
{
    const auto& preset = c_poleMixingPresets[presetIndex("4 pole lp")];
    for (const float sampleRate : {44100.0f, 48000.0f, 96000.0f})
    {
        for (const float peak : {100.0f, 1000.0f, 3000.0f, sampleRate / 6.0f})
        {
            const PoleMixingTuning tuning = poleMixingTuningForPeak(peak, sampleRate);
            auto responsePeak = [&](const double fraction)
            {
                const double resonance = fraction * tuning.oscillationThreshold / preset.resonanceFeedback;
                double best = 0.0;
                double bestFrequency = 0.0;
                for (double f = 0.8 * peak; f < 1.1 * peak; f *= 1.0002)
                {
                    const double magnitude =
                        std::abs(poleMixingResponse(preset, tuning.pole, resonance, f, sampleRate));
                    if (magnitude > best)
                    {
                        best = magnitude;
                        bestFrequency = f;
                    }
                }
                return bestFrequency / peak;
            };
            EXPECT_NEAR(responsePeak(0.95), 0.985, 0.01) << sampleRate << " " << peak;
            EXPECT_NEAR(responsePeak(0.85), 0.9525, 0.0125) << sampleRate << " " << peak;
        }
    }
}

TEST(PoleMixingFilter, TuningAgreesWithAbacDspMeasuredCutoffCorrection)
{
    // AbacDsp FourStageFilter::warpCutoffForSampleRate() at 48 kHz (same feedback topology, fit to
    // measured resonance peaks at 90 % of critical resonance), see docs/references.md
    auto abacWarp48 = [](const double x)
    { return 0.1409743683 + 0.9999793344 * x - 6.203395634e-05 * x * x + 2.855230937e-09 * x * x * x; };
    for (const float peak : {100.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 3000.0f, 4000.0f})
    {
        const double stageCutoff = -std::log(poleMixingTuningForPeak(peak, 48000.0f).pole) * 48000.0 / (2.0 * c_pi);
        EXPECT_NEAR(stageCutoff / abacWarp48(peak), 1.0, 0.002) << peak;
    }
}

TEST(PoleMixingFilter, SwitchingPresetKeepsStageStates)
{
    PoleMixingFilter filter;
    filter.setPreset(presetIndex("4 pole lp"));
    for (int i = 0; i < 48000; ++i)
    {
        std::ignore = filter.process(0.5f);
    }
    // all stages hold 0.5, so any mix with unity DC gain continues without a step
    filter.setPreset(presetIndex("2 pole lp"));
    EXPECT_NEAR(filter.process(0.5f), 0.5f, 1e-4f);
    filter.setPreset(presetIndex("2 pole notch"));
    EXPECT_NEAR(filter.process(0.5f), 0.5f, 1e-4f);
}

TEST(PoleMixingFilter, OutOfRangePresetIsIgnored)
{
    PoleMixingFilter filter;
    filter.setPreset(5);
    filter.setPreset(c_poleMixingPresetCount);
    filter.setPreset(1000);
    EXPECT_EQ(filter.getPresetIndex(), 5u);
}

TEST(PoleMixingFilter, CutoffIsIndependentOfSampleRate)
{
    // four stages at their cutoff: -12 dB at any sample rate (low cutoff, see OnePoleLowpass)
    const size_t preset = presetIndex("4 pole lp");
    for (const float sampleRate : {44100.0f, 48000.0f, 96000.0f})
    {
        EXPECT_NEAR(measuredDb(preset, c_cutoff, 0.0f, sampleRate), -12.04, 0.05) << sampleRate;
    }
}

TEST(PoleMixingFilter, ResetClearsState)
{
    PoleMixingFilter filter;
    filter.setPreset(presetIndex("4 pole lp"));
    filter.setResonance(1.0f);
    for (int i = 0; i < 1000; ++i)
    {
        std::ignore = filter.process(0.7f);
    }
    filter.reset();
    EXPECT_EQ(filter.process(0.0f), 0.0f);
}

}
