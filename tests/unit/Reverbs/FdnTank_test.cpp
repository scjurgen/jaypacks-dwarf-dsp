#include <algorithm>
#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <iostream>
#include <vector>

#include "tests/unit/TestHelpers.h"
#include "dsp/Numbers/PrimeDispatcher.h"
#include "dsp/Numbers/Random.h"
#include "dsp/BlockProcessors/BlockProcVibrato.h"
#include "dsp/Reverbs/FdnTank.h"

namespace jp::test
{

namespace
{
constexpr size_t c_order{16};
constexpr size_t c_block{16};
constexpr float c_fs{48000.0f};
using Tank = FdnTank<c_order, c_block>;

struct Stereo
{
    std::vector<float> left;
    std::vector<float> right;
};

/// Runs the tank for numBlocks blocks; input(n) gives the input sample n.
template <typename TankType, typename Input>
Stereo run(TankType& tank, const size_t numBlocks, Input&& input)
{
    constexpr size_t block = c_block;
    Stereo out;
    out.left.reserve(numBlocks * block);
    out.right.reserve(numBlocks * block);
    std::array<float, block> in{};
    std::array<float, block> l{};
    std::array<float, block> r{};
    for (size_t b = 0; b < numBlocks; ++b)
    {
        for (size_t i = 0; i < block; ++i)
        {
            in[i] = input(b * block + i);
        }
        tank.processBlockSplit(in.data(), l.data(), r.data());
        out.left.insert(out.left.end(), l.begin(), l.end());
        out.right.insert(out.right.end(), r.begin(), r.end());
    }
    return out;
}

[[nodiscard]] double rms(const std::vector<float>& x, const size_t from, const size_t count)
{
    double sum = 0.0;
    for (size_t i = from; i < from + count; ++i)
    {
        sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    }
    return std::sqrt(sum / static_cast<double>(count));
}

/// T60 in seconds from the energy decay curve (Schroeder backward integration of L^2 + R^2),
/// fitted by least squares between -5 and -35 dB.
[[nodiscard]] double measureT60(const Stereo& response)
{
    const size_t n = response.left.size();
    std::vector<double> edc(n);
    double sum = 0.0;
    for (size_t i = n; i-- > 0;)
    {
        sum += static_cast<double>(response.left[i]) * response.left[i] +
               static_cast<double>(response.right[i]) * response.right[i];
        edc[i] = sum;
    }
    double sx = 0.0;
    double sy = 0.0;
    double sxx = 0.0;
    double sxy = 0.0;
    double count = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double db = 10.0 * std::log10(edc[i] / edc[0]);
        if (db <= -5.0 && db >= -35.0)
        {
            const double t = static_cast<double>(i) / c_fs;
            sx += t;
            sy += db;
            sxx += t * t;
            sxy += t * db;
            count += 1.0;
        }
    }
    const double slope = (count * sxy - sx * sy) / (count * sxx - sx * sx);
    return -60.0 / slope;
}
}

TEST(FdnTank, LosslessAtInfiniteDecay)
{
    Tank tank(c_fs);
    tank.setMinSize(5.0f);
    tank.setMaxSize(15.0f);
    tank.setDecay(Tank::c_infiniteDecayMs);
    Random random(11);
    const auto out = run(tank, 48000 * 12 / c_block, [&](const size_t n)
                         { return n < 4800 ? (random.uniform() - 0.5f) / static_cast<float>(c_order) : 0.0f; });
    const double early = rms(out.left, 48000, 48000);
    const double late = rms(out.left, 48000 * 11, 48000);
    std::cout << "[ fdn      ] lossless: level change from 1 s to 11 s " << toDb(late / early) << " dB\n";
    EXPECT_NEAR(toDb(late / early), 0.0, 0.5);
}

