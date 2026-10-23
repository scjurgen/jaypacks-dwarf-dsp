// Integration tests of shipahoy through the built LV2 binary (<build>/lv2/jaypacks-shipahoy.lv2):
// what a host sets on the ports and what the scripts do must come out of the audio. The measurements use the blocks
// of dsp/Analysis/, which the meter plugin runs on the device (step 9), so the Mac and the device measure the same way.
//
// The plugin has no running state without a script, so the tests that need a fixed filter run a user script that sets
// it (frequency, resonance, type, drift) in OnStart. Hop positions of the factory scripts are read from the audio
// alone: a probe sine passes the one-pole lowpass ("lp 6 dB", no resonance, from a copy of the script with that type),
// its fitted amplitude gives the stage pole in closed form, the pole gives the peak frequency through
// poleMixingTuningForPeak() and that gives the position in the range.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

#include "Lv2Host.h"
#include "dsp/Analysis/GoertzelBand.h"
#include "dsp/Analysis/SignalStats.h"
#include "dsp/Analysis/ToneFit.h"
#include "dsp/Analysis/ZeroCrossingFrequency.h"
#include "dsp/Filters/PoleMixingFilter.h"
#include "dsp/Numbers/Random.h"
#include "plugins/shipahoy/ShipAhoyEngine.h"
#include "plugins/shipahoy/ShipAhoyScript.h"

