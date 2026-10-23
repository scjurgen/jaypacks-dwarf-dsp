// Tests of the morphexsynth plugin through its built LV2 binary (<build>/lv2/jaypacks-morphexsynth.lv2): notes
// from MIDI, the per-channel bend, the patches, twelve voices for ten seconds, the voice steal.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

#include "Lv2Host.h"
#include "tests/unit/TestHelpers.h"

namespace jp::host
{

namespace
{
constexpr double c_rate{48000.0};

// a sanitizer build runs many times slower, so a time budget means nothing there
#if defined(__SANITIZE_ADDRESS__)
constexpr bool c_sanitized{true};
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(undefined_behavior_sanitizer)
constexpr bool c_sanitized{true};
#else
constexpr bool c_sanitized{false};
#endif
#else
constexpr bool c_sanitized{false};
#endif

double rms(const std::vector<float>& x, const size_t from, const size_t to)
{
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
    {
        sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    }
    return std::sqrt(sum / static_cast<double>(to - from));
}

double peakNear(const std::vector<float>& signal, const double centreHz)
{
    double best = 0.0;
    double bestFrequency = centreHz;
    for (int cent = -100; cent <= 100; ++cent)
    {
        const double frequency = centreHz * std::pow(2.0, cent / 1200.0);
        const double level = std::abs(jp::test::toneAmplitude(signal, frequency, c_rate));
        if (level > best)
        {
            best = level;
            bestFrequency = frequency;
        }
    }
    return bestFrequency;
}

std::vector<float> silence(const size_t n)
{
    return std::vector<float>(n, 0.0f);
}
}

class Morphexsynth : public ::testing::Test
{
  protected:
    static void SetUpTestSuite()
    {
        s_bundle = std::make_unique<BundleInfo>(readBundle(fs::path(JP_LV2_DIR) / "jaypacks-morphexsynth.lv2"));
        s_library = std::make_unique<Library>(*s_bundle);
    }

    static void TearDownTestSuite()
    {
        s_library.reset();
        s_bundle.reset();
    }

    static std::unique_ptr<Instance> instance(const double sampleRate = c_rate)
    {
        return std::make_unique<Instance>(s_library->descriptor(), *s_bundle, sampleRate);
    }