TEST(FdnTank, DecayTimeMatchesTheSetting)
{
    for (const float seconds : {0.5f, 2.0f, 5.0f})
    {
        Tank tank(c_fs);
        tank.setMinSize(8.0f);
        tank.setMaxSize(30.0f);
        tank.setDecay(seconds * 1000.0f);
        const auto blocks = static_cast<size_t>(seconds * 1.5f * c_fs) / c_block;
        const auto out = run(tank, blocks, [](const size_t n) { return n == 0 ? 1.0f : 0.0f; });
        const double t60 = measureT60(out);
        std::cout << "[ fdn      ] T60 set " << seconds << " s, measured " << t60 << " s\n";
        EXPECT_NEAR(t60, seconds, 0.05 * seconds);
    }
}

TEST(FdnTank, LoopLengthIsTheLineLength)
{
    // four equal lines and an impulse: the first pass reaches every line, the Hadamard mix of
    // [1 1 1 1] feeds only line 0 (-2 after the 1/2 gain), which arrives one length later
    FdnTank<4, c_block> tank(c_fs);
    tank.setDecay(FdnTank<4, c_block>::c_infiniteDecayMs);
    constexpr size_t length = 997;
    for (size_t o = 0; o < 4; ++o)
    {
        tank.setDirectSize(o, length);
    }
    const auto out = run(tank, 3 * length / c_block, [](const size_t n) { return n == 0 ? 1.0f : 0.0f; });
    const auto tapsLine0 = tank.getItdTaps(0);
    const size_t second = 2 * length - 2 * c_block - tapsLine0[0];
    size_t lastFirstPass = 0;
    for (size_t o = 0; o < 4; ++o)
    {
        lastFirstPass = std::max(lastFirstPass, length - 2 * c_block - tank.getItdTaps(o)[0]);
    }
    for (size_t n = lastFirstPass + 1; n < second; ++n)
    {
        ASSERT_EQ(out.left[n], 0.0f) << "at " << n;
    }
    EXPECT_FLOAT_EQ(out.left[second], -2.0f);
}

TEST(FdnTank, CallbackLatencyKeepsTheLoopLength)
{
    // as LoopLengthIsTheLineLength, with an exact delay of N samples (a vibrato at depth 0)
    // on line 0 that states N as its latency: the loop stays 997 samples, so the recirculated
    // -2 arrives as before, only the taps of line 0 read it N samples earlier
    FdnTank<4, c_block> tank(c_fs);
    tank.setDecay(FdnTank<4, c_block>::c_infiniteDecayMs);
    constexpr size_t length = 997;
    for (size_t o = 0; o < 4; ++o)
    {
        tank.setDirectSize(o, length);
    }
    BlockVibrato<c_block> delay(c_fs, 0.002f);
    delay.setRate(0.5f);
    delay.setDepthCents(0.0f);
    delay.reset();
    const size_t latency = delay.getLatency();
    tank.setDelayCallback(0, &delay, latency);
    EXPECT_EQ(tank.getDelayWidth(0), length - 2 * c_block - latency);
    EXPECT_EQ(tank.getDelayWidth(1), length - 2 * c_block);
    const auto out = run(tank, 3 * length / c_block, [](const size_t n) { return n == 0 ? 1.0f : 0.0f; });
    const size_t second = 2 * length - 2 * c_block - tank.getItdTaps(0)[0] - latency;
    EXPECT_FLOAT_EQ(out.left[second], -2.0f);
    for (size_t n = length; n < second; ++n)
    {
        ASSERT_EQ(out.left[n], 0.0f) << "at " << n;
    }
    tank.removeDelayCallback(0);
    EXPECT_EQ(tank.getDelayWidth(0), length - 2 * c_block);
}

TEST(FdnTank, CallbackLatencySurvivesSizeChanges)
{
    Tank tank(c_fs);
    tank.setMinSize(8.0f);
    tank.setMaxSize(20.0f);
    BlockVibrato<c_block> vibrato(c_fs, 0.004f);
    tank.setDelayCallback(4, &vibrato, vibrato.getLatency());
    static_cast<void>(run(tank, 4, [](const size_t) { return 0.0f; }));
    EXPECT_EQ(tank.getDelayWidth(4), tank.getLineLength(4) - 2 * c_block - vibrato.getLatency());
    tank.setMinSize(11.0f);
    static_cast<void>(run(tank, 48000 / c_block, [](const size_t) { return 0.0f; }));
    EXPECT_FALSE(tank.isFading());
    EXPECT_EQ(tank.getDelayWidth(4), tank.getLineLength(4) - 2 * c_block - vibrato.getLatency());
}

