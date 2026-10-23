#include <algorithm>
#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <iostream>
#include <vector>

#include "tests/unit/TestHelpers.h"
#include "dsp/Numbers/Random.h"
#include "plugins/eigenghost/EigenghostPedal.h"

namespace jp::test
{

namespace
{
constexpr float c_fs{48000.0f};

struct Stereo
{
    std::vector<float> left;
    std::vector<float> right;
};

Stereo run(EigenghostPedal& pedal, const std::vector<float>& input, const size_t blockSize = 128)
{
    Stereo out{std::vector<float>(input.size()), std::vector<float>(input.size())};
    for (size_t pos = 0; pos < input.size(); pos += blockSize)
    {
        const size_t n = std::min(blockSize, input.size() - pos);
        const float* in[1] = {input.data() + pos};
        float* outs[2] = {out.left.data() + pos, out.right.data() + pos};
        pedal.process(in, outs, n);
    }
    return out;
}

std::vector<float> noise(const size_t length, const uint64_t seed = 1, const float amplitude = 0.5f)
{
    Random random(seed);
    std::vector<float> out(length);
    for (auto& x : out)
    {
        x = amplitude * (2.0f * random.uniform() - 1.0f);
    }
    return out;
}

double rms(const std::vector<float>& x, const size_t from, const size_t to)
{
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
    {
        sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    }
    return std::sqrt(sum / static_cast<double>(to - from));
}

double correlation(const std::vector<float>& a, const std::vector<float>& b, const size_t from, const size_t to)
{
    double ab = 0.0;
    double aa = 0.0;
    double bb = 0.0;
    for (size_t i = from; i < to; ++i)
    {
        ab += static_cast<double>(a[i]) * b[i];
        aa += static_cast<double>(a[i]) * a[i];
        bb += static_cast<double>(b[i]) * b[i];
    }
    return ab / std::sqrt(aa * bb);
}

/// A pedal at fs with the given settings, reset as the adapter does on activate().
template <typename Settings>
std::unique_ptr<EigenghostPedal> makePedal(Settings&& settings, const float fs = c_fs)
{
    auto pedal = std::make_unique<EigenghostPedal>();
    pedal->setSampleRate(fs);
    settings(*pedal);
    pedal->reset();
    return pedal;
}

const auto c_noSettings = [](EigenghostPedal&) {};
}

TEST(EigenghostPedal, DefaultGeometry)
{
    // the tank applies new lengths with its next block
    const auto pedal = makePedal(c_noSettings);
    std::ignore = run(*pedal, std::vector<float>(EigenghostPedal::c_blockSize, 0.0f));
    const auto& tank = pedal->getTank();
    EXPECT_EQ(tank.getLineLength(0), tank.computeSizeFromMeters(EigenghostPedal::c_defaultSize));
    EXPECT_EQ(tank.getLineLength(EigenghostPedal::c_order - 1),
              tank.computeSizeFromMeters(EigenghostPedal::c_defaultSize * EigenghostPedal::c_defaultSpread));
    EXPECT_EQ(tank.getMaxLineLength(), 24000u);
    // diffuser: 1.3 m = 187 samples for the shortest stage, 3 times that for the longest, both
    // snapped up to distinct primes
    EXPECT_EQ(pedal->getDiffuserStageSize(0), 191u);
    EXPECT_EQ(pedal->getDiffuserStageSize(EigenghostPedal::c_diffuserStages - 1), 563u);
    EXPECT_EQ(EigenghostPedal::getLatency(), 16u);
    EXPECT_EQ(pedal->getPredelaySamples(), 960u);
    // the modulated lines: swing for 4 cents at 0.5 Hz, the vibrato's latency taken off the line
    const float swing = BlockVibrato<EigenghostPedal::c_blockSize>::swingSamplesFor(4.0f, 0.5f, c_fs);
    for (size_t i = 0; i < 2; ++i)
    {
        EXPECT_NEAR(pedal->getModulationSwing(i), swing, 1e-3f) << i;
        const size_t line = EigenghostPedal::c_modulatedLines[i];
        EXPECT_LT(tank.getDelayWidth(line), tank.getLineLength(line) - 2 * EigenghostPedal::c_blockSize) << i;
    }
}

TEST(EigenghostPedal, PredelayDelaysTheWetSignalOnly)
{
    // dry muted, diffusion 0 (the allpasses are plain delays): the first wet sample of an
    // impulse moves by exactly the pre-delay
    const auto firstWet = [](const float ms)
    {
        const auto pedal = makePedal(
            [ms](EigenghostPedal& p)
            {
                p.setDry(EigenghostPedal::c_muteDb);
                p.setWet(0.0f);
                p.setDiffusion(0.0f);
                p.setShimmer(0.0f);
                p.setPredelay(ms);
            });
        std::vector<float> impulse(48000, 0.0f);
        impulse[0] = 1.0f;
        const auto out = run(*pedal, impulse);
        for (size_t n = 0; n < out.left.size(); ++n)
        {
            if (out.left[n] != 0.0f || out.right[n] != 0.0f)
            {
                return static_cast<long>(n);
            }
        }
        return -1L;
    };
    const long none = firstWet(0.0f);
    std::cout << "[ eigen    ] first wet sample: pre-delay 0 at " << none << ", 100 ms at " << firstWet(100.0f) << "\n";
    EXPECT_GT(none, 0);
    EXPECT_EQ(firstWet(100.0f) - none, 4800);
    EXPECT_EQ(firstWet(200.0f) - none, 9600);
    // the dry signal is not delayed by the pre-delay
    const auto pedal = makePedal(
        [](EigenghostPedal& p)
        {
            p.setWet(EigenghostPedal::c_muteDb);
            p.setPredelay(150.0f);
        });
    const auto input = noise(4800);
    EXPECT_EQ(run(*pedal, input).left, input);
}

TEST(EigenghostPedal, PredelayChangesFadeAndTheNewestWins)
{
    const auto pedal = makePedal(c_noSettings);
    const auto quiet = std::vector<float>(EigenghostPedal::c_blockSize, 0.0f);
    pedal->setPredelay(100.0f);
    std::ignore = run(*pedal, quiet); // starts the fade to 4800 samples
    pedal->setPredelay(50.0f);        // held: the fade runs 50 ms
    pedal->setPredelay(150.0f);       // the newest wins
    std::ignore = run(*pedal, std::vector<float>(1200, 0.0f));
    EXPECT_EQ(pedal->getPredelaySamples(), 4800u);
    std::ignore = run(*pedal, std::vector<float>(4800, 0.0f));
    EXPECT_EQ(pedal->getPredelaySamples(), 7200u);
}

TEST(EigenghostPedal, ModDepthMovesSlowly)
{
    // 500 ms smoothing: a fast depth change would sweep the read position and bend the pitch
    const auto pedal = makePedal([](EigenghostPedal& p) { p.setModDepth(0.0f); });
    EXPECT_EQ(pedal->getModulationSwing(0), 0.0f);
    pedal->setModDepth(20.0f);
    const float target = BlockVibrato<EigenghostPedal::c_blockSize>::swingSamplesFor(20.0f, 0.5f, c_fs);
    std::ignore = run(*pedal, std::vector<float>(4800, 0.0f));
    const float after100ms = pedal->getModulationSwing(0);
    std::ignore = run(*pedal, std::vector<float>(48000 * 4, 0.0f));
    std::cout << "[ eigen    ] mod depth 0 -> 20 cents: swing after 100 ms " << after100ms << ", after 4.1 s "
              << pedal->getModulationSwing(0) << " of " << target << " samples\n";
    EXPECT_LT(after100ms, 0.25f * target);
    EXPECT_NEAR(pedal->getModulationSwing(0), target, 1e-3f * target);
}

TEST(EigenghostPedal, WetMutedGivesExactlyTheDry)
{
    const auto pedal = makePedal([](EigenghostPedal& p) { p.setWet(EigenghostPedal::c_muteDb); });
    const auto input = noise(48000);
    const auto out = run(*pedal, input);
    EXPECT_EQ(out.left, input);
    EXPECT_EQ(out.right, input);
}

TEST(EigenghostPedal, DryGainFollowsItsDecibels)
{
    const auto pedal = makePedal(
        [](EigenghostPedal& p)
        {
            p.setWet(EigenghostPedal::c_muteDb);
            p.setDry(-6.0f);
        });
    const auto input = noise(4800);
    const auto out = run(*pedal, input);
    const float gain = std::pow(10.0f, -6.0f / 20.0f);
    for (size_t i = 0; i < input.size(); ++i)
    {
        ASSERT_FLOAT_EQ(out.left[i], gain * input[i]) << i;
    }
}

TEST(EigenghostPedal, DryMutedLeavesOnlyTheReverb)
{
    // an impulse: nothing comes out before the wet latency plus the shortest line's tap
    const auto pedal = makePedal([](EigenghostPedal& p) { p.setDry(EigenghostPedal::c_muteDb); });
    std::vector<float> input(48000, 0.0f);
    input[0] = 1.0f;
    const auto out = run(*pedal, input);
    const auto& tank = pedal->getTank();
    size_t earliest = input.size();
    for (size_t o = 0; o < EigenghostPedal::c_order; ++o)
    {
        const auto taps = tank.getItdTaps(o);
        earliest =
            std::min(earliest, tank.getLineLength(o) - 2 * EigenghostPedal::c_blockSize - std::max(taps[0], taps[1]));
    }
    earliest += EigenghostPedal::getLatency();
    size_t first = input.size();
    for (size_t i = 0; i < input.size(); ++i)
    {
        if (out.left[i] != 0.0f || out.right[i] != 0.0f)
        {
            first = i;
            break;
        }
    }
    std::cout << "[ eigen    ] first wet sample at " << first << " (bound " << earliest << ")\n";
    EXPECT_GE(first, earliest);
    EXPECT_LT(first, input.size());
}

TEST(EigenghostPedal, WetLevelGrowsWithTheDecay)
{
    // noise at 0 dB wet, dry muted: the first pass carries the input energy, the
    // reverberant part adds with the decay
    std::array<double, 3> levels{};
    const std::array<float, 3> decays{0.5f, 3.0f, 10.0f};
    const auto input = noise(48000 * 6, 3);
    for (size_t k = 0; k < decays.size(); ++k)
    {
        const float decay = decays[k];
        const auto pedal = makePedal(
            [decay](EigenghostPedal& p)
            {
                p.setDry(EigenghostPedal::c_muteDb);
                p.setWet(0.0f);
                p.setShimmer(0.0f);
                p.setDecay(decay);
            });
        const auto out = run(*pedal, input);
        levels[k] = toDb(rms(out.left, 48000 * 4, input.size()) / rms(input, 48000 * 4, input.size()));
        std::cout << "[ eigen    ] wet level for noise, decay " << decay << " s: " << levels[k] << " dB\n";
    }
    EXPECT_LT(levels[0], levels[1]);
    EXPECT_LT(levels[1], levels[2]);
    EXPECT_NEAR(levels[1], 5.0, 3.0);
}

TEST(EigenghostPedal, WidthSetsTheStereoImage)
{
    const auto input = noise(48000 * 3, 5);
    std::array<double, 3> correlations{};
    const std::array<float, 3> widths{0.0f, 100.0f, 150.0f};
    for (size_t k = 0; k < widths.size(); ++k)
    {
        const float width = widths[k];
        const auto pedal = makePedal(
            [width](EigenghostPedal& p)
            {
                p.setDry(EigenghostPedal::c_muteDb);
                p.setWet(0.0f);
                p.setWidth(width);
            });
        const auto out = run(*pedal, input);
        correlations[k] = correlation(out.left, out.right, 48000, input.size());
        std::cout << "[ eigen    ] width " << width << " %: left/right correlation " << correlations[k] << "\n";
        if (width == 0.0f)
        {
            EXPECT_EQ(out.left, out.right);
        }
    }
    EXPECT_LT(correlations[1], 0.5);
    EXPECT_LT(correlations[2], correlations[1]);
}

TEST(EigenghostPedal, ShimmerLoopStaysBounded)
{
    // the longest decay with fully shifted pitch lines, both an octave up, no damping:
    // continuous noise must settle to a plateau, not grow
    const auto pedal = makePedal(
        [](EigenghostPedal& p)
        {
            p.setDry(EigenghostPedal::c_muteDb);
            p.setWet(0.0f);
            p.setDecay(30.0f);
            p.setShimmer(100.0f);
            p.setPitch1(12.0f);
            p.setPitch2(12.0f);
            p.setDamping(20000.0f);
            p.setLowCut(10.0f);
        });
    const auto input = noise(48000 * 40, 7);
    const auto out = run(*pedal, input);
    float peak = 0.0f;
    for (const float y : out.left)
    {
        peak = std::max(peak, std::fabs(y));
    }
    const double second20 = toDb(rms(out.left, 48000 * 20, 48000 * 30));
    const double second30 = toDb(rms(out.left, 48000 * 30, 48000 * 40));
    std::cout << "[ eigen    ] shimmer at 30 s decay: " << second20 << " dB rms (20-30 s), " << second30
              << " dB rms (30-40 s), peak " << peak << "\n";
    EXPECT_LT(second30 - second20, 0.5);
    EXPECT_LT(peak, 20.0f);
}

TEST(EigenghostPedal, OutputDoesNotDependOnTheBlockSize)
{
    const auto input = noise(48000);
    const auto settings = [](EigenghostPedal& p)
    {
        p.setShimmer(70.0f);
        p.setPredelay(37.0f);
        p.setModDepth(15.0f);
    };
    const auto reference = run(*makePedal(settings), input, 1);
    for (const size_t blockSize : {7u, 16u, 128u, 1000u})
    {
        const auto out = run(*makePedal(settings), input, blockSize);
        EXPECT_EQ(out.left, reference.left) << blockSize;
        EXPECT_EQ(out.right, reference.right) << blockSize;
    }
}

TEST(EigenghostPedal, InPlaceEqualsSeparateBuffers)
{
    const auto input = noise(24000);
    const auto separate = run(*makePedal(c_noSettings), input);
    auto pedal = makePedal(c_noSettings);
    std::vector<float> shared = input;
    std::vector<float> right(input.size());
    for (size_t pos = 0; pos < input.size(); pos += 128)
    {
        const size_t n = std::min<size_t>(128, input.size() - pos);
        const float* in[1] = {shared.data() + pos};
        float* outs[2] = {shared.data() + pos, right.data() + pos};
        pedal->process(in, outs, n);
    }
    EXPECT_EQ(shared, separate.left);
    EXPECT_EQ(right, separate.right);
}

TEST(EigenghostPedal, ResetSilences)
{
    auto pedal = makePedal(c_noSettings);
    std::ignore = run(*pedal, noise(48000, 9, 1.0f));
    pedal->reset();
    const auto out = run(*pedal, std::vector<float>(48000, 0.0f));
    for (size_t i = 0; i < out.left.size(); ++i)
    {
        ASSERT_EQ(out.left[i], 0.0f) << i;
        ASSERT_EQ(out.right[i], 0.0f) << i;
    }
}

TEST(EigenghostPedal, SampleRateSetsTheLineCeiling)
{
    for (const float fs : {44100.0f, 96000.0f})
    {
        auto pedal = makePedal([](EigenghostPedal& p) { p.setSize(40.0f); }, fs);
        EXPECT_EQ(pedal->getTank().getMaxLineLength(), static_cast<size_t>(fs / 2.0f)) << fs;
        const auto out = run(*pedal, noise(static_cast<size_t>(fs)));
        for (const float y : out.left)
        {
            ASSERT_TRUE(std::isfinite(y)) << fs;
        }
    }
}

TEST(EigenghostPedal, SizeChangeFades)
{
    auto pedal = makePedal(c_noSettings);
    std::ignore = run(*pedal, noise(4800));
    const size_t before = pedal->getTank().getLineLength(0);
    pedal->setSize(20.0f);
    std::ignore = run(*pedal, noise(32));
    EXPECT_TRUE(pedal->getTank().isFading());
    std::ignore = run(*pedal, noise(4800));
    EXPECT_FALSE(pedal->getTank().isFading());
    EXPECT_GT(pedal->getTank().getLineLength(0), before);
    EXPECT_EQ(pedal->getTank().getLineLength(0), pedal->getTank().computeSizeFromMeters(20.0f));
}

}