    static std::unique_ptr<BundleInfo> s_bundle;
    static std::unique_ptr<Library> s_library;
};

std::unique_ptr<BundleInfo> Morphexsynth::s_bundle;
std::unique_ptr<Library> Morphexsynth::s_library;

TEST_F(Morphexsynth, HasAMidiInputAndNoAudioInput)
{
    auto synth = instance();
    EXPECT_TRUE(synth->hasMidiInput());
    EXPECT_EQ(synth->audioInputs(), 0u);
}

TEST_F(Morphexsynth, ANoteFromMidiSoundsFromItsFrameOnAndStopsAfterItsRelease)
{
    auto synth = instance();
    synth->setControl("patch", 9);
    synth->sendMidi(4800, 0x90, 45, 100);
    synth->sendMidi(24000, 0x80, 45, 64);
    const auto out = synth->process(silence(48000), 128);
    EXPECT_EQ(rms(out, 0, 4800), 0.0) << "nothing before the note on";
    EXPECT_GT(rms(out, 9600, 24000), 0.02);
    EXPECT_LT(rms(out, 40000, 48000), 1e-3) << "released at 24000, 120 ms release, a reverb tail of 0.6 s";
}

TEST_F(Morphexsynth, EachPatchSoundsDifferentAndStaysBounded)
{
    std::vector<double> levels;
    std::vector<std::vector<float>> outputs;
    for (int patch = 0; patch < 10; ++patch)
    {
        auto synth = instance();
        synth->setControl("patch", static_cast<float>(patch));
        synth->sendMidi(0, 0x90, 60, 110);
        synth->sendMidi(0, 0x90, 67, 110);
        const auto out = synth->process(silence(48000), 128);
        EXPECT_TRUE(std::all_of(out.begin(), out.end(), [](float v) { return std::isfinite(v) && std::abs(v) < 4.0f; }))
            << patch;
        EXPECT_GT(rms(out, 4800, 24000), 3e-4) << patch;
        outputs.push_back(out);
    }
    for (size_t a = 0; a < outputs.size(); ++a)
    {
        for (size_t b = a + 1; b < outputs.size(); ++b)
        {
            EXPECT_NE(outputs[a], outputs[b]) << a << " and " << b;
        }
    }
}

TEST_F(Morphexsynth, ThePitchBendOfOneChannelMovesOnlyItsNote)
{
    auto synth = instance();
    synth->setControl("patch", 2);
    synth->setControl("bendRange", 2);
    synth->setControl("reverb", -60);
    synth->setControl("chorus", 0);
    synth->setControl("phaser", 0);
    synth->sendMidi(0, 0x91, 57, 100);
    synth->sendMidi(0, 0x92, 69, 100);
    synth->sendMidi(24000, 0xE2, 0x7F, 0x7F);
    const auto out = synth->process(silence(72000), 128);
    const std::vector<float> window(out.begin() + 36000, out.begin() + 60000);
    const double up = 440.0 * std::pow(2.0, 2.0 / 12.0);
    EXPECT_NEAR(1200.0 * std::log2(peakNear(window, 220.0) / 220.0), 0.0, 8.0);
    EXPECT_NEAR(1200.0 * std::log2(peakNear(window, up) / up), 0.0, 8.0);
}

TEST_F(Morphexsynth, TwelveVoicesSoundForTenSecondsWithinTheTimeBudget)
{
    auto synth = instance();
    synth->setControl("patch", 3);
    for (int i = 0; i < 12; ++i)
    {
        synth->sendMidi(static_cast<uint64_t>(i * 100), 0x90, static_cast<uint8_t>(40 + 3 * i), 100);
    }
    const size_t length = 480000;
    const auto start = std::chrono::steady_clock::now();
    const auto out = synth->process(silence(length), 128);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    EXPECT_TRUE(std::all_of(out.begin(), out.end(), [](float v) { return std::isfinite(v) && std::abs(v) < 4.0f; }));
    EXPECT_GT(rms(out, length - 48000, length), 0.01) << "the chord is still alive after ten seconds";
    // ten seconds of audio: the Mac has far more time than the Dwarf's; this only catches a runaway
    if (!c_sanitized)
    {
        EXPECT_LT(seconds, 3.0);
    }
    RecordProperty("seconds_for_10s_of_12_voices", std::to_string(seconds));
}

TEST_F(Morphexsynth, AThirteenthNoteStealsAVoiceWithoutAClick)
{
    const auto largestStep = [](const std::vector<float>& x, const size_t from, const size_t to)
    {
        float largest = 0.0f;
        for (size_t i = from + 1; i < to; ++i)
        {
            largest = std::max(largest, std::abs(x[i] - x[i - 1]));
        }
        return largest;
    };
    auto synth = instance();
    synth->setControl("patch", 1);
    for (int i = 0; i < 12; ++i)
    {
        synth->sendMidi(static_cast<uint64_t>(i * 2400), 0x90, static_cast<uint8_t>(40 + 2 * i), 100);
    }
    synth->sendMidi(96000, 0x90, 80, 100);
    const auto out = synth->process(silence(120000), 128);
    const float before = largestStep(out, 80000, 96000);
    const float around = largestStep(out, 96000, 96480);
    EXPECT_LT(around, 3.0f * before) << "before " << before << ", around the steal " << around;
}

TEST_F(Morphexsynth, ThePressureSettingScalesTheEffectOfChannelPressure)
{
    // Section Brass: Z opens the cutoff
    const auto level = [](const float percent)
    {
        auto synth = instance();
        synth->setControl("patch", 8);
        synth->setControl("pressure", percent);
        synth->setControl("reverb", -60);
        synth->setControl("chorus", 0);
        synth->setControl("phaser", 0);
        synth->sendMidi(0, 0x91, 60, 100);
        synth->sendMidi(10, 0xD1, 127, 0, 2);
        const auto out = synth->process(silence(48000), 128);
        return rms(out, 24000, 48000);
    };
    EXPECT_GT(level(200.0f), 1.05 * level(0.0f));
    EXPECT_GT(level(400.0f), level(200.0f));
}

TEST_F(Morphexsynth, AttackAndReleaseOffsetTheTimesOfTheAmplitudeEnvelope)
{
    // Warm Bass attacks in 2 ms and releases in 120 ms; the note is released after 0.5 s
    const auto levels = [](const float attackMs, const float releaseMs)
    {
        auto synth = instance();
        synth->setControl("patch", 9);
        synth->setControl("attack", attackMs);
        synth->setControl("release", releaseMs);
        synth->setControl("reverb", -60);
        synth->setControl("chorus", 0);
        synth->setControl("phaser", 0);
        synth->sendMidi(0, 0x91, 45, 100);
        synth->sendMidi(24000, 0x81, 45, 64);
        const auto out = synth->process(silence(48000), 128);
        return std::make_pair(rms(out, 0, 1920), rms(out, 33600, 38400));
    };
    const auto plain = levels(0.0f, 0.0f);
    const auto slow = levels(300.0f, 2000.0f);
    EXPECT_GT(plain.first, 0.0);
    EXPECT_LT(slow.first, 0.2 * plain.first) << "attack";
    EXPECT_GT(slow.second, 100.0 * std::max(plain.second, 1e-9)) << "release";
}

TEST_F(Morphexsynth, TheMpeZonePortsGateTheChannels)
{
    auto synth = instance();
    synth->setControl("mpeUpper", 5);
    synth->sendMidi(0, 0x97, 60, 100);
    const auto silent = synth->process(silence(24000), 128);
    EXPECT_EQ(rms(silent, 0, silent.size()), 0.0) << "channel 8 is outside the zone";

    auto other = instance();
    other->setControl("mpeUpper", 5);
    other->sendMidi(0, 0x92, 60, 100);
    const auto played = other->process(silence(24000), 128);
    EXPECT_GT(rms(played, 12000, 24000), 1e-3);
}


namespace
{

/// The folder the plugins of this test process read the user's scripts from (JP_SCRIPT_DIR/morphexsynth), emptied.
fs::path userScripts(const std::string& name)
{
    const fs::path base = fs::current_path() / "morphex-host-test" / (name + "-" + std::to_string(::getpid()));
    fs::remove_all(base);
    fs::create_directories(base / "morphexsynth");
    ::setenv("JP_SCRIPT_DIR", base.c_str(), 1);
    return base / "morphexsynth";
}

void writeScript(const fs::path& path, const std::string& text, const int edit)
{
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << text;
    }
    fs::last_write_time(path, fs::file_time_type::clock::now() + std::chrono::seconds(edit));
}

std::string scriptWithVolume(const int db)
{
    return "function OnStart()\n    SetVolume(" + std::to_string(db) +
           ")\n    SetOscillator(0, { waveform = OscWaveform.Saw, level = 0.5 })\n"
           "    SetAmpEnvelope({ attackMs = 2, decayMs = 100, sustainLevel = 0.8, releaseMs = 50 })\n"
           "    SetFilter({ cutoff = 100 })\nend\n";
}

}

