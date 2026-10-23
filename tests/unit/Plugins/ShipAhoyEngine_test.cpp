#include <algorithm>
#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "tests/unit/TestHelpers.h"
#include "dsp/Numbers/Random.h"
#include "plugins/shipahoy/ShipAhoyEngine.h"
#include "tests/unit/ShipAhoyClockedEngine.h"

namespace jp::test
{

namespace
{
constexpr float c_sampleRate{48000.0f};

using RandomType = ClockedEngine::RandomType;

float processOne(ClockedEngine& pedal, const float in)
{
    float out = 0.0f;
    pedal.process(&in, &out, 1);
    return out;
}

/// Pedal with a fixed cutoff (octave range 0), snapped to its settings.
ClockedEngine fixedPedal(const float filterType, const float resonance = 0.0f, const float frequency = 1000.0f)
{
    ClockedEngine pedal;
    pedal.setSampleRate(c_sampleRate);
    pedal.setOctaveRange(0.0f);
    pedal.setFrequency(frequency);
    pedal.setResonance(resonance);
    pedal.setFilterType(filterType);
    pedal.setMix(100.0f);
    pedal.reset();
    return pedal;
}

double gainDb(const float filterType, const double frequency)
{
    ClockedEngine pedal = fixedPedal(filterType);
    return toDb(measureSineGain([&pedal](const float x) { return processOne(pedal, x); }, frequency, c_sampleRate));
}

/// Targets of the next count hops.
std::vector<float> hopTargets(ClockedEngine& pedal, const size_t count)
{
    std::vector<float> targets;
    targets.reserve(count);
    uint64_t hops = pedal.getHopCount();
    while (targets.size() < count)
    {
        std::ignore = processOne(pedal, 0.0f);
        if (pedal.getHopCount() != hops)
        {
            hops = pedal.getHopCount();
            targets.push_back(pedal.getTarget());
        }
    }
    return targets;
}

struct Stats
{
    double mean;
    double deviation;
    double lagOneCorrelation;
};

Stats stats(const std::vector<float>& values)
{
    const auto n = static_cast<double>(values.size());
    double sum = 0.0;
    for (const float v : values)
    {
        sum += v;
    }
    const double mean = sum / n;
    double variance = 0.0;
    double covariance = 0.0;
    for (size_t i = 0; i < values.size(); ++i)
    {
        variance += (values[i] - mean) * (values[i] - mean);
        if (i > 0)
        {
            covariance += (values[i] - mean) * (values[i - 1] - mean);
        }
    }
    return {mean, std::sqrt(variance / n), covariance / variance};
}

ClockedEngine randomPedal(const RandomType type, const float sampleRate = 1000.0f, const float speed = 16.0f)
{
    // a low sample rate makes hops cheap; the hop logic does not depend on it
    ClockedEngine pedal(1234u);
    pedal.setSampleRate(sampleRate);
    pedal.setSpeed(speed);
    pedal.setRandomType(static_cast<float>(type));
    pedal.reset();
    return pedal;
}
}

TEST(ShipAhoyEngine, DefaultsAreTheShipAhoyPreset)
{
    ClockedEngine pedal;
    EXPECT_EQ(pedal.getFilterPresetIndex(), findPoleMixingPreset("2 pole lp"));
    EXPECT_FLOAT_EQ(pedal.getPeakFrequency(), ShipAhoyEngine::c_defaultFrequency);
    EXPECT_EQ(ShipAhoyEngine::c_defaultResonance, 85.0f);
}

TEST(ShipAhoyEngine, FilterTypeMenuMapsToTheNamedPresets)
{
    EXPECT_EQ(ShipAhoyEngine::c_filterTypePresets[4], findPoleMixingPreset("2 pole bp"));   // bp 6 dB
    EXPECT_EQ(ShipAhoyEngine::c_filterTypePresets[6], findPoleMixingPreset("1 pole hp"));   // hp 6 dB
    EXPECT_EQ(ShipAhoyEngine::c_filterTypePresets[9], findPoleMixingPreset("* 4 pole hp")); // hp 24 dB
    ClockedEngine pedal;
    for (size_t i = 0; i < ShipAhoyEngine::c_filterTypeCount; ++i)
    {
        pedal.setFilterType(static_cast<float>(i));
        EXPECT_EQ(pedal.getFilterPresetIndex(), ShipAhoyEngine::c_filterTypePresets[i]) << i;
    }
    pedal.setFilterType(-3.0f);
    EXPECT_EQ(pedal.getFilterPresetIndex(), ShipAhoyEngine::c_filterTypePresets[0]);
    pedal.setFilterType(99.0f);
    EXPECT_EQ(pedal.getFilterPresetIndex(), ShipAhoyEngine::c_filterTypePresets[ShipAhoyEngine::c_filterTypeCount - 1]);
}

TEST(ShipAhoyEngine, FilterTypesHaveTheShapeTheirLabelSays)
{
    // centre 1 kHz, no resonance: compare 100 Hz, 1 kHz and 6 kHz
    for (const float type : {0.0f, 1.0f, 2.0f, 3.0f})
    {
        EXPECT_GT(gainDb(type, 100.0) - gainDb(type, 6000.0), 12.0) << ShipAhoyEngine::c_filterTypeLabels[type];
    }
    for (const float type : {6.0f, 7.0f, 8.0f, 9.0f})
    {
        EXPECT_GT(gainDb(type, 6000.0) - gainDb(type, 100.0), 12.0) << ShipAhoyEngine::c_filterTypeLabels[type];
    }
    for (const float type : {4.0f, 5.0f})
    {
        EXPECT_GT(gainDb(type, 1000.0) - gainDb(type, 100.0), 6.0) << ShipAhoyEngine::c_filterTypeLabels[type];
        EXPECT_GT(gainDb(type, 1000.0) - gainDb(type, 6000.0), 6.0) << ShipAhoyEngine::c_filterTypeLabels[type];
    }
    // steeper order, steeper slope one octave above the cutoff region
    EXPECT_LT(gainDb(3.0f, 8000.0), gainDb(1.0f, 8000.0) - 12.0);
    EXPECT_LT(gainDb(9.0f, 125.0), gainDb(7.0f, 125.0) - 12.0);
    // notch: deep cut at the stage cutoff (939 Hz when the peak is tuned to 1 kHz; the digital
    // one-pole moves the exact zero slightly), passband at the ends
    const double stageCutoff =
        -std::log(poleMixingTuningForPeak(1000.0f, c_sampleRate).pole) * c_sampleRate / (2.0 * c_pi);
    EXPECT_LT(gainDb(10.0f, stageCutoff), -18.0);
    EXPECT_GT(gainDb(10.0f, 50.0), -1.0);
}

TEST(ShipAhoyEngine, FullyDryMixPassesTheInputUnchanged)
{
    ClockedEngine pedal = fixedPedal(3.0f, 90.0f);
    pedal.setMix(0.0f);
    pedal.reset();
    Random random(5u);
    for (int i = 0; i < 48000; ++i)
    {
        const float x = 2.0f * random.uniform() - 1.0f;
        ASSERT_EQ(processOne(pedal, x), x) << i;
    }
}

TEST(ShipAhoyEngine, MixIsZeroToOneHundredPercentWithEqualPowerInTheMiddle)
{
    // DC through the lowpass (DC gain 1) settles to 1, so the output is dry gain + wet gain
    ClockedEngine dry = fixedPedal(1.0f);
    ClockedEngine half = fixedPedal(1.0f);
    ClockedEngine wet = fixedPedal(1.0f);
    ClockedEngine below = fixedPedal(1.0f);
    ClockedEngine above = fixedPedal(1.0f);
    dry.setMix(0.0f);
    half.setMix(50.0f);
    wet.setMix(100.0f);
    below.setMix(-40.0f);
    above.setMix(140.0f);
    for (auto* pedal : {&dry, &half, &wet, &below, &above})
    {
        pedal->reset();
    }
    float outDry = 0.0f;
    float outHalf = 0.0f;
    float outWet = 0.0f;
    float outBelow = 0.0f;
    float outAbove = 0.0f;
    for (int i = 0; i < 48000; ++i)
    {
        outDry = processOne(dry, 1.0f);
        outHalf = processOne(half, 1.0f);
        outWet = processOne(wet, 1.0f);
        outBelow = processOne(below, 1.0f);
        outAbove = processOne(above, 1.0f);
    }
    EXPECT_NEAR(outDry, 1.0f, 1e-4f);
    EXPECT_NEAR(outWet, 1.0f, 1e-4f);
    EXPECT_NEAR(outHalf, 2.0f * std::cos(c_pi / 4.0), 1e-4f) << "equal gains of 1/sqrt(2) each";
    EXPECT_FLOAT_EQ(outBelow, outDry) << "below 0 % is clamped";
    EXPECT_FLOAT_EQ(outAbove, outWet) << "above 100 % is clamped";
}

TEST(ShipAhoyEngine, FullyWetMixIsTheFilterOutput)
{
    ClockedEngine pedal = fixedPedal(3.0f, 50.0f);
    const PoleMixingTuning tuning = poleMixingTuningForPeak(1000.0f, c_sampleRate);
    PoleMixingFilter filter;
    filter.setSampleRate(c_sampleRate);
    filter.setPole(tuning.pole);
    filter.setPreset(findPoleMixingPreset("4 pole lp"));
    filter.setResonance(0.5f * tuning.oscillationThreshold /
                        c_poleMixingPresets[findPoleMixingPreset("4 pole lp")].resonanceFeedback);
    Random random(6u);
    for (int i = 0; i < 48000; ++i)
    {
        const float x = 0.5f * (2.0f * random.uniform() - 1.0f);
        ASSERT_NEAR(processOne(pedal, x), filter.process(x), 1e-5f) << i;
    }
}

TEST(ShipAhoyEngine, MixChangesAreSmoothed)
{
    ClockedEngine pedal = fixedPedal(1.0f); // 2 pole lp, DC gain 1
    pedal.setMix(0.0f);
    pedal.reset();
    float previous = 0.0f;
    for (int i = 0; i < 48000; ++i)
    {
        previous = processOne(pedal, 0.5f);
    }
    pedal.setMix(100.0f);
    float maxStep = 0.0f;
    for (int i = 0; i < 48000; ++i)
    {
        const float y = processOne(pedal, 0.5f);
        maxStep = std::max(maxStep, std::abs(y - previous));
        previous = y;
    }
    EXPECT_LT(maxStep, 1e-3f);
    EXPECT_NEAR(previous, 0.5f, 1e-4f);
}

TEST(ShipAhoyEngine, HopPeriodIsOneOverSpeed)
{
    for (const float sampleRate : {44100.0f, 48000.0f, 96000.0f})
    {
        for (const float speed : {0.5f, 7.0f, 16.0f})
        {
            ClockedEngine pedal;
            pedal.setSampleRate(sampleRate);
            pedal.setSpeed(speed);
            pedal.reset();
            std::vector<int64_t> hopSamples;
            uint64_t hops = 0;
            for (int64_t i = 0; hopSamples.size() < 10; ++i)
            {
                std::ignore = processOne(pedal, 0.0f);
                if (pedal.getHopCount() != hops)
                {
                    hops = pedal.getHopCount();
                    hopSamples.push_back(i);
                }
            }
            EXPECT_EQ(hopSamples.front(), 0) << "first hop on the first sample";
            const double period = sampleRate / speed;
            for (size_t k = 1; k < hopSamples.size(); ++k)
            {
                EXPECT_NEAR(static_cast<double>(hopSamples[k] - hopSamples[k - 1]), period, 1.0)
                    << sampleRate << " " << speed;
            }
        }
    }
}

TEST(ShipAhoyEngine, SpeedChangeTakesEffectWithinOneNewPeriod)
{
    ClockedEngine pedal;
    pedal.setSpeed(0.1f);
    pedal.reset();
    for (int i = 0; i < 4800; ++i)
    {
        std::ignore = processOne(pedal, 0.0f);
    }
    const uint64_t hops = pedal.getHopCount();
    pedal.setSpeed(16.0f);
    int samples = 0;
    while (pedal.getHopCount() == hops)
    {
        std::ignore = processOne(pedal, 0.0f);
        ++samples;
    }
    EXPECT_LE(samples, 3001);
}

TEST(ShipAhoyEngine, FrequencyStaysInsideTheOctaveRange)
{
    for (size_t type = 0; type < ClockedEngine::c_randomTypeCount; ++type)
    {
        ClockedEngine pedal(type);
        pedal.setSpeed(16.0f);
        pedal.setRandomType(static_cast<float>(type));
        pedal.setFrequency(1000.0f);
        pedal.setOctaveRange(4.0f);
        pedal.setGlideUp(30.0f);
        pedal.setGlideDown(70.0f);
        pedal.reset();
        float lowest = 1e9f;
        float highest = 0.0f;
        for (int i = 0; i < 48000 * 20; ++i)
        {
            std::ignore = processOne(pedal, 0.0f);
            lowest = std::min(lowest, pedal.getPeakFrequency());
            highest = std::max(highest, pedal.getPeakFrequency());
        }
        EXPECT_GE(lowest, 250.0f * 0.9999f) << type;
        EXPECT_LE(highest, 4000.0f * 1.0001f) << type;
        if (static_cast<RandomType>(type) != RandomType::Violet)
        {
            EXPECT_GT(highest / lowest, 8.0f) << type << ": uses most of the 16:1 range";
        }
    }
}

TEST(ShipAhoyEngine, FrequencyIsClampedToASixthOfTheSampleRate)
{
    ClockedEngine pedal;
    pedal.setFrequency(6000.0f);
    pedal.setOctaveRange(8.0f);
    pedal.setSpeed(16.0f);
    pedal.reset();
    float highest = 0.0f;
    for (int i = 0; i < 48000 * 5; ++i)
    {
        std::ignore = processOne(pedal, 0.0f);
        ASSERT_LE(pedal.getPeakFrequency(), 8000.0f);
        ASSERT_GE(pedal.getPeakFrequency(), ShipAhoyEngine::c_minFrequency);
        highest = std::max(highest, pedal.getPeakFrequency());
    }
    EXPECT_EQ(highest, 8000.0f);
}

TEST(ShipAhoyEngine, ResonancePeakLandsOnTheFrequencyKnob)
{
    // lp 24 dB at 95 % resonance: the response peak within 2 % below the knob value
    for (const float sampleRate : {44100.0f, 48000.0f, 96000.0f})
    {
        for (const float frequency : {300.0f, 2500.0f})
        {
            double best = 0.0;
            double bestFrequency = 0.0;
            for (double f = 0.9 * frequency; f < 1.05 * frequency; f *= 1.0025)
            {
                ClockedEngine pedal;
                pedal.setSampleRate(sampleRate);
                pedal.setOctaveRange(0.0f);
                pedal.setFrequency(frequency);
                pedal.setResonance(95.0f);
                pedal.setFilterType(3.0f);
                pedal.reset();
                const double gain = measureSineGain([&pedal](const float x) { return processOne(pedal, x); }, f,
                                                    sampleRate, 0.001, 0.3, 0.2);
                if (gain > best)
                {
                    best = gain;
                    bestFrequency = f;
                }
            }
            EXPECT_GT(bestFrequency / frequency, 0.975) << sampleRate << " " << frequency;
            EXPECT_LT(bestFrequency / frequency, 1.003) << sampleRate << " " << frequency;
        }
    }
}

TEST(ShipAhoyEngine, ZeroOctaveRangeHoldsTheFrequencyAtTheCentre)
{
    ClockedEngine pedal;
    pedal.setOctaveRange(0.0f);
    pedal.setFrequency(440.0f);
    pedal.setSpeed(16.0f);
    pedal.reset();
    for (int i = 0; i < 48000 * 2; ++i)
    {
        std::ignore = processOne(pedal, 0.0f);
        ASSERT_NEAR(pedal.getPeakFrequency(), 440.0f, 440.0f * 1e-5f) << i;
    }
    EXPECT_GE(pedal.getHopCount(), 32u);
}

TEST(ShipAhoyEngine, GlideLastsTheSetFractionOfThePeriod)
{
    ClockedEngine pedal(77u);
    pedal.setSpeed(4.0f); // 12000 samples per hop
    pedal.setGlideUp(50.0f);
    pedal.setGlideDown(10.0f);
    pedal.reset();
    int checkedUp = 0;
    int checkedDown = 0;
    uint64_t hops = 0;
    bool up = false;
    int glideSamples = 0;
    for (int i = 0; i < 12000 * 40; ++i)
    {
        const float before = pedal.getPosition();
        std::ignore = processOne(pedal, 0.0f);
        if (pedal.getHopCount() != hops)
        {
            hops = pedal.getHopCount();
            up = pedal.getTarget() > before;
            glideSamples = 0;
        }
        if (glideSamples >= 0)
        {
            ++glideSamples;
            if (pedal.getPosition() == pedal.getTarget())
            {
                EXPECT_EQ(glideSamples, up ? 6000 : 1200) << "hop " << hops;
                (up ? checkedUp : checkedDown) += 1;
                glideSamples = -1;
            }
        }
    }
    EXPECT_GT(checkedUp, 5);
    EXPECT_GT(checkedDown, 5);
}

TEST(ShipAhoyEngine, GlideStartsWhereTheFrequencyIsWithoutJumps)
{
    for (const float glide : {1.0f, 100.0f})
    {
        ClockedEngine pedal(3u);
        pedal.setSpeed(7.0f);
        pedal.setGlideUp(glide);
        pedal.setGlideDown(glide);
        pedal.reset();
        std::ignore = processOne(pedal, 0.0f);
        const float maxStep = 1.0f / (glide / 100.0f * c_sampleRate / 7.0f);
        float previous = pedal.getPosition();
        for (int i = 0; i < 48000 * 5; ++i)
        {
            std::ignore = processOne(pedal, 0.0f);
            ASSERT_LE(std::abs(pedal.getPosition() - previous), maxStep * 1.001f) << glide << " " << i;
            previous = pedal.getPosition();
        }
    }
}

TEST(ShipAhoyEngine, ResonanceIsAFractionOfTheThresholdForEveryType)
{
    for (size_t type = 0; type < ShipAhoyEngine::c_filterTypeCount; ++type)
    {
        ClockedEngine pedal = fixedPedal(static_cast<float>(type), 60.0f, 2000.0f);
        std::ignore = processOne(pedal, 0.0f);
        const float a = c_poleMixingPresets[ShipAhoyEngine::c_filterTypePresets[type]].resonanceFeedback;
        const PoleMixingTuning tuning = poleMixingTuningForPeak(2000.0f, c_sampleRate);
        EXPECT_FLOAT_EQ(pedal.getFilterPole(), tuning.pole) << type;
        EXPECT_NEAR(pedal.getFilterResonance() * a, 0.6f * tuning.oscillationThreshold, 1e-4f) << type;
    }
}

TEST(ShipAhoyEngine, EveryFilterTypeAndRandomTypeStaysFinite)
{
    for (size_t filterType = 0; filterType < ShipAhoyEngine::c_filterTypeCount; ++filterType)
    {
        for (size_t randomType = 0; randomType < ClockedEngine::c_randomTypeCount; ++randomType)
        {
            ClockedEngine pedal(filterType * 10 + randomType);
            pedal.setFilterType(static_cast<float>(filterType));
            pedal.setRandomType(static_cast<float>(randomType));
            pedal.setResonance(100.0f);
            pedal.setOctaveRange(8.0f);
            pedal.setSpeed(16.0f);
            pedal.setGlideUp(1.0f);
            pedal.setGlideDown(1.0f);
            pedal.reset();
            Random random(filterType + 100 * randomType);
            float peak = 0.0f;
            for (int i = 0; i < 24000; ++i)
            {
                const float y = processOne(pedal, 2.0f * random.uniform() - 1.0f);
                ASSERT_TRUE(std::isfinite(y)) << filterType << " " << randomType;
                peak = std::max(peak, std::abs(y));
            }
            EXPECT_LT(peak, 100.0f) << filterType << " " << randomType;
        }
    }
}

TEST(ShipAhoyEngine, OutputDoesNotDependOnTheBlockPartition)
{
    constexpr size_t length = 48000 * 3;
    std::vector<float> input(length);
    Random random(9u);
    for (auto& x : input)
    {
        x = 2.0f * random.uniform() - 1.0f;
    }
    auto render = [&input](const std::vector<size_t>& blocks)
    {
        ClockedEngine pedal(42u);
        pedal.setRandomType(3.0f);
        std::vector<float> output(input.size());
        size_t position = 0;
        size_t block = 0;
        while (position < input.size())
        {
            // parameter changes at fixed sample positions, independent of the blocks
            const size_t n = std::min(blocks[block++ % blocks.size()], input.size() - position);
            for (size_t i = position; i < position + n; ++i)
            {
                if (i % 24000 == 0)
                {
                    pedal.setMix(static_cast<float>(i % 48000 == 0 ? 50 : 100));
                    pedal.setFrequency(i % 48000 == 0 ? 500.0f : 1500.0f);
                }
            }
            pedal.process(&input[position], &output[position], n);
            position += n;
        }
        return output;
    };
    // only block lengths that divide 24000 keep parameter changes on block starts
    const auto reference = render({1});
    EXPECT_EQ(render({64}), reference);
    EXPECT_EQ(render({128, 32, 960, 1, 79, 24000 - 1200}), reference);
}

TEST(ShipAhoyEngine, SameSeedSameOutputDifferentSeedDifferentOutput)
{
    auto render = [](const uint64_t seed)
    {
        ClockedEngine pedal(seed);
        std::vector<float> out(48000);
        std::vector<float> in(48000, 0.25f);
        pedal.process(in.data(), out.data(), in.size());
        return out;
    };
    EXPECT_EQ(render(1u), render(1u));
    EXPECT_NE(render(1u), render(2u));
}

TEST(ShipAhoyEngine, RandomTypesHaveTheirDistribution)
{
    {
        ClockedEngine pedal = randomPedal(RandomType::White);
        const auto s = stats(hopTargets(pedal, 20000));
        EXPECT_NEAR(s.mean, 0.5, 0.01);
        EXPECT_NEAR(s.deviation, std::sqrt(1.0 / 12.0), 0.01);
        EXPECT_NEAR(s.lagOneCorrelation, 0.0, 0.03);
    }
    {
        ClockedEngine pedal = randomPedal(RandomType::Toggle);
        const auto targets = hopTargets(pedal, 2000);
        for (size_t i = 1; i < targets.size(); ++i)
        {
            const bool high = targets[i] >= 0.6f;
            const bool low = targets[i] <= 0.4f;
            ASSERT_TRUE(high != low) << i;
            ASSERT_NE(high, targets[i - 1] >= 0.6f) << i;
        }
    }
    {
        ClockedEngine pedal = randomPedal(RandomType::Violet);
        EXPECT_NEAR(stats(hopTargets(pedal, 20000)).mean, 0.125, 0.01);
    }
    {
        ClockedEngine pedal = randomPedal(RandomType::Gauss);
        const auto s = stats(hopTargets(pedal, 20000));
        EXPECT_NEAR(s.mean, 0.5, 0.01);
        EXPECT_NEAR(s.deviation, 1.0 / 6.0, 0.01);
    }
}

TEST(ShipAhoyEngine, BrownWalkWandersAroundTheCentreAndUsesTheRange)
{
    ClockedEngine pedal = randomPedal(RandomType::Brown);
    const auto targets = hopTargets(pedal, 40000);
    const auto s = stats(targets);
    EXPECT_NEAR(s.mean, 0.5, 0.03);
    EXPECT_NEAR(s.deviation, ClockedEngine::c_brownDeviation, 0.02);
    EXPECT_NEAR(s.lagOneCorrelation, std::exp(-0.1), 0.02);
    const auto clamped =
        std::count_if(targets.begin(), targets.end(), [](const float t) { return t <= 0.0f || t >= 1.0f; });
    EXPECT_LT(static_cast<double>(clamped) / static_cast<double>(targets.size()), 0.03);
    auto sorted = targets;
    std::sort(sorted.begin(), sorted.end());
    const float p05 = sorted[sorted.size() / 20];
    const float p95 = sorted[sorted.size() - sorted.size() / 20];
    EXPECT_GT(p95 - p05, 0.6f);
    // the first hops start near the centre, not at an edge
    ClockedEngine fresh = randomPedal(RandomType::Brown);
    EXPECT_NEAR(hopTargets(fresh, 1).front(), 0.5f, 0.3f);
}

namespace
{
/// An engine without a hop clock, at 1 kHz centre and the given range, for script mode tests.
ClockedEngine plainEngine(const float octaveRange = 0.0f)
{
    ClockedEngine engine;
    engine.setSampleRate(c_sampleRate);
    engine.setOctaveRange(octaveRange);
    engine.setFrequency(1000.0f);
    engine.setResonance(0.0f);
    engine.setMix(100.0f);
    engine.reset();
    return engine;
}

void run(ShipAhoyEngine& engine, const size_t samples)
{
    for (size_t i = 0; i < samples; ++i)
    {
        const float in = 0.0f;
        float out = 0.0f;
        engine.process(&in, &out, 1);
    }
}

float noteOf(const float hz)
{
    return 69.0f + 12.0f * std::log2(hz / 440.0f);
}
}

TEST(ShipAhoyEngineScriptMode, NoteIsAbsoluteMidiAndTheGlideIsLinearInSemitones)
{
    ClockedEngine engine = plainEngine();
    run(engine, 10);
    const float start = noteOf(engine.getPeakFrequency());
    engine.cutoff(81.0f, 1000);
    run(engine, 500);
    EXPECT_NEAR(engine.getNote(), start + (81.0f - start) * 0.5f, 1e-3f);
    EXPECT_NEAR(engine.getPeakFrequency(), noteToFrequency(engine.getNote()), engine.getPeakFrequency() * 1e-4f);
    run(engine, 500);
    EXPECT_EQ(engine.getNote(), 81.0f);
    EXPECT_NEAR(engine.getPeakFrequency(), 880.0f, 0.1f);
}

TEST(ShipAhoyEngineScriptMode, ZeroGlideJumpsAtOnceAndAnInterruptedGlideContinuesFromWhereItIs)
{
    ClockedEngine engine = plainEngine();
    engine.cutoff(60.0f, 0);
    run(engine, 2);
    EXPECT_NEAR(engine.getPeakFrequency(), noteToFrequency(60.0f), 1e-2f);
    engine.cutoff(72.0f, 1000);
    run(engine, 250);
    const float middle = engine.getNote();
    EXPECT_NEAR(middle, 63.0f, 1e-3f);
    engine.cutoff(48.0f, 500);
    EXPECT_EQ(engine.getNote(), middle) << "no jump when retargeting";
    run(engine, 500);
    EXPECT_EQ(engine.getNote(), 48.0f);
}

TEST(ShipAhoyEngineScriptMode, NotesBeyondTheSupportedRangeAreClamped)
{
    ClockedEngine engine = plainEngine();
    engine.cutoff(127.0f, 0);
    run(engine, 2);
    EXPECT_FLOAT_EQ(engine.getPeakFrequency(), ShipAhoyEngine::c_maxFrequencyRatio * c_sampleRate);
    engine.cutoff(0.0f, 0);
    run(engine, 2);
    EXPECT_FLOAT_EQ(engine.getPeakFrequency(), ShipAhoyEngine::c_minFrequency);
}

namespace
{
ClockedEngine driftEngine(const float depth, const float rate, const uint64_t seed = 5u)
{
    ClockedEngine engine(seed);
    engine.setSampleRate(c_sampleRate);
    engine.setOctaveRange(0.0f);
    engine.setFrequency(1000.0f);
    engine.setResonance(0.0f);
    engine.setModDepth(depth);
    engine.setModRate(rate);
    engine.reset();
    return engine;
}

/// The drift in semitones, one value per sample.
std::vector<float> driftOf(ShipAhoyEngine& engine, const size_t samples)
{
    std::vector<float> values(samples);
    for (auto& value : values)
    {
        const float in = 0.0f;
        float out = 0.0f;
        engine.process(&in, &out, 1);
        value = engine.getModulation();
    }
    return values;
}
}

TEST(ShipAhoyEngineDrift, DepthZeroIsOffWhateverTheRate)
{
    Random random(3u);
    std::vector<float> input(20000);
    for (auto& x : input)
    {
        x = 2.0f * random.uniform() - 1.0f;
    }
    auto render = [&input](const float rate)
    {
        ClockedEngine engine = driftEngine(0.0f, rate);
        std::vector<float> output(input.size());
        engine.process(input.data(), output.data(), input.size());
        EXPECT_EQ(engine.getModulation(), 0.0f);
        return output;
    };
    EXPECT_EQ(render(0.05f), render(20.0f));
}

TEST(ShipAhoyEngineDrift, DepthIsTheStandardDeviationInSemitones)
{
    constexpr float depth = 3.0f;
    ClockedEngine engine = driftEngine(depth, 5.0f);
    run(engine, 48000); // depth smoother settled (it starts snapped) and the walk started
    const std::vector<float> drift = driftOf(engine, 48000 * 60);
    double sum = 0.0;
    double sumSquares = 0.0;
    for (const float x : drift)
    {
        sum += x;
        sumSquares += static_cast<double>(x) * x;
    }
    const double mean = sum / static_cast<double>(drift.size());
    const double deviation = std::sqrt(sumSquares / static_cast<double>(drift.size()) - mean * mean);
    EXPECT_NEAR(mean, 0.0, 0.15 * depth);
    EXPECT_NEAR(deviation, depth, 0.08 * depth);
}

TEST(ShipAhoyEngineDrift, RateIsTheCornerFrequency)
{
    // OU autocorrelation exp(-2 pi fc t): at t = 1 / (2 pi fc) it is 1 / e
    for (const float rate : {0.5f, 2.0f, 8.0f})
    {
        ClockedEngine engine = driftEngine(2.0f, rate);
        const std::vector<float> drift = driftOf(engine, 48000 * 120);
        const auto lag = static_cast<size_t>(c_sampleRate / (2.0f * 3.14159265f * rate));
        double product = 0.0;
        double power = 0.0;
        for (size_t i = 0; i + lag < drift.size(); ++i)
        {
            product += static_cast<double>(drift[i]) * drift[i + lag];
            power += static_cast<double>(drift[i]) * drift[i];
        }
        EXPECT_NEAR(product / power, std::exp(-1.0), 0.1) << "rate " << rate;
    }
}

TEST(ShipAhoyEngineDrift, SameSeedRepeatsAndResetRestartsIt)
{
    ClockedEngine first = driftEngine(2.0f, 4.0f, 11u);
    ClockedEngine second = driftEngine(2.0f, 4.0f, 11u);
    ClockedEngine other = driftEngine(2.0f, 4.0f, 12u);
    const auto reference = driftOf(first, 20000);
    EXPECT_EQ(driftOf(second, 20000), reference);
    EXPECT_NE(driftOf(other, 20000), reference);
    first.reset();
    EXPECT_EQ(driftOf(first, 20000), reference);
}

TEST(ShipAhoyEngineDrift, OutputDoesNotDependOnTheBlockPartition)
{
    constexpr size_t length = 48000;
    std::vector<float> input(length);
    Random random(9u);
    for (auto& x : input)
    {
        x = 2.0f * random.uniform() - 1.0f;
    }
    auto render = [&input](const size_t block)
    {
        ClockedEngine engine = driftEngine(6.0f, 6.0f, 21u);
        std::vector<float> output(input.size());
        for (size_t position = 0; position < input.size(); position += block)
        {
            const size_t n = std::min(block, input.size() - position);
            engine.process(&input[position], &output[position], n);
        }
        return output;
    };
    const auto reference = render(1);
    for (const size_t block : {7u, 16u, 128u, 4096u})
    {
        EXPECT_EQ(render(block), reference) << "block " << block;
    }
}

TEST(ShipAhoyEngineDrift, MovesTheFrequencyInBothModes)
{
    ClockedEngine knobs = driftEngine(4.0f, 8.0f);
    float low = 1e9f;
    float high = 0.0f;
    for (size_t i = 0; i < 48000 * 10; ++i)
    {
        run(knobs, 1);
        low = std::min(low, knobs.getPeakFrequency());
        high = std::max(high, knobs.getPeakFrequency());
        EXPECT_NEAR(knobs.getPeakFrequency(), 1000.0f * std::exp2(knobs.getModulation() / 12.0f), 1.0f);
    }
    EXPECT_LT(low, 800.0f);
    EXPECT_GT(high, 1250.0f);

    ClockedEngine script = driftEngine(4.0f, 8.0f);
    script.cutoff(81.0f, 1000);
    run(script, 1000);
    EXPECT_EQ(script.getNote(), 81.0f);
    for (size_t i = 0; i < 48000; ++i)
    {
        run(script, 1);
        const float expected = noteToFrequency(81.0f + script.getModulation());
        EXPECT_NEAR(script.getPeakFrequency(), expected, expected * 1e-4f);
    }
}

TEST(ShipAhoyEngineDrift, DepthBackToZeroReturnsTheFilterToTheUndriftedFrequency)
{
    ClockedEngine engine = driftEngine(6.0f, 5.0f);
    run(engine, 48000);
    ASSERT_NE(engine.getModulation(), 0.0f);
    engine.setModDepth(0.0f);
    run(engine, 48000);
    EXPECT_EQ(engine.getModulation(), 0.0f);
    EXPECT_NEAR(engine.getPeakFrequency(), 1000.0f, 0.01f);
}

TEST(ShipAhoyEngineDrift, DepthChangesAreSmoothedAndTheFrequencyStaysInsideTheClamp)
{
    // slowest rate: the steps measured are the depth smoother's, not the drift's own motion
    ClockedEngine engine = driftEngine(0.0f, ShipAhoyEngine::c_minModRate);
    run(engine, 4800);
    engine.setModDepth(12.0f);
    float previous = 0.0f;
    float largestStep = 0.0f;
    for (size_t i = 0; i < 48000 * 5; ++i)
    {
        run(engine, 1);
        largestStep = std::max(largestStep, std::abs(engine.getModulation() - previous));
        previous = engine.getModulation();
        EXPECT_GE(engine.getPeakFrequency(), ShipAhoyEngine::c_minFrequency);
        EXPECT_LE(engine.getPeakFrequency(), ShipAhoyEngine::c_maxFrequencyRatio * c_sampleRate);
    }
    EXPECT_LT(largestStep, 0.06f); // semitones per sample, no jump
}
}
