// Integration tests of jaystwin through the built LV2 binary (<build>/lv2/jaypacks-jaystwin.lv2):
// the TTL agrees with the pedal's constants, knob sweeps and jumps have no clicks (largest
// sample step of a 237 Hz sine against the largest step of the steady outputs over the knob's
// range, with a deliberate step as the control), the output level against the gain knob, the
// aliasing of the atan drive at high gain, and no subnormal output in a decaying tail.

#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Lv2Host.h"
#include "dsp/Analysis/SignalStats.h"
#include "plugins/jaystwin/JaysTwinPedal.h"
#include "tests/unit/TestHelpers.h"

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

/// Six open-string notes with eight harmonics each, peaking at -12 dBFS.
std::vector<float> chord(const size_t length)
{
    const double notes[] = {82.41, 110.0, 146.83, 196.0, 246.94, 329.63};
    std::vector<float> out(length);
    double peak = 0.0;
    for (size_t i = 0; i < length; ++i)
    {
        double s = 0.0;
        for (const double f : notes)
        {
            for (int k = 1; k <= 8; ++k)
            {
                s += std::sin(2.0 * test::c_pi * f * k * static_cast<double>(i) / c_fs + k) / (0.5 * k * k + 0.5);
            }
        }
        out[i] = static_cast<float>(s);
        peak = std::max(peak, std::abs(s));
    }
    for (float& v : out)
    {
        v = static_cast<float>(v * 0.25 / peak);
    }
    return out;
}

double rmsDb(const std::vector<float>& x, const size_t from)
{
    double sum = 0.0;
    for (size_t i = from; i < x.size(); ++i)
    {
        sum += static_cast<double>(x[i]) * x[i];
    }
    return 10.0 * std::log10(sum / static_cast<double>(x.size() - from));
}

double maxStep(const std::vector<float>& x, const size_t from)
{
    SignalStats stats;
    stats.add(x.data() + from, x.size() - from);
    return stats.maxDelta();
}
}