TEST(FdnTank, ModulatedLinesKeepTheDecayTime)
{
    // two lines with a vibrato at 20 cents (as Eigenghost's lines 4 and 5)
    for (const float seconds : {0.5f, 2.0f})
    {
        Tank tank(c_fs);
        tank.setMinSize(8.0f);
        tank.setMaxSize(30.0f);
        tank.setDecay(seconds * 1000.0f);
        std::array<BlockVibrato<c_block>, 2> vibratos{BlockVibrato<c_block>(c_fs, 0.004f),
                                                      BlockVibrato<c_block>(c_fs, 0.004f)};
        for (size_t i = 0; i < 2; ++i)
        {
            vibratos[i].setRate(0.5f);
            vibratos[i].setPhase(0.25f * static_cast<float>(i));
            vibratos[i].setDepthCents(20.0f);
            vibratos[i].reset();
            tank.setDelayCallback(4 + i, &vibratos[i], vibratos[i].getLatency());
        }
        const auto blocks = static_cast<size_t>(seconds * 1.5f * c_fs) / c_block;
        const auto out = run(tank, blocks, [](const size_t n) { return n == 0 ? 1.0f : 0.0f; });
        const double t60 = measureT60(out);
        std::cout << "[ fdn      ] T60 set " << seconds << " s with two modulated lines, measured " << t60 << " s\n";
        EXPECT_NEAR(t60, seconds, 0.05 * seconds);
    }
}

TEST(FdnTank, ModulatedLinesStayNearlyLossless)
{
    // infinite decay, two lines with the allpass vibrato: the level holds within 0.5 dB over
    // 10 s (measured -0.18 dB at 4 cents, -0.41 dB at 20 cents; the time-varying allpass
    // coefficient is the only loss), so the modulation shortens even a 30 s decay by under 1 %
    for (const float cents : {4.0f, 20.0f})
    {
        Tank tank(c_fs);
        tank.setMinSize(5.0f);
        tank.setMaxSize(15.0f);
        tank.setDecay(Tank::c_infiniteDecayMs);
        std::array<BlockVibrato<c_block>, 2> vibratos{BlockVibrato<c_block>(c_fs, 0.004f),
                                                      BlockVibrato<c_block>(c_fs, 0.004f)};
        for (size_t i = 0; i < 2; ++i)
        {
            vibratos[i].setRate(0.5f);
            vibratos[i].setPhase(0.25f * static_cast<float>(i));
            vibratos[i].setDepthCents(cents);
            vibratos[i].reset();
            tank.setDelayCallback(4 + i, &vibratos[i], vibratos[i].getLatency());
        }
        Random random(11);
        const auto out = run(tank, 48000 * 12 / c_block, [&](const size_t n)
                             { return n < 4800 ? (random.uniform() - 0.5f) / static_cast<float>(c_order) : 0.0f; });
        const double change = toDb(rms(out.left, 48000 * 11, 48000) / rms(out.left, 48000, 48000));
        std::cout << "[ fdn      ] lossless with two lines at " << cents << " cents: level change from 1 s to 11 s "
                  << change << " dB\n";
        EXPECT_NEAR(change, 0.0, 0.5) << cents;
    }
}