namespace jp::host
{

namespace
{
constexpr double c_pi{3.14159265358979323846};
constexpr float c_fs{48000.0f};
using Settings = std::map<std::string, float>;

// port values (shipahoy.json)
constexpr float c_userScript{9.0f}; // user-1
constexpr float c_hop{0.0f};

// hop measurement: probe tone through "lp 6 dB"
constexpr double c_probeHz{8000.0};
constexpr double c_probeAmplitude{0.25};
constexpr float c_centreHz{1000.0f};

double toDb(const double x)
{
    return 20.0 * std::log10(std::max(x, 1e-12));
}

std::vector<float> noise(const size_t length, const float amplitude, const uint64_t seed = 3u)
{
    Random random(seed);
    std::vector<float> out(length);
    for (auto& x : out)
    {
        x = amplitude * (2.0f * random.uniform() - 1.0f);
    }
    return out;
}

std::vector<float> sine(const double frequency, const double amplitude, const size_t length)
{
    std::vector<float> out(length);
    for (size_t i = 0; i < length; ++i)
    {
        out[i] = static_cast<float>(amplitude * std::sin(2.0 * c_pi * frequency * static_cast<double>(i) / c_fs));
    }
    return out;
}

/// Lua text that sets a fixed filter in OnStart: the frequency in Hz, the resonance in percent, the type by name.
std::string fixedFilter(const double hz, const double resonance, const std::string& type)
{
    return "function OnStart()\n    SetFilterType(FilterType." + type + ")\n    SetResonance(" +
           std::to_string(resonance) + ")\n    SetCutoff(69 + 12 * math.log(" + std::to_string(hz) +
           " / 440, 2), { glide = 0 })\nend\n";
}

/// Position in the range from the probe's amplitude after the one-pole lowpass.
double positionFromProbeGain(const double gain, const double centreHz, const double rangeOctaves)
{
    // |H|^2 = (1 - p)^2 / (1 - 2 p cos w + p^2)  <=>  p^2 + B p + 1 = 0; the root inside |p| < 1
    const double h2 = gain * gain;
    const double c = std::cos(2.0 * c_pi * c_probeHz / c_fs);
    const double b = (2.0 - 2.0 * h2 * c) / (h2 - 1.0);
    const double pole = (-b - std::sqrt(b * b - 4.0)) / 2.0;
    // the tuned pole falls as the peak frequency rises
    double low = 10.0;
    double high = c_fs / 6.0;
    for (int i = 0; i < 60; ++i)
    {
        const double mid = std::sqrt(low * high);
        const double midPole = poleMixingTuningForPeak(static_cast<float>(mid), c_fs).pole;
        (midPole > pole ? low : high) = mid;
    }
    return std::log2(std::sqrt(low * high) / centreHz) / rangeOctaves + 0.5;
}

/// The samples at which the steps of the plugin's clock take effect: the first tick one lead after the start, then one
/// step apart, each a lead later (the plugin's samples count from 1). The lead is two blocks of the largest block the
/// plugin is run with: the tests here use 128.
std::vector<size_t> stepSamples(const double tempo, const size_t stepIndex, const size_t length)
{
    const double stepTicks = static_cast<double>(ship_ahoy::c_stepTicks[stepIndex]);
    const double samplesPerTick = c_fs * 60.0 / (tempo * static_cast<double>(ship_ahoy::c_ticksPerQuarter));
    const double lead = 2.0 * 128.0;
    std::vector<size_t> at;
    for (size_t k = 0;; ++k)
    {
        const auto sample = static_cast<size_t>(1.0 + lead + static_cast<double>(k) * stepTicks * samplesPerTick) +
                            static_cast<size_t>(lead) - 1;
        if (sample >= length)
        {
            return at;
        }
        at.push_back(sample);
    }
}

/// Position of every step measured on the plugin output, over the last three quarters of the step (after the glide and
/// the filter have settled).
std::vector<double> measuredPositions(const std::vector<float>& output, const std::vector<size_t>& steps,
                                      const double centreHz, const double rangeOctaves)
{
    std::vector<double> positions;
    for (size_t k = 0; k + 1 < steps.size(); ++k)
    {
        const size_t begin = steps[k] + (steps[k + 1] - steps[k]) / 4;
        ToneFit fit(c_probeHz, c_fs);
        fit.add(output.data() + begin, steps[k + 1] - begin);
        positions.push_back(positionFromProbeGain(fit.result().amplitude / c_probeAmplitude, centreHz, rangeOctaves));
    }
    return positions;
}

double mean(const std::vector<double>& x)
{
    return std::accumulate(x.begin(), x.end(), 0.0) / static_cast<double>(x.size());
}

double deviation(const std::vector<double>& x)
{
    const double m = mean(x);
    double sum = 0.0;
    for (const double v : x)
    {
        sum += (v - m) * (v - m);
    }
    return std::sqrt(sum / static_cast<double>(x.size()));
}

double lagOneCorrelation(const std::vector<double>& x)
{
    const double m = mean(x);
    double numerator = 0.0;
    double denominator = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        denominator += (x[i] - m) * (x[i] - m);
        if (i > 0)
        {
            numerator += (x[i] - m) * (x[i - 1] - m);
        }
    }
    return numerator / denominator;
}
}

class ShipAhoy : public ::testing::Test
{
  protected:
    static void SetUpTestSuite()
    {
        s_bundle = std::make_unique<BundleInfo>(readBundle(fs::path(JP_LV2_DIR) / "jaypacks-shipahoy.lv2"));
        s_library = std::make_unique<Library>(*s_bundle);
        s_userFolder = fs::current_path() / "shipahoy-host-test" / ("user-" + std::to_string(::getpid()));
        fs::remove_all(s_userFolder);
        fs::create_directories(s_userFolder / "shipahoy");
        ::setenv("JP_SCRIPT_DIR", s_userFolder.c_str(), 1);
    }

    static void TearDownTestSuite()
    {
        ::unsetenv("JP_SCRIPT_DIR");
        fs::remove_all(s_userFolder);
        s_library.reset();
        s_bundle.reset();
    }

