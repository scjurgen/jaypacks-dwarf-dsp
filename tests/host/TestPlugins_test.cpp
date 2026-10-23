// The test plugins together through their built LV2 binaries, wired the way the device test
// of step 10 wires them in a pedalboard: siggen -> (shipahoy) -> meter signal input, siggen ->
// meter reference input. Results are read only from the meter's control outputs, with the
// measure / resultId protocol, so these tests are the Mac version of the device checks.

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

#include "Lv2Host.h"
#include "dsp/Filters/PoleMixingFilter.h"
#include "plugins/shipahoy/ShipAhoyEngine.h"

namespace jp::host
{

namespace
{
constexpr double c_fs{48000.0};
constexpr size_t c_blockSize{128};
using Settings = std::map<std::string, float>;

// menu indices (siggen.json)
constexpr float c_sine{1.0f};
constexpr float c_whiteNoise{4.0f};

double toDb(const double x)
{
    return 20.0 * std::log10(std::max(x, 1e-12));
}

/// Bundle and binary of one plugin, loaded once per test suite.
struct Plugin
{
    explicit Plugin(const std::string& name)
        : bundle(readBundle(fs::path(JP_LV2_DIR) / ("jaypacks-" + name + ".lv2")))
        , library(bundle)
    {
    }

    std::unique_ptr<Instance> instance(const Settings& settings) const
    {
        auto plugin = std::make_unique<Instance>(library.descriptor(), bundle, c_fs);
        set(*plugin, settings);
        plugin->reactivate();
        return plugin;
    }

    static void set(Instance& plugin, const Settings& settings)
    {
        for (const auto& [symbol, value] : settings)
        {
            plugin.setControl(symbol, value);
        }
    }

    BundleInfo bundle;
    Library library;
};
}

class TestPlugins : public ::testing::Test
{
  protected:
    static void SetUpTestSuite()
    {
        s_siggen = std::make_unique<Plugin>("siggen");
        s_meter = std::make_unique<Plugin>("meter");
        s_shipahoy = std::make_unique<Plugin>("shipahoy");
    }

    static void TearDownTestSuite()
    {
        s_siggen.reset();
        s_meter.reset();
        s_shipahoy.reset();
    }

    void SetUp() override
    {
        m_siggen = s_siggen->instance({});
        m_meter = s_meter->instance({});
    }

    /// Runs the graph for `samples` samples in host blocks.
    void run(const size_t samples)
    {
        for (size_t pos = 0; pos < samples; pos += c_blockSize)
        {
            const size_t n = std::min(c_blockSize, samples - pos);
            const auto generated = m_siggen->process(std::vector<float>(n, 0.0f), n);
            const auto processed = m_shipahoy ? m_shipahoy->process(generated, n) : generated;
            std::ignore = m_meter->processChannels({processed, generated}, n, n);
        }
    }

    /// The client side of the protocol: a new measure value, then run until resultId shows it.
    void measure()
    {
        const float id = ++m_measureId;
        m_meter->setControl("measure", id);
        const double window = m_meterWindow;
        const auto limit = static_cast<size_t>((window + 1.0) * c_fs);
        for (size_t done = 0; m_meter->output("resultId") != id; done += c_blockSize)
        {
            ASSERT_LT(done, limit) << "no result for measure " << id;
            run(c_blockSize);
        }
    }

    void setMeterWindow(const float seconds)
    {
        m_meterWindow = seconds;
        m_meter->setControl("window", seconds);
    }

    std::array<float, 8> bandOutputs(const std::string& what) const
    {
        std::array<float, 8> values{};
        for (size_t k = 0; k < values.size(); ++k)
        {
            values[k] = m_meter->output("band" + std::to_string(k + 1) + what);
        }
        return values;
    }

    static std::unique_ptr<Plugin> s_siggen;
    static std::unique_ptr<Plugin> s_meter;
    static std::unique_ptr<Plugin> s_shipahoy;

