// Integration tests of eigenghost through the built LV2 binary (<build>/lv2/jaypacks-eigenghost.lv2):
// the TTL agrees with the pedal's constants, and what a host sets on the ports comes out of
// the audio (left output; the host harness returns the first output channel). Clicks are
// judged as energy above 5 kHz on a 237 Hz sine (a reverb tank swings to a new steady state
// after a change, so the largest sample step is no measure), against a deliberate step as
// the control.

#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "Lv2Host.h"
#include "dsp/Analysis/SignalStats.h"
#include "tests/unit/TestHelpers.h"
#include "plugins/eigenghost/EigenghostPedal.h"

namespace jp::host
{

namespace
{
constexpr float c_fs{48000.0f};
using Settings = std::map<std::string, float>;

std::vector<float> sine(const double frequency, const double amplitude, const size_t length)
{
    std::vector<float> out(length);
    for (size_t i = 0; i < length; ++i)
    {
        out[i] = static_cast<float>(amplitude * std::sin(2.0 * test::c_pi * frequency * static_cast<double>(i) / c_fs));
    }
    return out;
}

/// T60 in seconds from the energy decay curve of a response, least squares between -5 and -35 dB.
double measureT60(const std::vector<float>& response)
{
    std::vector<double> edc(response.size());
    double sum = 0.0;
    for (size_t i = response.size(); i-- > 0;)
    {
        sum += static_cast<double>(response[i]) * response[i];
        edc[i] = sum;
    }
    double sx = 0.0;
    double sy = 0.0;
    double sxx = 0.0;
    double sxy = 0.0;
    double count = 0.0;
    for (size_t i = 0; i < edc.size(); ++i)
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
    return -60.0 * (count * sxx - sx * sx) / (count * sxy - sx * sy);
}

std::vector<float> slice(const std::vector<float>& x, const size_t from, const size_t count)
{
    return {x.begin() + static_cast<long>(from), x.begin() + static_cast<long>(from + count)};
}
}

class Eigenghost : public ::testing::Test
{
  protected:
    static void SetUpTestSuite()
    {
        s_bundle = std::make_unique<BundleInfo>(readBundle(fs::path(JP_LV2_DIR) / "jaypacks-eigenghost.lv2"));
        s_library = std::make_unique<Library>(*s_bundle);
    }

    static void TearDownTestSuite()
    {
        s_library.reset();
        s_bundle.reset();
    }

    /// A fresh instance with these settings, activated as a host does after setting the ports.
    static std::unique_ptr<Instance> instance(const Settings& settings)
    {
        auto plugin = std::make_unique<Instance>(s_library->descriptor(), *s_bundle, c_fs);
        for (const auto& [symbol, value] : settings)
        {
            plugin->setControl(symbol, value);
        }
        plugin->reactivate();
        return plugin;
    }

