// Morphexsynth with scripts: the user's script slot, hot reload, hooks, the factory scripts as values, and what the
// audio thread does with what the worker sends.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

#include "lv2/PedalServices.h"
#include "plugins/morphexsynth/MorphexsynthPedal.h"

namespace jp::test
{

namespace fs = std::filesystem;

namespace
{

constexpr float c_rate{48000.0f};

struct Stereo
{
    std::vector<float> left;
    std::vector<float> right;
};

/// The host's wake-up job as the pedal sees it: counts them.
struct WorkerStub
{
    WorkerStub()
    {
        schedule.handle = this;
        schedule.schedule_work = [](LV2_Worker_Schedule_Handle handle, uint32_t, const void*)
        {
            ++static_cast<WorkerStub*>(handle)->jobs;
            return LV2_WORKER_SUCCESS;
        };
    }

    LV2_Worker_Schedule schedule{};
    int jobs{0};
};

/// A pedal, its scripts folders and the time. work() plays the host's worker thread.
class Rig
{
  public:
    explicit Rig(const std::string& name, const bool activate = true)
        : m_folder(fs::current_path() / "morphex-script-test" / (name + "-" + std::to_string(::getpid())))
        , m_pedal(std::make_unique<MorphexsynthPedal>())
        , m_scheduler()
    {
        fs::remove_all(m_folder);
        fs::create_directories(m_folder / "user");
        m_pedal->setScriptDirectories(std::string(JP_SOURCE_DIR) + "/plugins/morphexsynth/scripts",
                                      (m_folder / "user").string());
        m_scheduler.setSchedule(&m_worker.schedule);
        m_pedal->setWorkScheduler(m_scheduler);
        m_pedal->setSampleRate(c_rate);
        if (activate)
        {
            m_pedal->reset();
        }
    }

    ~Rig()
    {
        m_pedal.reset();
        fs::remove_all(m_folder);
    }

    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    MorphexsynthPedal& pedal()
    {
        return *m_pedal;
    }

    int wakeUps() const
    {
        return m_worker.jobs;
    }

    /// Writes user-<n>.lua with a modification time of its own.
    void writeUser(const int slot, const std::string& text)
    {
        const fs::path path = m_folder / "user" / ("user-" + std::to_string(slot) + ".lua");
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << text;
        }
        fs::last_write_time(path, fs::file_time_type::clock::now() + std::chrono::seconds(++m_edits));
    }

    [[nodiscard]] std::string readLog(const std::string& name) const
    {
        std::ifstream in(m_folder / "user" / (name + ".log"));
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return text;
    }

    /// The host's worker thread runs the pedal's work() once.
    void work()
    {
        WorkResponder responder(nullptr, nullptr);
        m_pedal->work(nullptr, 0, responder);
    }

    void midi(const uint8_t status, const uint8_t data1 = 0, const uint8_t data2 = 0)
    {
        const std::array<uint8_t, 3> message{status, data1, data2};
        m_pedal->midi(message.data(), 3);
    }

    void noteOn(const int channel, const int note, const int velocity = 100)
    {
        midi(static_cast<uint8_t>(0x90 | (channel - 1)), static_cast<uint8_t>(note), static_cast<uint8_t>(velocity));
    }

    Stereo render(const size_t numSamples, const size_t blockSize = 128)
    {
        Stereo out;
        out.left.resize(numSamples);
        out.right.resize(numSamples);
        for (size_t pos = 0; pos < numSamples; pos += blockSize)
        {
            const size_t n = std::min(blockSize, numSamples - pos);
            std::array<float*, 2> outputs{out.left.data() + pos, out.right.data() + pos};
            m_pedal->process(nullptr, outputs.data(), n);
        }
        return out;
    }

  private:
    fs::path m_folder;
    std::unique_ptr<MorphexsynthPedal> m_pedal;
    WorkerStub m_worker;
    WorkScheduler m_scheduler;
    int m_edits{0};
};

double rms(const std::vector<float>& x, const size_t from, const size_t to)
{
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
    {
        sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    }
    return std::sqrt(sum / static_cast<double>(to - from));
}

bool identical(const Stereo& a, const Stereo& b)
{
    return a.left == b.left && a.right == b.right;
}

std::string factoryText(const std::string& name)
{
    std::string text;
    EXPECT_TRUE(script::detail::readTextFile(
        std::string(JP_SOURCE_DIR) + "/plugins/morphexsynth/scripts/" + name + ".lua", 100000, text));
    return text;
}

const char* c_quietPatch = "function OnStart()\n"
                           "    SetVolume(-6)\n"
                           "    SetOscillator(0, { waveform = OscWaveform.Saw, level = 0.5 })\n"
                           "    SetAmpEnvelope({ attackMs = 2, decayMs = 100, sustainLevel = 0.8, releaseMs = 50 })\n"
                           "    SetFilter({ cutoff = 100, resonance = 0.1 })\n"
                           "end\n";

}