    /// The text of user-1, for the plugins made from now on (the script is loaded when a plugin is activated).
    static void writeUser(const std::string& text, const int edit = 1)
    {
        const fs::path path = s_userFolder / "shipahoy" / "user-1.lua";
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << text;
        }
        fs::last_write_time(path, fs::file_time_type::clock::now() + std::chrono::seconds(edit));
    }

    /// A fresh instance at c_fs with these settings, activated as a host does after setting the ports (smoothers start
    /// at the values).
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

    static std::vector<float> run(const Settings& settings, const std::vector<float>& input)
    {
        return instance(settings)->process(input, 128);
    }

    /// The plugin running a fixed filter (a user script) with these port settings besides.
    static std::vector<float> runFixed(const double hz, const double resonance, const std::string& type,
                                       const std::vector<float>& input, Settings settings = {})
    {
        writeUser(fixedFilter(hz, resonance, type));
        settings["script"] = c_userScript;
        return run(settings, input);
    }

    /// The text of a factory script with a replacement made (the replaced text must be there).
    static std::string factoryScript(const std::string& name, const std::string& from, const std::string& to)
    {
        std::string text = readFile(s_bundle->dir / "scripts" / (name + ".lua"));
        const size_t at = text.find(from);
        EXPECT_NE(at, std::string::npos) << name << " has no '" << from << "'";
        if (at != std::string::npos)
        {
            text.replace(at, from.size(), to);
        }
        return text;
    }

    static std::unique_ptr<BundleInfo> s_bundle;
    static std::unique_ptr<Library> s_library;
    static fs::path s_userFolder;
};

std::unique_ptr<BundleInfo> ShipAhoy::s_bundle;
std::unique_ptr<Library> ShipAhoy::s_library;
fs::path ShipAhoy::s_userFolder;

TEST_F(ShipAhoy, Mix0IsTheInput)
{
    const auto input = noise(48000, 0.5f);
    EXPECT_EQ(run({{"mix", 0.0f}}, input), input);
}

TEST_F(ShipAhoy, EveryFilterTypeMatchesItsTransferFunction)
{
    // mix 100, a script that holds 1 kHz without resonance: the plugin is the filter of the menu entry, measured from
    // noise with TransferBand against poleMixingResponse()
    const auto input = noise(48000 * 16, 0.25f);
    const double pole = poleMixingTuningForPeak(c_centreHz, c_fs).pole;
    const std::vector<double> probes{63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0};
    double worstDb = 0.0;
    for (size_t type = 0; type < ShipAhoyEngine::c_filterTypeCount; ++type)
    {
        // the name of the FilterType constant at the index (ship_ahoy::c_filterTypeEntries)
        const auto output =
            runFixed(c_centreHz, 0.0, ship_ahoy::c_filterTypeEntries[type].name, input, {{"mix", 100.0f}});
        std::vector<TransferBand> bands;
        for (const double f : probes)
        {
            bands.emplace_back(f, c_fs, 16384); // 2.9 Hz bins: little leakage at the steep low end
        }
        for (size_t i = 0; i < input.size(); ++i)
        {
            for (auto& band : bands)
            {
                band.add(input[i], output[i]);
            }
        }
        const PoleMixingPreset& preset = c_poleMixingPresets[ShipAhoyEngine::c_filterTypePresets[type]];
        for (size_t k = 0; k < probes.size(); ++k)
        {
            const double theoryDb = toDb(std::abs(poleMixingResponse(preset, pole, 0.0, probes[k], c_fs)));
            if (theoryDb > -40.0)
            {
                const double deviation = toDb(bands[k].gain()) - theoryDb;
                worstDb = std::max(worstDb, std::abs(deviation));
                EXPECT_NEAR(deviation, 0.0, 0.2)
                    << ShipAhoyEngine::c_filterTypeLabels[type] << " at " << probes[k] << " Hz";
                // lower only on steep flanks, e.g. 0.988 for "notch octaves" at 500 Hz
                EXPECT_GT(bands[k].coherence(), 0.98)
                    << ShipAhoyEngine::c_filterTypeLabels[type] << " at " << probes[k];
            }
        }
    }
    std::cout << "[ shape    ] worst deviation from the transfer function: " << worstDb << " dB\n";
}