    std::unique_ptr<Instance> m_siggen;
    std::unique_ptr<Instance> m_shipahoy;
    std::unique_ptr<Instance> m_meter;
    float m_measureId{0.0f};
    float m_meterWindow{0.5f};
};

std::unique_ptr<Plugin> TestPlugins::s_siggen;
std::unique_ptr<Plugin> TestPlugins::s_meter;
std::unique_ptr<Plugin> TestPlugins::s_shipahoy;

TEST_F(TestPlugins, MeterReadsTheSiggenSine)
{
    Plugin::set(*m_siggen, {{"waveform", c_sine}, {"frequency", 1000.0f}, {"level", -20.0f}});
    setMeterWindow(1.0f);
    m_meter->setControl("toneHz", 1000.0f);
    measure();
    EXPECT_NEAR(m_meter->output("peak"), -20.0f, 1e-3f);
    EXPECT_NEAR(m_meter->output("rms"), -20.0 - 10.0 * std::log10(2.0), 1e-3);
    EXPECT_NEAR(m_meter->output("frequency"), 1000.0f, 0.01f);
    EXPECT_NEAR(m_meter->output("toneLevel"), -20.0f, 1e-4f);
    EXPECT_LT(m_meter->output("toneResidual"), -120.0f);
    EXPECT_EQ(m_meter->output("diffPeak"), -240.0f) << "signal and reference are the same";
    EXPECT_EQ(m_meter->output("nonFinite"), 0.0f);
    EXPECT_NEAR(m_siggen->output("rms"), m_meter->output("rms"), 1e-4f) << "siggen reports its own level";
    std::cout << "[ sine     ] peak " << m_meter->output("peak") << " dB, rms " << m_meter->output("rms")
              << " dB, frequency " << m_meter->output("frequency") << " Hz, tone residual "
              << m_meter->output("toneResidual") << " dB\n";
}

TEST_F(TestPlugins, RestartGivesTheSameNoiseAndTheSameResult)
{
    Plugin::set(*m_siggen, {{"waveform", c_whiteNoise}, {"level", -6.0f}});
    setMeterWindow(1.0f);
    std::array<std::array<float, 8>, 2> levels{};
    std::array<float, 2> rms{};
    for (size_t k = 0; k < 2; ++k)
    {
        run(1000); // arbitrary position before the restart
        m_siggen->setControl("restart", static_cast<float>(k + 1));
        measure(); // same block as the restart: the window starts with the restarted noise
        EXPECT_EQ(m_siggen->output("restartId"), static_cast<float>(k + 1));
        levels[k] = bandOutputs("Level");
        rms[k] = m_meter->output("rms");
    }
    EXPECT_EQ(levels[0], levels[1]);
    EXPECT_EQ(rms[0], rms[1]);
    EXPECT_NEAR(rms[0], -6.0 - 10.0 * std::log10(3.0), 0.02) << "uniform noise: peak / sqrt(3)";
}

TEST_F(TestPlugins, ShipAhoyMix0EqualsTheReference)
{
    m_shipahoy = s_shipahoy->instance({{"mix", 0.0f}});
    Plugin::set(*m_siggen, {{"waveform", c_whiteNoise}, {"level", -6.0f}});
    measure();
    EXPECT_EQ(m_meter->output("diffPeak"), -240.0f);
    EXPECT_EQ(m_meter->output("rms"), m_meter->output("refRms"));
}

TEST_F(TestPlugins, ShipAhoyFilterTypesMeasuredByTheMeter)
{
    // step 8's filter shape test, measured by the meter plugin from its band outputs: noise from siggen, shipahoy
    // running a user script that holds 1 kHz without resonance and sets the type, gain against the reference input
    const fs::path folder = fs::current_path() / "shipahoy-host-test" / ("meter-" + std::to_string(::getpid()));
    fs::remove_all(folder);
    fs::create_directories(folder / "shipahoy");
    ::setenv("JP_SCRIPT_DIR", folder.c_str(), 1);
    Plugin::set(*m_siggen, {{"waveform", c_whiteNoise}, {"level", -12.0f}});
    setMeterWindow(16.0f);
    const std::array<double, 8> bandsHz{63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0};
    const double centre = 1000.0;
    const double pole = poleMixingTuningForPeak(centre, c_fs).pole;
    double worstDb = 0.0;
    for (size_t type = 0; type < ShipAhoyEngine::c_filterTypeCount; ++type)
    {
        {
            std::ofstream out(folder / "shipahoy" / "user-1.lua", std::ios::binary | std::ios::trunc);
            out << "function OnStart()\n    SetFilterType(" << type << ")\n    SetResonance(0)\n"
                << "    SetCutoff(69 + 12 * math.log(1000 / 440, 2), { glide = 0 })\nend\n";
        }
        m_shipahoy = s_shipahoy->instance({{"mix", 100.0f}, {"script", 9.0f}});
        run(4800); // let the filter settle on the new type before the window starts
        measure();
        const auto gains = bandOutputs("Gain");
        const auto coherences = bandOutputs("Coherence");
        const PoleMixingPreset& preset = c_poleMixingPresets[ShipAhoyEngine::c_filterTypePresets[type]];
        for (size_t k = 0; k < bandsHz.size(); ++k)
        {
            const double theoryDb = toDb(std::abs(poleMixingResponse(preset, pole, 0.0, bandsHz[k], c_fs)));
            if (theoryDb > -40.0)
            {
                const double deviation = gains[k] - theoryDb;
                worstDb = std::max(worstDb, std::abs(deviation));
                EXPECT_NEAR(deviation, 0.0, 0.2) << ShipAhoyEngine::c_filterTypeLabels[type] << " at " << bandsHz[k];
                EXPECT_GT(coherences[k], 0.98f) << ShipAhoyEngine::c_filterTypeLabels[type] << " at " << bandsHz[k];
            }
        }
    }
    std::cout << "[ shape    ] meter bands: worst deviation from the transfer function " << worstDb << " dB\n";
    ::unsetenv("JP_SCRIPT_DIR");
    fs::remove_all(folder);
}

}