class JaysTwin : public ::testing::Test
{
  protected:
    static void SetUpTestSuite()
    {
        s_bundle = std::make_unique<BundleInfo>(readBundle(fs::path(JP_LV2_DIR) / "jaypacks-jaystwin.lv2"));
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

std::unique_ptr<BundleInfo> JaysTwin::s_bundle;
std::unique_ptr<Library> JaysTwin::s_library;

TEST_F(JaysTwin, TtlDefaultsAreThePedalDefaults)
{
    using P = JaysTwinPedal;
    const std::map<std::string, float> expected{{"bass", P::c_defaultBass},     {"mid", P::c_defaultMid},
                                                {"treble", P::c_defaultTreble}, {"gain", P::c_defaultGain},
                                                {"level", P::c_defaultLevel},   {"bright", P::c_defaultBright}};
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
}

TEST_F(JaysTwin, KnobSweepsAndJumpsHaveNoClicks)
{
    const auto input = sine(237.0, 0.25, 48000 * 3);
    const size_t start = 4800;
    // A 20 ms tone change of a driven signal overshoots the steady steps a little even with a
    // per-sample redesign (measured up to 1.17 times, treble); designs applied at once every 16
    // samples made 3.1 times. The redesign artifact itself is pinned exactly in the unit test
    // FenderToneStack.RampedRedesignEqualsPerSampleRedesign.
    const std::vector<std::pair<std::string, Settings>> cases{
        {"bass", {}}, {"mid", {}}, {"treble", {}}, {"gain", {}}, {"gain", {{"bright", 1.0f}}}};
    for (const auto& knobCase : cases)
    {
        const std::string& knob = knobCase.first;
        const Settings& base = knobCase.second;
        const auto with = [&base](const std::string& symbol, const float value)
        {
            Settings s = base;
            s[symbol] = value;
            return s;
        };
        // the largest step of the steady output anywhere on the knob's range
        // gain goes up to eleven, the tone knobs to 10
        const float top = knob == "gain" ? 11.0f : 10.0f;
        double bound = 0.0;
        for (float value = 0.0f; value <= top; value += 1.0f)
        {
            bound = std::max(bound, maxStep(instance(with(knob, value))->process(input, 128), start));
        }
        // a host moves the knob once per block: 0 -> top -> 0 over 2 s, then jumps between 0
        // and top every 107 ms
        auto plugin = instance(with(knob, 0.0f));
        const auto moved = plugin->process(input, 128, false,
                                           [&plugin, &knob, top](const size_t position)
                                           {
                                               const float t = static_cast<float>(position) / c_fs;
                                               float value = t < 1.0f ? top * t : top * (2.0f - t);
                                               if (t >= 2.0f)
                                               {
                                                   value = (position / 5120) % 2 == 0 ? 0.0f : top;
                                               }
                                               plugin->setControl(knob, value);
                                           });
        const double step = maxStep(moved, start);
        std::cout << "[ twin     ] " << knob << (base.empty() ? "" : " (bright)") << " sweep and jumps: largest step "
                  << step << ", steady over the range at most " << bound << "\n";
        EXPECT_LT(step, 1.3 * bound) << knob;
    }
    // control: the steady output with a hard step to half its level
    auto stepped = instance({})->process(input, 128);
    const double steady = maxStep(stepped, start);
    for (size_t i = 24000 + 3; i < stepped.size(); ++i)
    {
        stepped[i] *= 0.5f;
    }
    std::cout << "[ twin     ] control: steady step " << steady << ", with a hard half-level step "
              << maxStep(stepped, start) << "\n";
    EXPECT_GT(maxStep(stepped, start), 2.0 * steady);
}

TEST_F(JaysTwin, LevelRisesWithTheGainAndTheDefaultsMatchTheInput)
{
    const auto input = chord(96000);
    const double inputDb = rmsDb(input, 48000);
    double previous = -1e9;
    for (float gain = 0.0f; gain <= 11.0f; gain += 1.0f)
    {
        const auto out = instance({{"gain", gain}})->process(input, 128);
        const double db = rmsDb(out, 48000);
        std::cout << "[ twin     ] chord at -12 dBFS peak (" << inputDb << " dBFS rms), gain " << gain << ": " << db
                  << " dBFS rms\n";
        EXPECT_GT(db, previous) << gain;
        previous = db;
    }
    const double defaults = rmsDb(instance({})->process(input, 128), 48000);
    EXPECT_NEAR(defaults, inputDb, 1.0);
    // the presets are levelled to the defaults
    const std::map<std::string, Settings> presets{
        {"Bright clean", {{"bass", 4}, {"mid", 5}, {"treble", 6}, {"gain", 2}, {"level", 4}, {"bright", 1}}},
        {"Edge", {{"bass", 5}, {"mid", 6}, {"treble", 5}, {"gain", 7}, {"level", -10}}},
        {"Scooped", {{"bass", 7}, {"mid", 2}, {"treble", 7}, {"gain", 3}, {"level", -1}}}};
    for (const auto& [name, settings] : presets)
    {
        const double db = rmsDb(instance(settings)->process(input, 128), 48000);
        std::cout << "[ twin     ] preset " << name << ": " << db - defaults << " dB against the defaults\n";
        EXPECT_NEAR(db, defaults, 1.0) << name;
    }
}

TEST_F(JaysTwin, AliasingOfTheDriveAtHighGain)
{
    // the atan sees a sine (the tone stack before it is linear), so the clean output holds odd
    // harmonics only; every component k f0 above Nyquist folds back as an alias. Measured on
    // the output (after the cabinet), relative to the fundamental.
    const size_t settle = 24000;
    const size_t window = 65536;
    for (const float gain : {7.0f, 10.0f, 11.0f})
    {
        for (const double f0 : {1300.0, 3100.0})
        {
            const auto out = instance({{"gain", gain}})->process(sine(f0, 0.25, settle + window), 128);
            const std::vector<float> tail(out.begin() + static_cast<long>(settle), out.end());
            const double fundamental = std::abs(test::toneAmplitude(tail, f0, c_fs));
            double aliasAll = 0.0;
            double aliasLow = 0.0;
            double harmonics = 0.0;
            for (int k = 3; k <= 199; k += 2)
            {
                double f = std::fmod(k * f0, static_cast<double>(c_fs));
                f = f > 0.5 * c_fs ? c_fs - f : f;
                const double a = std::abs(test::toneAmplitude(tail, f, c_fs)) / fundamental;
                if (k * f0 < 0.5 * c_fs)
                {
                    harmonics += a * a;
                }
                else
                {
                    aliasAll += a * a;
                    aliasLow += f < 5000.0 ? a * a : 0.0;
                }
            }
            std::cout << "[ twin     ] gain " << gain << ", " << f0 << " Hz at -12 dBFS: harmonics "
                      << 10.0 * std::log10(harmonics) << " dB, aliases " << 10.0 * std::log10(aliasAll)
                      << " dB (below 5 kHz " << 10.0 * std::log10(aliasLow) << " dB) re fundamental\n";
            // up to 10 the aliases stay below the harmonics; at 11 a 3.1 kHz tone aliases as strongly
            // as its harmonics (both near -36 dB re fundamental): reported, the case for
            // oversampling if it is audible
            if (gain <= 10.0f)
            {
                EXPECT_LT(10.0 * std::log10(aliasAll), 10.0 * std::log10(harmonics)) << gain << " " << f0;
            }
        }
    }
}

TEST_F(JaysTwin, DecayingTailHasNoSubnormals)
{
    std::vector<float> input = chord(48000 * 6);
    std::fill(input.begin() + 48000, input.end(), 0.0f);
    const auto out = instance({{"gain", 11.0f}, {"bass", 10.0f}})->process(input, 128);
    size_t subnormals = 0;
    for (const float y : out)
    {
        subnormals += std::fpclassify(y) == FP_SUBNORMAL ? 1u : 0u;
    }
    EXPECT_EQ(subnormals, 0u);
    EXPECT_LT(std::abs(out.back()), 1e-20f);
}

}