TEST_F(ShipAhoy, ResonanceRingsAtTheFrequencyOfTheScript)
{
    // an impulse into the filter at 99 % of the self-oscillation threshold rings at the frequency the script names
    // (the tuning puts the resonance peak there)
    // (the impulse comes after the resonance smoother has settled: 85 % at the start, 99 % from the script)
    constexpr size_t c_impulseAt{9600};
    std::vector<float> impulse(48000, 0.0f);
    impulse[c_impulseAt] = 0.5f;
    for (const float frequency : {300.0f, 1000.0f, 4000.0f})
    {
        const auto output = runFixed(frequency, 99.0, "Lp24", impulse, {{"mix", 100.0f}});
        const size_t begin = c_impulseAt + 480;
        const size_t end = c_impulseAt + 9600;
        SignalStats stats;
        stats.add(output.data() + begin, end - begin);
        ZeroCrossingFrequency counter(c_fs, static_cast<float>(0.01 * stats.peak()));
        counter.add(output.data() + begin, end - begin);
        EXPECT_NEAR(counter.frequency() / frequency, 1.0, 0.01) << frequency << " Hz: rings at " << counter.frequency();
        std::cout << "[ ringing  ] script " << frequency << " Hz, rings at " << counter.frequency() << " Hz\n";
    }
}

TEST_F(ShipAhoy, DriftIsOffAtDepthZeroAndTheScriptCommandReachesTheAudio)
{
    const auto input = sine(250.0, 0.1, 48000 * 4);
    const auto withDrift = [&](const float depth, const float rate)
    {
        writeUser("function OnStart()\n    SetFilterType(FilterType.Lp12)\n    SetResonance(85)\n"
                  "    SetCutoff(69 + 12 * math.log(1000 / 440, 2), { glide = 0 })\n    SetDrift(" +
                  std::to_string(depth) + ", { rate = " + std::to_string(rate) + " })\nend\n");
        return run({{"script", c_userScript}}, input);
    };
    EXPECT_EQ(withDrift(0.0f, 0.05f), withDrift(0.0f, 20.0f));
    const auto slow = withDrift(3.0f, 1.0f);
    EXPECT_NE(slow, withDrift(0.0f, 1.0f));
    EXPECT_NE(slow, withDrift(3.0f, 5.0f));
    EXPECT_NE(slow, withDrift(6.0f, 1.0f));

    // the same drift as the engine alone: same seed, same commands (applied at the first sample, depth ramped)
    ShipAhoyEngine engine;
    engine.setSampleRate(c_fs);
    engine.reset();
    engine.setResonance(85.0f);
    engine.setFilterType(1.0f);
    engine.cutoff(69.0f + 12.0f * std::log2(1000.0f / 440.0f), 0);
    engine.setModDepth(3.0f);
    engine.setModRate(1.0f);
    std::vector<float> reference(input.size());
    engine.process(input.data(), reference.data(), input.size());
    double worst = 0.0;
    for (size_t i = 0; i < input.size(); ++i)
    {
        worst = std::max(worst, std::abs(static_cast<double>(slow[i]) - reference[i]));
    }
    std::cout << "[ drift    ] plugin against engine: largest difference " << worst << "\n";
    EXPECT_LT(worst, 1e-3);
}