    static std::unique_ptr<BundleInfo> s_bundle;
    static std::unique_ptr<Library> s_library;
};

std::unique_ptr<BundleInfo> Eigenghost::s_bundle;
std::unique_ptr<Library> Eigenghost::s_library;

TEST_F(Eigenghost, TtlDefaultsAreThePedalDefaults)
{
    using P = EigenghostPedal;
    const std::map<std::string, float> expected{{"decay", P::c_defaultDecay},
                                                {"dry", P::c_defaultDry},
                                                {"wet", P::c_defaultWet},
                                                {"size", P::c_defaultSize},
                                                {"pitch1", P::c_defaultPitch1},
                                                {"pitch2", P::c_defaultPitch2},
                                                {"shimmer", P::c_defaultShimmer},
                                                {"reverse", P::c_defaultReverse},
                                                {"diffusion", P::c_defaultDiffusion},
                                                {"diffuserSize", P::c_defaultDiffuserSize},
                                                {"spread", P::c_defaultSpread},
                                                {"bulge", P::c_defaultBulge},
                                                {"damping", P::c_defaultDamping},
                                                {"lowCut", P::c_defaultLowCut},
                                                {"width", P::c_defaultWidth},
                                                {"predelay", P::c_defaultPredelay},
                                                {"modDepth", P::c_defaultModDepth}};
    size_t controls = 0;
    for (const PortInfo& port : s_bundle->ports)
    {
        if (port.audio || !port.input)
        {
            continue;
        }
        ++controls;
        const auto it = expected.find(port.symbol);
        ASSERT_NE(it, expected.end()) << port.symbol;
        EXPECT_FLOAT_EQ(port.defaultValue, it->second) << port.symbol;
    }
    EXPECT_EQ(controls, expected.size());
    for (const PortInfo& port : s_bundle->ports)
    {
        if (port.symbol == "dry" || port.symbol == "wet")
        {
            EXPECT_FLOAT_EQ(port.minimum, P::c_muteDb) << port.symbol;
        }
    }
}

TEST_F(Eigenghost, DecayKnobSetsTheDecayTime)
{
    std::vector<float> impulse(static_cast<size_t>(c_fs * 4.5f), 0.0f);
    impulse[0] = 1.0f;
    for (const float decay : {1.0f, 3.0f})
    {
        auto plugin = instance({{"dry", -60.0f},
                                {"wet", 0.0f},
                                {"decay", decay},
                                {"shimmer", 0.0f},
                                {"damping", 20000.0f},
                                {"lowCut", 10.0f}});
        const auto response = plugin->process(impulse, 128);
        const double t60 = measureT60(response);
        std::cout << "[ eigen    ] decay knob " << decay << " s: T60 " << t60 << " s\n";
        // the 20 kHz lowpass of the two damped lines still takes some treble, so the
        // broadband T60 lies slightly below the setting
        EXPECT_GT(t60, 0.9 * decay);
        EXPECT_LT(t60, 1.05 * decay);
    }
}

TEST_F(Eigenghost, PitchLinesClimbInTheTail)
{
    // a 237 Hz tone for 1 s, then the tail: with the pitch lines at a fifth up the tail
    // carries 355 Hz, which a tail without pitch shifting does not
    const size_t length = static_cast<size_t>(c_fs * 3.0f);
    std::vector<float> input = sine(237.0, 0.5, length);
    std::fill(input.begin() + static_cast<long>(c_fs), input.end(), 0.0f);
    const double fifth = 237.0 * std::pow(2.0, 7.0 / 12.0);
    std::map<float, double> atFifth;
    for (const float shimmer : {0.0f, 100.0f})
    {
        auto plugin = instance({{"dry", -60.0f},
                                {"wet", 0.0f},
                                {"decay", 4.0f},
                                {"shimmer", shimmer},
                                {"pitch1", 7.0f},
                                {"pitch2", 7.0f}});
        const auto output = plugin->process(input, 128);
        const auto tail = slice(output, static_cast<size_t>(c_fs * 1.2f), 65536);
        atFifth[shimmer] = test::toDb(std::abs(test::toneAmplitude(tail, fifth, c_fs)));
    }
    std::cout << "[ eigen    ] tail at a fifth up: " << atFifth[0.0f] << " dB without, " << atFifth[100.0f]
              << " dB with pitch lines\n";
    EXPECT_GT(atFifth[100.0f] - atFifth[0.0f], 30.0);
}

TEST_F(Eigenghost, DryWetJumpsHaveNoStep)
{
    // dry and wet jump between "dry only" and "wet only" every 107 ms. During a jump the
    // output is a crossfade of two sines of the same frequency and different phase (the input
    // and the reverb's answer), which can briefly be louder than either, so the bound is the
    // sum of their largest steps; a gain stepping within a block would jump far above it
    const auto input = sine(237.0, 0.5, 48000 * 3);
    const Settings dryOnly{{"shimmer", 0.0f}, {"dry", 0.0f}, {"wet", -60.0f}};
    const Settings wetOnly{{"shimmer", 0.0f}, {"dry", -60.0f}, {"wet", 6.0f}};
    SignalStats dry;
    SignalStats wet;
    const auto dryOut = instance(dryOnly)->process(input, 128);
    const auto wetOut = instance(wetOnly)->process(input, 128);
    dry.add(dryOut.data() + 48000, dryOut.size() - 48000);
    wet.add(wetOut.data() + 48000, wetOut.size() - 48000);
    auto plugin = instance(dryOnly);
    const auto jumped = plugin->process(input, 128, false,
                                        [&plugin](const size_t position)
                                        {
                                            const size_t step = position / 128;
                                            if (position >= 48000 && step % 40 == 0)
                                            {
                                                const bool toDry = (step / 40) % 2 == 0;
                                                plugin->setControl("dry", toDry ? 0.0f : -60.0f);
                                                plugin->setControl("wet", toDry ? -60.0f : 6.0f);
                                            }
                                        });
    SignalStats stats;
    stats.add(jumped.data() + 48000, jumped.size() - 48000);
    const double bound = dry.maxDelta() + wet.maxDelta();
    std::cout << "[ eigen    ] dry/wet jumps: largest step " << stats.maxDelta() << " (dry only " << dry.maxDelta()
              << ", wet only " << wet.maxDelta() << ")\n";
    EXPECT_LT(stats.maxDelta(), 1.1 * bound);
}

TEST_F(Eigenghost, DecayJumpsHaveNoZipper)
{
    // the decay jumps between 0.5 and 10 s every 107 ms; the line gains follow per block
    // (16 samples), smoothed in log time. Control: the steady output with one step to half level.
    const Settings base{{"shimmer", 0.0f}, {"dry", -60.0f}, {"wet", 0.0f}, {"decay", 2.0f}};
    const auto input = sine(237.0, 0.5, 48000 * 3);
    const size_t window = 65536;
    const size_t start = 48000;
    const auto steady = slice(instance(base)->process(input, 128), start, window);
    auto plugin = instance(base);
    const auto jumped = plugin->process(input, 128, false,
                                        [&plugin](const size_t position)
                                        {
                                            const size_t step = position / 128;
                                            if (position >= 48000 && step % 40 == 0)
                                            {
                                                plugin->setControl("decay", (step / 40) % 2 == 0 ? 0.5f : 10.0f);
                                            }
                                        });
    const double steadyDb = test::highBandEnergyDb(steady, 5000.0, c_fs);
    const double jumpedDb = test::highBandEnergyDb(slice(jumped, start, window), 5000.0, c_fs);
    auto stepped = steady;
    for (size_t i = window / 2; i < window; ++i)
    {
        stepped[i] *= 0.5f;
    }
    const double steppedDb = test::highBandEnergyDb(stepped, 5000.0, c_fs);
    std::cout << "[ eigen    ] energy above 5 kHz: steady " << steadyDb << " dB, decay jumps every 107 ms " << jumpedDb
              << " dB, one hard step " << steppedDb << " dB\n";
    EXPECT_LT(jumpedDb, -70.0);
    EXPECT_GT(steppedDb, -60.0);
}

TEST_F(Eigenghost, SizeSweepHasNoClicks)
{
    const Settings base{{"shimmer", 0.0f}, {"decay", 2.0f}};
    const auto input = sine(237.0, 0.5, 48000 * 3);
    const size_t window = 65536;
    const size_t start = 48000;
    auto plugin = instance(base);
    const std::vector<float> sizes{20.0f, 6.0f, 33.0f, 12.0f, 3.0f, 40.0f};
    const auto output = plugin->process(input, 128, false,
                                        [&plugin, &sizes](const size_t position)
                                        {
                                            const size_t step = position / 128;
                                            if (position >= 48000 && step % 50 == 0)
                                            {
                                                plugin->setControl("size", sizes[(step / 50) % sizes.size()]);
                                            }
                                        });
    const double sweptDb = test::highBandEnergyDb(slice(output, start, window), 5000.0, c_fs);
    std::cout << "[ eigen    ] energy above 5 kHz with a size change every 133 ms: " << sweptDb << " dB\n";
    EXPECT_LT(sweptDb, -70.0);
}

}