TEST(MorphexsynthScript, AUserScriptThatIsAFactoryScriptSoundsLikeTheFactoryPatchBitForBit)
{
    Rig factory("same-factory");
    factory.pedal().setPatch(3);
    Rig user("same-user", false);
    user.writeUser(1, factoryText("cutting-lead"));
    user.pedal().setScript(1);
    user.pedal().reset();
    factory.pedal().reset();
    EXPECT_EQ(user.pedal().getScriptStatus(), 1.0f);
    for (Rig* rig : {&factory, &user})
    {
        rig->noteOn(1, 60, 100);
        rig->noteOn(2, 67, 90);
    }
    const Stereo a = factory.render(24000);
    const Stereo b = user.render(24000);
    EXPECT_GT(rms(a.left, 2000, 24000), 0.001);
    EXPECT_TRUE(identical(a, b)) << "a script is a script, whichever folder it comes from";
}

TEST(MorphexsynthScript, AScriptSelectedWhileTheSynthRunsTakesOverAtTheNextBlockAfterTheWorkerLoadedIt)
{
    Rig rig("takeover");
    rig.writeUser(2, c_quietPatch);
    rig.noteOn(1, 60);
    const Stereo before = rig.render(4800);
    rig.pedal().setScript(2);
    EXPECT_GT(rig.wakeUps(), 0) << "selecting wakes the worker";
    const Stereo unchanged = rig.render(2400);
    EXPECT_GT(rms(unchanged.left, 0, 2400), 0.005) << "the note goes on until the new script has loaded";
    EXPECT_EQ(rig.pedal().getScriptStatus(), 0.0f) << "nothing loaded yet";
    rig.work();
    EXPECT_EQ(rig.pedal().getScriptStatus(), 1.0f);
    rig.render(2400); // the takeover: the note is released
    EXPECT_GT(rms(before.left, 2400, 4800), 0.005);
    const Stereo tail = rig.render(4 * 48000);
    EXPECT_LT(rms(tail.left, 3 * 48000, 4 * 48000), 1e-5) << "the note was released when the script took over";
    rig.noteOn(1, 60);
    const Stereo played = rig.render(9600);
    EXPECT_GT(rms(played.left, 4800, 9600), 0.01) << "the new sound plays";
    EXPECT_LT(rms(played.left, 4800, 9600), rms(before.left, 2400, 4800)) << "and is quieter than the factory patch";
}

TEST(MorphexsynthScript, ABrokenScriptKeepsTheSoundAndTheStatusAndTheLogSayWhy)
{
    Rig rig("broken");
    rig.writeUser(1, "function OnStart( SetVolume(0) end");
    rig.pedal().setScript(1);
    rig.work();
    EXPECT_EQ(rig.pedal().getScriptStatus(), 3.0f);
    rig.noteOn(1, 60);
    EXPECT_GT(rms(rig.render(9600).left, 4800, 9600), 0.005) << "the factory patch still plays";
    const std::string log = rig.readLog("user-1");
    EXPECT_NE(log.find("state: compile error"), std::string::npos) << log;
    EXPECT_NE(log.find("user-1:1:"), std::string::npos) << log;
    rig.writeUser(1, "function OnStart() error('late') end");
    rig.render(24000);
    rig.work();
    EXPECT_EQ(rig.pedal().getScriptStatus(), 4.0f) << "the next check found the new text";
    rig.writeUser(1, c_quietPatch);
    rig.render(24000);
    rig.work();
    EXPECT_EQ(rig.pedal().getScriptStatus(), 1.0f) << "and the fixed one";
}

TEST(MorphexsynthScript, AMissingFileIsReportedAndTheFactorySoundStays)
{
    Rig rig("missing");
    rig.pedal().setScript(4);
    rig.work();
    EXPECT_EQ(rig.pedal().getScriptStatus(), 2.0f);
    rig.noteOn(1, 60);
    EXPECT_GT(rms(rig.render(9600).left, 4800, 9600), 0.005);
    rig.writeUser(4, c_quietPatch);
    rig.render(24000);
    rig.work();
    EXPECT_EQ(rig.pedal().getScriptStatus(), 1.0f) << "the file appeared";
}