TEST_F(ShipAhoy, MixJumpHasNoStep)
{
    // the mix jumps from dry to wet and back between two blocks; the 20 ms smoothing must keep
    // the output as smooth as the dry and the wet signal themselves
    const auto input = sine(220.0, 0.5, 48000);
    writeUser(fixedFilter(c_centreHz, 0.0, "Lp12"));
    const Settings fixed{{"script", c_userScript}};
    auto dryOnly = fixed;
    dryOnly["mix"] = 0.0f;
    auto wetOnly = fixed;
    wetOnly["mix"] = 100.0f;
    SignalStats dry;
    SignalStats wet;
    const auto dryOut = run(dryOnly, input);
    const auto wetOut = run(wetOnly, input);
    dry.add(dryOut.data(), dryOut.size());
    wet.add(wetOut.data(), wetOut.size());
    auto plugin = instance(dryOnly);
    const auto output = plugin->process(input, 128, false,
                                        [&plugin](const size_t position)
                                        {
                                            if (position == 128 * 100)
                                            {
                                                plugin->setControl("mix", 100.0f);
                                            }
                                            else if (position == 128 * 250)
                                            {
                                                plugin->setControl("mix", 0.0f);
                                            }
                                        });
    SignalStats jumped;
    jumped.add(output.data(), output.size());
    const double smoothest = std::max(dry.maxDelta(), wet.maxDelta());
    EXPECT_LT(jumped.maxDelta(), 1.1 * smoothest) << "dry " << dry.maxDelta() << ", wet " << wet.maxDelta();
    std::cout << "[ mix jump ] max delta " << jumped.maxDelta() << " (dry " << dry.maxDelta() << ", wet "
              << wet.maxDelta() << ")\n";
}

TEST_F(ShipAhoy, AHeldNoteKeepsASineCleanAndHopMovesIt)
{
    // a script that holds a note changes nothing about a sine (well below the resonance peak, where the gain would
    // drive the stages into their +-1 clamp); the hops of the first factory script smear it
    const auto input = sine(250.0, 0.1, 48000 * 3);
    for (const bool hops : {false, true})
    {
        Settings settings{{"mix", 100.0f}, {"tempo", 240.0f}};
        std::vector<float> output;
        if (hops)
        {
            settings["script"] = c_hop;
            output = run(settings, input);
        }
        else
        {
            output = runFixed(c_centreHz, 85.0, "Lp12", input, settings);
        }
        ToneFit fit(250.0, c_fs);
        fit.add(output.data() + 24000, output.size() - 24000);
        const double residualDb = toDb(fit.result().residualRms / fit.result().amplitude);
        std::cout << "[ hold     ] " << (hops ? "hop" : "held note") << ": residual " << residualDb << " dB\n";
        if (!hops)
        {
            EXPECT_LT(residualDb, -90.0);
        }
        else
        {
            EXPECT_GT(residualDb, -30.0);
        }
    }
}

TEST_F(ShipAhoy, HopsOfTheFirstScriptAreWhiteInThreeOctavesAroundOneKilohertz)
{
    // a copy of hop.lua with the lp 6 dB type and Y = 0 (no resonance), a short glide: the positions measured on the
    // output are the random numbers of the script, uniform in [0, 1]
    writeUser(factoryScript("hop", "FilterType.Lp12", "FilterType.Lp6"));
    const auto input = sine(c_probeHz, c_probeAmplitude, 48000 * 20);
    const auto output =
        run({{"script", c_userScript}, {"mix", 100.0f}, {"tempo", 240.0f}, {"x", 1.0f}, {"y", 0.0f}}, input);
    const auto steps = stepSamples(240.0, ship_ahoy::c_defaultStep, input.size());
    ASSERT_GT(steps.size(), 300u);
    const auto positions = measuredPositions(output, steps, 1000.0, 3.0);
    const auto [lowest, highest] = std::minmax_element(positions.begin(), positions.end());
    EXPECT_GE(*lowest, -1e-3);
    EXPECT_LE(*highest, 1.0 + 1e-3);
    EXPECT_NEAR(mean(positions), 0.5, 0.03);
    EXPECT_NEAR(deviation(positions), 1.0 / std::sqrt(12.0), 0.02);
    EXPECT_NEAR(lagOneCorrelation(positions), 0.0, 0.1);
    std::cout << "[ hop      ] " << positions.size() << " steps: mean " << mean(positions) << ", deviation "
              << deviation(positions) << ", lag-one correlation " << lagOneCorrelation(positions) << "\n";
}