TEST(FdnTank, LengthsFromMetres)
{
    Tank tank(c_fs);
    tank.setMinSize(5.0f);
    tank.setMaxSize(15.0f);
    tank.setSpreadBulge(0.0f);
    tank.reset();
    std::array<float, c_block> silence{};
    std::array<float, c_block> l{};
    std::array<float, c_block> r{};
    tank.processBlockSplit(silence.data(), l.data(), r.data());
    // 5 m = 720 samples at 333.3 m/s, next prime 727; 15 m = 2160 samples, next prime 2161
    EXPECT_EQ(tank.getLineLength(0), 727u);
    EXPECT_EQ(tank.getLineLength(c_order - 1), 2161u);
    for (size_t o = 0; o < c_order; ++o)
    {
        EXPECT_TRUE(isPrimeNumber(tank.getLineLength(o))) << o;
        if (o > 0)
        {
            EXPECT_GT(tank.getLineLength(o), tank.getLineLength(o - 1)) << o;
        }
    }
    EXPECT_FALSE(tank.isFading());
}

TEST(FdnTank, LengthsStayInsideTheLimits)
{
    Tank tank(c_fs);
    EXPECT_EQ(tank.getMaxLineLength(), 24000u);
    EXPECT_EQ(tank.getMinLineLength(), 2 * c_block + 16 + 1);
    tank.setMinSize(0.01f);
    tank.setMaxSize(400.0f);
    tank.reset();
    std::array<float, c_block> silence{};
    std::array<float, c_block> l{};
    std::array<float, c_block> r{};
    tank.processBlockSplit(silence.data(), l.data(), r.data());
    for (size_t o = 0; o < c_order; ++o)
    {
        EXPECT_GE(tank.getLineLength(o), tank.getMinLineLength()) << o;
        EXPECT_LE(tank.getLineLength(o), tank.getMaxLineLength()) << o;
    }
    EXPECT_EQ(tank.getLineLength(c_order - 1), 24000u);
}

TEST(FdnTank, SizeChangeFadesInsteadOfJumping)
{
    // A 237 Hz sine through the tank, then a size change. The ringing tank moves to a new
    // steady state and may swing louder on the way, so the sample step is no measure; a
    // click is broadband, so the energy above 5 kHz in the 8192 samples after the change is.
    // A hard change of the same lengths is the control.
    const auto runWithChange = [](const bool hard)
    {
        Tank tank(c_fs);
        tank.setMinSize(10.0f);
        tank.setMaxSize(30.0f);
        tank.setDecay(1500.0f);
        const double omega = 2.0 * c_pi * 237.0 / c_fs;
        const auto sine = [omega](const size_t n)
        { return static_cast<float>(0.1 * std::sin(omega * static_cast<double>(n))); };
        const Stereo first = run(tank, 4 * 48000 / c_block, sine);
        if (hard)
        {
            // the probe applies its lengths with its first block
            Tank probe(c_fs);
            probe.setMinSize(12.0f);
            probe.setMaxSize(36.0f);
            std::ignore = run(probe, 1, [](const size_t) { return 0.0f; });
            for (size_t o = 0; o < c_order; ++o)
            {
                tank.setDirectSize(o, probe.getLineLength(o));
            }
        }
        else
        {
            tank.setMinSize(12.0f);
            tank.setMaxSize(36.0f);
        }
        const size_t offset = first.left.size();
        const Stereo second = run(tank, 8192 / c_block, [&](const size_t n) { return sine(offset + n); });
        const std::vector<float> steady(first.left.end() - 8192, first.left.end());
        return std::array<double, 2>{highBandEnergyDb(steady, 5000.0, c_fs),
                                     highBandEnergyDb(second.left, 5000.0, c_fs)};
    };
    const auto faded = runWithChange(false);
    const auto hard = runWithChange(true);
    std::cout << "[ fdn      ] energy above 5 kHz: steady " << faded[0] << " dB, fade " << faded[1]
              << " dB, hard change " << hard[1] << " dB\n";
    EXPECT_LT(faded[1], -80.0);
    EXPECT_GT(hard[1], -50.0);
}