namespace jp::host
{

TEST_F(Eigenghost, PredelayShiftsTheWetOnset)
{
    // dry muted, diffusion 0: the first wet sample of an impulse moves by the pre-delay
    std::vector<float> impulse(48000, 0.0f);
    impulse[0] = 1.0f;
    const auto firstWet = [&impulse](const float ms)
    {
        const auto out =
            instance({{"dry", -60.0f}, {"wet", 0.0f}, {"diffusion", 0.0f}, {"shimmer", 0.0f}, {"predelay", ms}})
                ->process(impulse, 128);
        const auto it = std::find_if(out.begin(), out.end(), [](const float y) { return y != 0.0f; });
        return static_cast<long>(it - out.begin());
    };
    const long none = firstWet(0.0f);
    std::cout << "[ eigen    ] first wet sample: pre-delay 0 at " << none << ", 50 ms at " << firstWet(50.0f) << "\n";
    EXPECT_EQ(firstWet(50.0f) - none, 2400);
    EXPECT_EQ(firstWet(200.0f) - none, 9600);
}

TEST_F(Eigenghost, PredelaySweepHasNoClicks)
{
    // as the size sweep: a pre-delay change every 133 ms crossfades instead of jumping
    const Settings base{{"shimmer", 0.0f}, {"decay", 2.0f}};
    const auto input = sine(237.0, 0.5, 48000 * 3);
    const size_t window = 65536;
    const size_t start = 48000;
    auto plugin = instance(base);
    const std::vector<float> delays{120.0f, 5.0f, 200.0f, 40.0f, 0.0f, 80.0f};
    const auto output = plugin->process(input, 128, false,
                                        [&plugin, &delays](const size_t position)
                                        {
                                            const size_t step = position / 128;
                                            if (position >= 48000 && step % 50 == 0)
                                            {
                                                plugin->setControl("predelay", delays[(step / 50) % delays.size()]);
                                            }
                                        });
    const double sweptDb = test::highBandEnergyDb(slice(output, start, window), 5000.0, c_fs);
    std::cout << "[ eigen    ] energy above 5 kHz with a pre-delay change every 133 ms: " << sweptDb << " dB\n";
    EXPECT_LT(sweptDb, -70.0);
}

TEST_F(Eigenghost, ModDepthSpreadsTheTail)
{
    // 1 kHz for 1 s, then the tail (1.2 to 2.6 s): the share of its energy outside 1 kHz +-3 Hz
    // grows with the depth (the reverb's own modes spread it already without modulation)
    const size_t from = 57600;
    const size_t window = 65536;
    std::vector<float> input = sine(1000.0, 0.5, from + window);
    std::fill(input.begin() + 48000, input.end(), 0.0f);
    const auto outside = [&](const float cents)
    {
        const auto out =
            instance({{"dry", -60.0f}, {"wet", 0.0f}, {"shimmer", 0.0f}, {"decay", 4.0f}, {"modDepth", cents}})
                ->process(input, 128);
        const auto magnitude = test::hannMagnitudeSpectrum(slice(out, from, window));
        const double binHz = static_cast<double>(c_fs) / static_cast<double>(window);
        double inside = 0.0;
        double total = 0.0;
        for (size_t k = 0; k < magnitude.size(); ++k)
        {
            const double power = magnitude[k] * magnitude[k];
            total += power;
            inside += std::abs(static_cast<double>(k) * binHz - 1000.0) <= 3.0 ? power : 0.0;
        }
        return 1.0 - inside / total;
    };
    const double none = outside(0.0f);
    const double full = outside(20.0f);
    std::cout << "[ eigen    ] tail energy outside 1 kHz +-3 Hz: depth 0 " << 100.0 * none << " %, 4 cents "
              << 100.0 * outside(4.0f) << " %, 20 cents " << 100.0 * full << " %\n";
    EXPECT_GT(full, none + 0.3);
}

}