TEST_F(ShipAhoy, SlowDriftHopsFollowTheOrnsteinUhlenbeckWalk)
{
    // measured on the output of a copy with the lp 6 dB type and a short glide: centred (800 Hz, four octaves), 0.2 of
    // the range wide, consecutive steps correlated with e^(-0.1) = 0.905; X = 0 switches the wobble off
    std::string text = factoryScript("slow-drift", "FilterType.Lp24", "FilterType.Lp6");
    const size_t glide = text.find("glide = 1 ");
    ASSERT_NE(glide, std::string::npos);
    text.replace(glide, 9, "glide = 0.01");
    writeUser(text);
    const auto input = sine(c_probeHz, c_probeAmplitude, 48000 * 60);
    const auto output =
        run({{"script", c_userScript}, {"mix", 100.0f}, {"tempo", 240.0f}, {"x", 0.0f}, {"y", 0.0f}}, input);
    const auto positions =
        measuredPositions(output, stepSamples(240.0, ship_ahoy::c_defaultStep, input.size()), 800.0, 4.0);
    ASSERT_GT(positions.size(), 900u);
    EXPECT_NEAR(mean(positions), 0.5, 0.06);
    EXPECT_NEAR(deviation(positions), 0.2, 0.04);
    EXPECT_NEAR(lagOneCorrelation(positions), 0.905, 0.05);
    std::cout << "[ brown    ] " << positions.size() << " steps: mean " << mean(positions) << ", deviation "
              << deviation(positions) << ", lag-one correlation " << lagOneCorrelation(positions) << "\n";
}

TEST_F(ShipAhoy, TempoAndStepSetTheRateOfTheSteps)
{
    // steps per second = tempo / 60 * 96 / step ticks. Toggle trill alternates between the lower and the upper 40 % of
    // the range, so every step crosses the middle; the rate of those crossings on a 1 ms grid is the step rate
    struct Case
    {
        float tempo;
        float step; // index in the Step port
        double rate;
    };
    writeUser(factoryScript("toggle-trill", "FilterType.Bp6", "FilterType.Lp6"));
    for (const Case& c : {Case{60.0f, 7.0f, 4.0}, Case{105.0f, 7.0f, 7.0}, Case{240.0f, 7.0f, 16.0},
                          Case{60.0f, 6.0f, 2.0}, Case{120.0f, 5.0f, 2.0}, Case{30.0f, 9.0f, 8.0}})
    {
        const size_t length = static_cast<size_t>(c_fs * std::max(2.5, 14.0 / c.rate));
        const auto output = run(
            {{"script", c_userScript}, {"mix", 100.0f}, {"tempo", c.tempo}, {"step", c.step}, {"x", 1.0f}, {"y", 0.0f}},
            sine(c_probeHz, c_probeAmplitude, length));
        const size_t block = 48;
        std::vector<double> crossings;
        int previousSide = 0;
        // the filter rests at 1 kHz, the centre, until the first step (the clock starts one lead after activation): the
        // side of a position at 0.5 is noise, so the count starts after the first step
        const size_t skip = static_cast<size_t>(c_fs * (0.1 + 1.0 / c.rate));
        for (size_t begin = skip; begin + block <= output.size(); begin += block)
        {
            ToneFit fit(c_probeHz, c_fs);
            fit.add(output.data() + begin, block);
            const int side =
                positionFromProbeGain(fit.result().amplitude / c_probeAmplitude, 1000.0, 3.0) > 0.5 ? 1 : -1;
            if (previousSide != 0 && side != previousSide)
            {
                crossings.push_back(static_cast<double>(begin) / c_fs);
            }
            previousSide = side;
        }
        ASSERT_GE(crossings.size(), 10u) << c.tempo << " BPM, step " << c.step;
        const double rate = static_cast<double>(crossings.size() - 1) / (crossings.back() - crossings.front());
        EXPECT_NEAR(rate / c.rate, 1.0, 0.005)
            << c.tempo << " BPM, step " << ship_ahoy::c_stepLabels[size_t(c.step)] << ": measured " << rate;
        std::cout << "[ tempo    ] " << c.tempo << " BPM at " << ship_ahoy::c_stepLabels[static_cast<size_t>(c.step)]
                  << ": " << crossings.size() << " steps at " << rate << " Hz (expected " << c.rate << ")\n";
    }
}