TEST(FdnTank, DecayChangesRampPerSample)
{
    // a smoothed decay sweep (one setDecay per block, as a pedal smoothing the knob does)
    // from 0.5 to 10 s and back: the line gains ramp across each block, so a 237 Hz sine
    // gains no energy above 5 kHz (-93 dB; with the gains stepped per block -55 dB)
    Tank tank(c_fs);
    tank.setMinSize(10.0f);
    tank.setMaxSize(30.0f);
    tank.setDecay(500.0f);
    const double omega = 2.0 * c_pi * 237.0 / c_fs;
    std::array<float, c_block> in{};
    std::array<float, c_block> l{};
    std::array<float, c_block> r{};
    std::vector<float> out;
    float logDecay = std::log2(0.5f);
    const float coefficient = std::exp(-static_cast<float>(c_block) / (0.02f * c_fs));
    size_t n = 0;
    for (size_t b = 0; b < 3 * 48000 / c_block; ++b)
    {
        const float target = (b / 400) % 2 == 0 ? std::log2(0.5f) : std::log2(10.0f);
        logDecay = target + (logDecay - target) * coefficient;
        tank.setDecay(1000.0f * std::exp2(logDecay));
        for (auto& x : in)
        {
            x = static_cast<float>(0.1 * std::sin(omega * static_cast<double>(n++)));
        }
        tank.processBlockSplit(in.data(), l.data(), r.data());
        out.insert(out.end(), l.begin(), l.end());
    }
    const std::vector<float> window(out.end() - 65536, out.end());
    const double energy = highBandEnergyDb(window, 5000.0, c_fs);
    std::cout << "[ fdn      ] energy above 5 kHz with a smoothed decay sweep: " << energy << " dB\n";
    EXPECT_LT(energy, -85.0);
}

TEST(FdnTank, FadeTimingGainsAndHeldChanges)
{
    Tank tank(c_fs);
    tank.setMinSize(10.0f);
    tank.setMaxSize(30.0f);
    tank.setDecay(2000.0f);
    std::array<float, c_block> silence{};
    std::array<float, c_block> l{};
    std::array<float, c_block> r{};
    tank.processBlockSplit(silence.data(), l.data(), r.data());
    const size_t firstLine0 = tank.getLineLength(0);

    tank.setMinSize(11.0f);
    tank.processBlockSplit(silence.data(), l.data(), r.data());
    ASSERT_TRUE(tank.isFading());
    const size_t fadeBlocks = 2400 / c_block;
    // two more requests during the fade: only the newest counts
    tank.setMinSize(13.0f);
    tank.setMinSize(12.0f);
    size_t blocks = 1;
    while (tank.isFading())
    {
        tank.processBlockSplit(silence.data(), l.data(), r.data());
        ++blocks;
    }
    EXPECT_EQ(blocks, fadeBlocks);
    EXPECT_GT(tank.getLineLength(0), firstLine0);

    // the held request starts the next fade with the next block
    tank.processBlockSplit(silence.data(), l.data(), r.data());
    EXPECT_TRUE(tank.isFading());
    while (tank.isFading())
    {
        tank.processBlockSplit(silence.data(), l.data(), r.data());
    }
    EXPECT_EQ(tank.getLineLength(0), tank.computeSizeFromMeters(12.0f));
    for (size_t o = 0; o < c_order; ++o)
    {
        const float expected = std::pow(0.001f, static_cast<float>(tank.getLineLength(o)) / c_fs / 2.0f) / 4.0f;
        EXPECT_NEAR(tank.getLineGain(o), expected, 1e-6f) << o;
    }
}

TEST(FdnTank, ChangesBeforeTheFirstBlockApplyAtOnce)
{
    Tank tank(c_fs);
    tank.setMinSize(3.0f);
    tank.setMaxSize(9.0f);
    std::array<float, c_block> silence{};
    std::array<float, c_block> l{};
    std::array<float, c_block> r{};
    tank.processBlockSplit(silence.data(), l.data(), r.data());
    EXPECT_FALSE(tank.isFading());
    EXPECT_EQ(tank.getLineLength(0), tank.computeSizeFromMeters(3.0f));
    tank.reset();
    tank.setMinSize(4.0f);
    tank.processBlockSplit(silence.data(), l.data(), r.data());
    EXPECT_FALSE(tank.isFading());
    EXPECT_EQ(tank.getLineLength(0), tank.computeSizeFromMeters(4.0f));
}