TEST(MorphexsynthScript, AnEditedFileIsReloadedAtTheNextCheckAndTheNotesAreReleased)
{
    Rig rig("reload");
    rig.writeUser(1, c_quietPatch);
    rig.pedal().setScript(1);
    rig.work();
    rig.render(512);
    rig.noteOn(1, 60);
    const Stereo first = rig.render(9600);
    const double quiet = rms(first.left, 4800, 9600);
    EXPECT_GT(quiet, 0.005);
    std::string louder = c_quietPatch;
    louder.replace(louder.find("SetVolume(-6)"), 13, "SetVolume(6)");
    rig.writeUser(1, louder);
    rig.render(24000); // half a second: the check is due
    rig.work();
    rig.render(48000);
    EXPECT_EQ(rig.pedal().activeVoices(), 0u) << "all notes were released by the new script";
    rig.noteOn(1, 60);
    const Stereo second = rig.render(9600);
    EXPECT_GT(rms(second.left, 4800, 9600), quiet * 3.0) << "12 dB louder";
}

TEST(MorphexsynthScript, AHookActsAfterTheWorkerRanAndOnlyHooksTheScriptDefinesWakeTheWorker)
{
    Rig rig("hooks");
    rig.writeUser(1, std::string(c_quietPatch) + "function OnNoteOn(ch, note, vel) SetVolume(-60) end\n");
    rig.pedal().setScript(1);
    rig.work();
    rig.render(512);
    const int baseline = rig.wakeUps();
    rig.midi(0xE1, 0, 64);  // a pitch bend: the script has no OnPitchBend
    rig.midi(0xB1, 74, 30); // a controller: no OnCC
    rig.midi(0xC1, 5);      // a program change: no OnProgramChange
    rig.render(512);
    EXPECT_EQ(rig.wakeUps(), baseline) << "no event for a hook that is not there";
    rig.noteOn(1, 60);
    EXPECT_GT(rig.wakeUps(), baseline);
    const Stereo early = rig.render(2400);
    EXPECT_GT(rms(early.left, 600, 2400), 0.005) << "the hook has not run yet";
    rig.work();
    const Stereo late = rig.render(9600);
    EXPECT_LT(rms(late.left, 4800, 9600), 1e-3) << "the hook made the patch 54 dB quieter";
}

namespace
{
Stereo playScript(const std::string& name, const std::string& text, const size_t length = 12000)
{
    Rig rig(name, false);
    rig.writeUser(1, text);
    rig.pedal().setScript(1);
    rig.pedal().reset();
    rig.noteOn(1, 57, 100);
    return rig.render(length);
}
}

TEST(MorphexsynthScript, AWaveMorphAtZeroChangesNothingAndAtOneSoundsLikeTheThirdWaveformFromTheFirstSample)
{
    const std::string base = "function OnStart()\n"
                             "    SetVolume(-6)\n"
                             "    SetAmpEnvelope({ attackMs = 2, decayMs = 100, sustainLevel = 0.8, releaseMs = 50 })\n"
                             "    SetFilter({ cutoff = 100 })\n"
                             "    SetOscillator(0, { waveform = OscWaveform.%s, level = 0.5, pwm = 0.3 })\n"
                             "%s"
                             "end\n";
    const auto script = [&](const char* wave, const char* extra)
    {
        char text[1024];
        std::snprintf(text, sizeof(text), base.c_str(), wave, extra);
        return std::string(text);
    };
    const Stereo plain = playScript("morph-plain", script("Saw", ""));
    const Stereo zero = playScript(
        "morph-zero",
        script("Saw", "    SetWaveMorph(0, {waveform2 = OscWaveform.White, waveform3 = OscWaveform.Square})\n"));
    EXPECT_TRUE(identical(plain, zero)) << "morph 0 is exactly the oscillator without a morph";
    const Stereo full = playScript("morph-full", script("Saw", "    SetWaveMorph(0, {waveform2 = OscWaveform.White, "
                                                               "waveform3 = OscWaveform.Triangle, morph = 1})\n"));
    const Stereo triangle = playScript("morph-triangle", script("Triangle", ""));
    EXPECT_TRUE(identical(full, triangle))
        << "morph 1 reads the third waveform alone, from the first sample of the note";
    EXPECT_FALSE(identical(full, plain));
}