TEST_F(Morphexsynth, TheBundleShipsTheFactoryScriptsAndTheDocumentsAboutTheScriptApi)
{
    const fs::path scripts = s_bundle->dir / "scripts";
    for (const char* name : {"4-ever-mini", "airy-pad", "crystal-bells", "cutting-lead", "grand-stage", "harp-pluck",
                             "kick-punch", "retro-poly", "section-brass", "warm-bass"})
    {
        EXPECT_TRUE(fs::exists(scripts / (std::string(name) + ".lua"))) << name;
    }
    const std::string api = readFile(scripts / "API.md");
    for (const char* fragment :
         {"# Morphexsynth scripting API", "### SetOscillator(index, { fields })", "### SetWaveMorph(index, { fields })",
          "### OnNoteOn(channel, note, velocity)", "## Filter types", "`CtrlTarget.FilterCutoff`"})
    {
        EXPECT_NE(api.find(fragment), std::string::npos) << fragment;
    }
    EXPECT_NE(readFile(scripts / "constants.lua").find("OscWaveform = {"), std::string::npos);
}

TEST_F(Morphexsynth, AUserScriptIsLoadedByTheWorkerAndReloadedWhenItsFileChanges)
{
    const fs::path folder = userScripts("worker");
    writeScript(folder / "user-1.lua", scriptWithVolume(-6), 1);
    auto synth = instance();
    ASSERT_TRUE(synth->hasWorker());
    synth->setControl("reverb", -60);
    synth->setControl("vol", 0);
    synth->setControl("script", 1);
    std::ignore = synth->process(silence(1024), 128); // the select, the worker, the commands: a few blocks
    EXPECT_EQ(synth->output("scriptStatus"), 1.0f);
    EXPECT_GT(synth->jobsScheduled(), 0u);
    synth->sendMidi(0, 0x90, 60, 100);
    const auto quiet = synth->process(silence(24000), 128);
    const double quietLevel = rms(quiet, 12000, 24000);
    EXPECT_GT(quietLevel, 0.003);

    writeScript(folder / "user-1.lua", scriptWithVolume(6), 2);
    std::ignore = synth->process(silence(30000), 128); // more than the half second after which the file is checked
    std::ignore = synth->process(silence(4800), 128);
    EXPECT_EQ(synth->output("scriptStatus"), 1.0f);
    synth->sendMidi(0, 0x90, 60, 100);
    const auto loud = synth->process(silence(24000), 128);
    EXPECT_GT(rms(loud, 12000, 24000), 3.0 * quietLevel) << "12 dB louder";

    writeScript(folder / "user-1.lua", "function OnStart( oops", 3);
    std::ignore = synth->process(silence(30000), 128);
    std::ignore = synth->process(silence(4800), 128);
    EXPECT_EQ(synth->output("scriptStatus"), 3.0f) << "compile error";
    synth->sendMidi(0, 0x90, 60, 100);
    const auto kept = synth->process(silence(24000), 128);
    EXPECT_NEAR(rms(kept, 12000, 24000), rms(loud, 12000, 24000), 0.05 * rms(loud, 12000, 24000))
        << "the previous script keeps sounding";
    ::unsetenv("JP_SCRIPT_DIR");
}