TEST_F(ShipAhoy, EveryFactoryScriptMakesItsOwnSoundAndStaysFinite)
{
    const auto input = noise(48000 * 2, 0.5f);
    std::vector<std::vector<float>> outputs;
    for (const float script : {0.0f, 1.0f, 2.0f, 3.0f, 7.0f, 8.0f})
    {
        outputs.push_back(run({{"script", script}, {"tempo", 240.0f}}, input));
        SignalStats stats;
        stats.add(outputs.back().data(), outputs.back().size());
        EXPECT_EQ(stats.nonFinite(), 0u) << script;
    }
    for (size_t a = 0; a < outputs.size(); ++a)
    {
        for (size_t b = a + 1; b < outputs.size(); ++b)
        {
            EXPECT_NE(outputs[a], outputs[b]) << a << " vs " << b;
        }
    }
}

TEST_F(ShipAhoy, XAndYReachTheDefaultScript)
{
    // X is the glide and Y the resonance of hop.lua: a different X or Y is a different sound
    const auto input = noise(48000, 0.5f);
    const auto base = run({{"tempo", 240.0f}}, input);
    EXPECT_NE(base, run({{"tempo", 240.0f}, {"x", 80.0f}}, input));
    EXPECT_NE(base, run({{"tempo", 240.0f}, {"y", 20.0f}}, input));
}

TEST_F(ShipAhoy, OutOfRangeScriptValuesAreClamped)
{
    const auto input = noise(24000, 0.5f);
    EXPECT_EQ(run({{"script", 99.0f}}, input), run({{"script", 16.0f}}, input));
    EXPECT_EQ(run({{"script", -5.0f}}, input), run({{"script", 0.0f}}, input));
}

TEST_F(ShipAhoy, TheBundleShipsTheScriptsAndTheDocumentsAboutTheScriptApi)
{
    const fs::path scripts = s_bundle->dir / "scripts";
    for (const char* name : {"hop", "sequence", "random-hold", "alternating", "follow", "answer", "echo", "slow-drift",
                             "toggle-trill", "notes"})
    {
        EXPECT_TRUE(fs::exists(scripts / (std::string(name) + ".lua"))) << name;
    }
    const std::string api = readFile(scripts / "API.md");
    for (const char* fragment :
         {"# Ship Ahoy scripting API", "### SetCutoff(note, { fields })", "### SpeedRatio(ratio, { fields })",
          "### SetResonance(percent)", "### SetFilterType(type)", "### SetDrift(depth, { fields })", "### GetX()",
          "### GetY()", "### OnStep(step)", "### OnControl(x, y)", "### OnTick(tick)", "### SetTimer(id, { fields })"})
    {
        EXPECT_NE(api.find(fragment), std::string::npos) << fragment;
    }
    const std::string constants = readFile(scripts / "constants.lua");
    EXPECT_NE(constants.find("Ticks = {"), std::string::npos);
    EXPECT_NE(constants.find("FilterType = {"), std::string::npos);
}

TEST_F(ShipAhoy, TheTempoPortIsTheBoardTempoAndTheTransportPortTheTransportState)
{
    auto plugin = instance({});
    EXPECT_TRUE(plugin->hasWorker());
    EXPECT_EQ(plugin->audioInputs(), 1u);
    const std::string ttl = readFile(s_bundle->dir / "shipahoy.ttl");
    EXPECT_NE(ttl.find("lv2:designation time:beatsPerMinute"), std::string::npos);
    // the face's Tempo knob has no designation: the host would drive it from the pedalboard
    EXPECT_EQ(ttl.find("lv2:designation time:beatsPerMinute", ttl.find("lv2:designation time:beatsPerMinute") + 1),
              std::string::npos);
    const auto designated = ttl.rfind("lv2:symbol", ttl.find("lv2:designation time:beatsPerMinute"));
    EXPECT_NE(ttl.substr(designated, 40).find("\"hostTempo\""), std::string::npos) << ttl.substr(designated, 40);
    EXPECT_NE(ttl.find("lv2:designation time:speed"), std::string::npos);
}