TEST(MorphexsynthScript, AHookMovesTheMorphWhileANoteSoundsWithoutAClick)
{
    const auto run = [](const char* name, const bool controller)
    {
        Rig rig(name);
        rig.writeUser(1, std::string(c_quietPatch) +
                             "function OnCC(ch, cc, value) SetWaveMorph(0, {waveform2 = OscWaveform.Square, "
                             "waveform3 = OscWaveform.Triangle, morph = value / 127}) end\n");
        rig.pedal().setScript(1);
        rig.work();
        rig.render(512);
        rig.noteOn(1, 45, 100);
        rig.render(9600);
        if (controller)
        {
            rig.midi(0xB0, 1, 127);
        }
        rig.render(128);
        rig.work(); // the host's worker runs the hook
        return rig.render(9600);
    };
    const Stereo moved = run("morph-hook", true);
    const Stereo still = run("morph-still", false);
    double difference = 0.0;
    double largestStep = 0.0;
    double largestStill = 0.0;
    for (size_t i = 1; i < moved.left.size(); ++i)
    {
        difference += std::abs(static_cast<double>(moved.left[i] - still.left[i]));
        largestStep = std::max(largestStep, static_cast<double>(std::abs(moved.left[i] - moved.left[i - 1])));
        largestStill = std::max(largestStill, static_cast<double>(std::abs(still.left[i] - still.left[i - 1])));
        ASSERT_TRUE(std::isfinite(moved.left[i]));
    }
    EXPECT_GT(difference / static_cast<double>(moved.left.size()), 0.001) << "the hook changed the sound";
    EXPECT_LE(largestStep, largestStill * 1.2) << "the move is a ramp, not a jump";
}

TEST(MorphexsynthScript, TheHooksGetTheChannelNumberFromZeroAndAZeroVelocityNoteOnIsANoteOff)
{
    Rig rig("hookargs");
    rig.writeUser(1, std::string(c_quietPatch) + "function OnNoteOn(ch, note, vel) Log('on', ch, note, vel) end\n"
                                                 "function OnNoteOff(ch, note, vel) Log('off', ch, note, vel) end\n"
                                                 "function OnCC(ch, cc, value) Log('cc', ch, cc, value) end\n"
                                                 "function OnProgramChange(ch, p) Log('pc', ch, p) end\n"
                                                 "function OnPitchBend(ch, b) Log('bend', ch, b) end\n");
    rig.pedal().setScript(1);
    rig.work();
    rig.noteOn(3, 64, 99);
    rig.midi(0x92, 64, 0);
    rig.midi(0x82, 64, 33);
    rig.midi(0xB4, 74, 100);
    rig.midi(0xC5, 9);
    rig.midi(0xE6, 0x00, 0x00);
    rig.midi(0xE6, 0x7F, 0x7F);
    rig.render(512);
    rig.work();
    const std::string log = rig.readLog("user-1");
    for (const char* line :
         {"on 2 64 99", "off 2 64 64", "off 2 64 33", "cc 4 74 100", "pc 5 9", "bend 6 -8192", "bend 6 8191"})
    {
        EXPECT_NE(log.find(line), std::string::npos) << line << "\n" << log;
    }
}

TEST(MorphexsynthScript, MidiOutsideTheZoneDoesNotReachTheScript)
{
    Rig rig("zone");
    rig.writeUser(1, std::string(c_quietPatch) + "function OnNoteOn(ch, note, vel) SetVolume(-60) end\n");
    rig.pedal().setScript(1);
    rig.work();
    rig.pedal().setMpeLower(3);
    rig.pedal().setMpeUpper(4);
    rig.render(128);
    const int baseline = rig.wakeUps();
    rig.noteOn(2, 60); // channel 2 is below the member channels 3 and 4
    rig.noteOn(9, 60);
    EXPECT_EQ(rig.wakeUps(), baseline);
    rig.noteOn(3, 60);
    EXPECT_GT(rig.wakeUps(), baseline);
}

TEST(MorphexsynthScript, AScriptCanMoveTheMpeZone)
{
    Rig rig("setzone");
    rig.writeUser(1, std::string(c_quietPatch) + "SetMpeZone(1, 5, 6)\n");
    rig.pedal().setScript(1);
    rig.work();
    rig.render(128);
    rig.noteOn(2, 60);
    EXPECT_LT(rms(rig.render(4800).left, 2400, 4800), 1e-4) << "channel 2 is outside the zone the script set";
    rig.noteOn(5, 60);
    EXPECT_GT(rms(rig.render(4800).left, 2400, 4800), 0.005);
}