TEST_F(Morphexsynth, TheSameOnTheHostsWorkerThreadWithNotesAndHooks)
{
    const fs::path folder = userScripts("thread");
    writeScript(folder / "user-2.lua",
                scriptWithVolume(-6) + "function OnNoteOn(ch, note, vel) if vel > 100 then SetVolume(-40) end end\n",
                1);
    auto synth = instance();
    synth->startWorkerThread();
    synth->setControl("reverb", -60);
    synth->setControl("vol", 0);
    synth->setControl("script", 2);
    std::ignore = synth->process(silence(512), 128);
    synth->waitForWorker();
    std::ignore = synth->process(silence(512), 128);
    EXPECT_EQ(synth->output("scriptStatus"), 1.0f);

    synth->sendMidi(0, 0x90, 60, 90); // soft: the hook leaves the patch alone
    const auto soft = synth->process(silence(24000), 128);
    synth->waitForWorker();
    synth->sendMidi(0, 0x91, 62, 120); // hard: the hook turns the patch down
    std::ignore = synth->process(silence(2048), 128);
    synth->waitForWorker();
    const auto hard = synth->process(silence(24000), 128);
    EXPECT_GT(rms(soft, 12000, 24000), 0.003);
    EXPECT_LT(rms(hard, 12000, 24000), 0.3 * rms(soft, 12000, 24000)) << "after the hook, the patch is 34 dB down";
    EXPECT_EQ(synth->output("scriptStatus"), 1.0f);
    ::unsetenv("JP_SCRIPT_DIR");
}
}