namespace
{
double rmsOf(const std::vector<float>& x, const size_t begin, const size_t end)
{
    double sum = 0.0;
    for (size_t i = begin; i < end; ++i)
    {
        sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    }
    return std::sqrt(sum / static_cast<double>(end - begin));
}
}

TEST_F(ShipAhoy, AUserScriptSteersTheFilterAndABrokenOneChangesNothing)
{
    // a script that sends the filter to A5 (880 Hz) at every step must sound like the one that holds A5 from the start
    const auto input = noise(48000 * 2, 0.5f);
    const std::string setup = "    SetFilterType(FilterType.Lp12)\n    SetResonance(60)\n";
    const Settings scripted{{"mix", 100.0f}, {"script", c_userScript}};
    writeUser("function OnStart()\n" + setup + "end\nfunction OnStep(step)\n    SetCutoff(81, { glide = 0 })\nend\n");
    auto plugin = instance(scripted);
    plugin->startWorkerThread();
    const auto output = plugin->process(input, 128);
    EXPECT_EQ(plugin->output("scriptStatus"), 1.0f);
    writeUser("function OnStart()\n" + setup + "    SetCutoff(81, { glide = 0 })\nend\n", 2);
    const auto reference = run(scripted, input);
    const size_t from = 48000;
    EXPECT_NEAR(rmsOf(output, from, output.size()), rmsOf(reference, from, reference.size()),
                0.01 * rmsOf(reference, from, reference.size()));
    double worst = 0.0;
    for (size_t i = from; i < output.size(); ++i)
    {
        worst = std::max(worst, static_cast<double>(std::abs(output[i] - reference[i])));
    }
    EXPECT_LT(worst, 1e-3) << "same filter after the first second";
    std::cout << "[ script   ] A5 by step against held: largest difference " << worst << "\n";

    // a script that does not load: the one before keeps steering
    writeUser("function OnStep(step  -- oops", 3);
    auto again = instance(scripted);
    again->startWorkerThread();
    std::ignore = again->process(noise(24000, 0.5f), 128);
    writeUser("function OnStep(step  -- still oops", 5);
    std::ignore = again->process(noise(48000, 0.5f), 128);
    EXPECT_EQ(again->output("scriptStatus"), 3.0f) << "compile error";
}

TEST_F(ShipAhoy, ANotePlayedIntoThePluginSteersTheFilterThroughOnNewPitch)
{
    // a script that sends the filter to the note it hears must make a played A5 (880 Hz) sound like a filter held at
    // 880 Hz (the commands of the hook are applied at the next block, without the lead)
    const auto input = sine(880.0, 0.3, 48000 * 2);
    const std::string setup = "    SetFilterType(FilterType.Lp12)\n    SetResonance(60)\n";
    const Settings played{{"mix", 100.0f}, {"script", c_userScript}};
    writeUser("function OnStart()\n" + setup +
              "end\nfunction OnNewPitch(note, hz, pitch, periodicity, level)\n"
              "    SetCutoff(pitch, { glide = 0 })\nend\n");
    auto plugin = instance(played);
    plugin->startWorkerThread();
    const auto output = plugin->process(input, 128);
    EXPECT_EQ(plugin->output("scriptStatus"), 1.0f);
    writeUser("function OnStart()\n" + setup + "    SetCutoff(81, { glide = 0 })\nend\n", 2);
    const auto reference = run(played, input);
    const size_t from = 48000;
    EXPECT_NEAR(rmsOf(output, from, output.size()), rmsOf(reference, from, reference.size()),
                0.01 * rmsOf(reference, from, reference.size()));
    std::cout << "[ played   ] A5 played, filter at its note: rms " << rmsOf(output, from, output.size())
              << " against the held filter's " << rmsOf(reference, from, reference.size()) << "\n";
}

}