TEST(FdnTank, CallbacksProcessTheFeedbackOnly)
{
    // muting every line leaves the first pass (the taps read the lines before the
    // processors) and removes everything after it
    class Mute final : public BlockProcessorBase<c_block>
    {
      public:
        void process(std::array<float, c_block>& block) noexcept override
        {
            block.fill(0.0f);
        }
    };
    Mute mute;
    Tank tank(c_fs);
    tank.setMinSize(5.0f);
    tank.setMaxSize(10.0f);
    tank.setDecay(Tank::c_infiniteDecayMs);
    for (size_t o = 0; o < c_order; ++o)
    {
        tank.setDelayCallback(o, &mute);
    }
    const auto out = run(tank, 48000 / c_block, [](const size_t n) { return n == 0 ? 1.0f : 0.0f; });
    const size_t longest = tank.getLineLength(c_order - 1);
    double firstPass = 0.0;
    for (size_t n = 0; n < longest; ++n)
    {
        firstPass += std::fabs(out.left[n]);
    }
    EXPECT_NEAR(firstPass, static_cast<double>(c_order), 1e-6);
    for (size_t n = longest; n < out.left.size(); ++n)
    {
        ASSERT_EQ(out.left[n], 0.0f) << n;
    }
}

TEST(FdnTank, ResetSilences)
{
    Tank tank(c_fs);
    tank.setDecay(5000.0f);
    Random random(5);
    std::ignore = run(tank, 3000, [&](const size_t) { return random.uniform() - 0.5f; });
    tank.reset();
    const auto out = run(tank, 3000, [](const size_t) { return 0.0f; });
    for (size_t n = 0; n < out.left.size(); ++n)
    {
        ASSERT_EQ(out.left[n], 0.0f) << n;
        ASSERT_EQ(out.right[n], 0.0f) << n;
    }
}

TEST(FdnTank, SeedDecidesTheStereoTaps)
{
    Tank a(c_fs, 0.5f, 1);
    Tank b(c_fs, 0.5f, 1);
    Tank c(c_fs, 0.5f, 2);
    bool differs = false;
    for (size_t o = 0; o < c_order; ++o)
    {
        EXPECT_EQ(a.getItdTaps(o), b.getItdTaps(o));
        const auto taps = a.getItdTaps(o);
        EXPECT_TRUE(taps[0] == 0 || taps[1] == 0) << o;
        EXPECT_LE(std::max(taps[0], taps[1]), 16u) << o;
        differs = differs || a.getItdTaps(o) != c.getItdTaps(o);
    }
    EXPECT_TRUE(differs);
    Random random(9);
    std::vector<float> noise(48000);
    for (auto& x : noise)
    {
        x = random.uniform() - 0.5f;
    }
    const auto outA = run(a, 3000, [&](const size_t n) { return noise[n]; });
    const auto outB = run(b, 3000, [&](const size_t n) { return noise[n]; });
    EXPECT_EQ(outA.left, outB.left);
    EXPECT_EQ(outA.right, outB.right);
}

TEST(FdnTank, LeftAndRightDiffer)
{
    Tank tank(c_fs);
    tank.setMinSize(8.0f);
    tank.setMaxSize(30.0f);
    tank.setDecay(2000.0f);
    Random random(21);
    const auto out =
        run(tank, 48000 * 3 / c_block, [&](const size_t n) { return n < 48000 ? random.uniform() - 0.5f : 0.0f; });
    double lr = 0.0;
    double ll = 0.0;
    double rr = 0.0;
    for (size_t n = 48000; n < out.left.size(); ++n)
    {
        lr += static_cast<double>(out.left[n]) * out.right[n];
        ll += static_cast<double>(out.left[n]) * out.left[n];
        rr += static_cast<double>(out.right[n]) * out.right[n];
    }
    const double correlation = lr / std::sqrt(ll * rr);
    std::cout << "[ fdn      ] left/right correlation of the tail " << correlation << "\n";
    EXPECT_LT(correlation, 0.99);
}

}