TEST(MorphexsynthScript, ThePatchPortWaitsWhileAScriptIsSelectedAndComesBackWithPortZero)
{
    Rig rig("port");
    rig.writeUser(1, c_quietPatch);
    rig.pedal().setScript(1);
    rig.work();
    rig.render(128);
    rig.noteOn(1, 60);
    const double quiet = rms(rig.render(9600).left, 4800, 9600);
    rig.pedal().setPatch(0);
    rig.render(128);
    rig.noteOn(1, 60);
    const double stillQuiet = rms(rig.render(9600).left, 4800, 9600);
    EXPECT_NEAR(stillQuiet, quiet, quiet * 0.01) << "the Patch port does nothing while a script is selected";
    rig.pedal().setScript(0);
    rig.render(128);
    EXPECT_EQ(rig.pedal().getScriptStatus(), 1.0f) << "the factory patch is ok";
    rig.noteOn(1, 60);
    const double factory = rms(rig.render(9600).left, 4800, 9600);
    EXPECT_GT(std::abs(factory - quiet), quiet * 0.05) << "patch 0 plays now";
    EXPECT_EQ(rig.pedal().getPatchIndex(), 0u);
}

TEST(MorphexsynthScript, AReloadUnderTwelveHeldNotesReleasesThemAndStaysFinite)
{
    Rig rig("held");
    rig.writeUser(1, factoryText("airy-pad"));
    rig.pedal().setScript(1);
    rig.work();
    rig.render(128);
    for (int i = 0; i < 12; ++i)
    {
        rig.noteOn(1 + i % 3, 40 + 3 * i);
    }
    rig.render(9600);
    EXPECT_EQ(rig.pedal().activeVoices(), 12u);
    rig.writeUser(1, factoryText("kick-punch"));
    rig.render(24000);
    rig.work();
    const Stereo after = rig.render(5 * 48000);
    EXPECT_TRUE(std::all_of(after.left.begin(), after.left.end(),
                            [](const float v) { return std::isfinite(v) && std::abs(v) < 4.0f; }));
    EXPECT_EQ(rig.pedal().activeVoices(), 0u);
}

TEST(MorphexsynthScript, ASetupThatSetsLessThanEverythingStartsFromABlankPatch)
{
    Rig rig("blank");
    rig.writeUser(1, "function OnStart() SetVolume(0) end\n");
    rig.pedal().setScript(1);
    rig.work();
    rig.render(128);
    rig.noteOn(1, 60);
    EXPECT_LT(rms(rig.render(9600).left, 2400, 9600), 1e-6) << "no oscillator has a level: nothing sounds";
}

TEST(MorphexsynthScript, TheFactoryPatchesAreEvaluatedOnceAndAFailingOneKeepsTheSound)
{
    Rig rig("badfactory", false);
    rig.pedal().setScriptDirectories("/nonexistent/folder", "/nonexistent/user");
    rig.pedal().setSampleRate(c_rate);
    rig.pedal().reset();
    EXPECT_EQ(rig.pedal().getScriptStatus(), 2.0f) << "the file of the selected patch is missing";
    rig.noteOn(1, 60);
    EXPECT_LT(rms(rig.render(9600).left, 2400, 9600), 1e-6) << "and nothing has loaded: silence";
}

TEST(MorphexsynthScript, EveryExampleLoadsSoundsAndSurvivesItsHooks)
{
    const fs::path folder = std::string(JP_SOURCE_DIR) + "/plugins/morphexsynth/scripts/examples";
    std::vector<fs::path> files;
    for (const auto& file : fs::directory_iterator(folder))
    {
        if (file.path().extension() == ".lua")
        {
            files.push_back(file.path());
        }
    }
    std::sort(files.begin(), files.end());
    ASSERT_GE(files.size(), 7u);
    for (const fs::path& file : files)
    {
        std::ifstream in(file);
        ASSERT_TRUE(in.good()) << file;
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        Rig rig("example-" + file.stem().string(), false);
        rig.writeUser(1, text);
        rig.pedal().setScript(1);
        rig.pedal().reset();
        EXPECT_EQ(rig.pedal().getScriptStatus(), 1.0f) << file.filename();
        rig.noteOn(1, 60, 100);
        rig.render(4800);
        rig.midi(0xB0, 1, 100); // the mod wheel, pressure and a program change reach the hooks that want them
        rig.midi(0xD0, 90);
        rig.midi(0xC0, 1);
        rig.work();
        const Stereo played = rig.render(9600);
        rig.work();
        EXPECT_EQ(rig.pedal().getScriptStatus(), 1.0f) << file.filename();
        EXPECT_GT(rms(played.left, 2400, 9600), 0.001) << file.filename() << " is silent";
        for (const float sample : played.left)
        {
            ASSERT_TRUE(std::isfinite(sample)) << file.filename();
        }
    }
}

}
