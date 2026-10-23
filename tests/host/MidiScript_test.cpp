// Tests of the midiscript plugin through its built LV2 binary (<build>/lv2/jaypacks-midiscript.lv2) and a host whose
// worker runs on a thread of its own: the bundle ships its scripts and documents, and a script's notes leave at the
// exact samples.

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

#include "Lv2Host.h"

namespace jp::host
{

class MidiScript : public ::testing::Test
{
  protected:
    static void SetUpTestSuite()
    {
        s_bundle = std::make_unique<BundleInfo>(readBundle(fs::path(JP_LV2_DIR) / "jaypacks-midiscript.lv2"));
        s_library = std::make_unique<Library>(*s_bundle);
    }

    static void TearDownTestSuite()
    {
        s_library.reset();
        s_bundle.reset();
    }

    static std::unique_ptr<Instance> instance()
    {
        return std::make_unique<Instance>(s_library->descriptor(), *s_bundle, 48000.0);
    }

    static std::unique_ptr<BundleInfo> s_bundle;
    static std::unique_ptr<Library> s_library;
};

std::unique_ptr<BundleInfo> MidiScript::s_bundle;
std::unique_ptr<Library> MidiScript::s_library;

TEST_F(MidiScript, TheBundleShipsTheScriptsTheLibrariesAndTheDocumentsAboutTheScriptApi)
{
    const fs::path scripts = s_bundle->dir / "scripts";
    for (const char* name : {"passthrough", "arpeggiator", "chord-player", "scale-quantiser", "octave-doubler",
                             "cc-remap", "scales", "chords", "rhythm"})
    {
        EXPECT_TRUE(fs::exists(scripts / (std::string(name) + ".lua"))) << name;
    }
    const std::string api = readFile(scripts / "API.md");
    for (const char* fragment :
         {"# MIDI Script scripting API", "### Note(channel, note, velocity, { fields })", "### Pass({ fields })",
          "### OnNoteOn(channel, note, velocity)", "### OnTick(tick)", "### SetTimer(id, { fields })"})
    {
        EXPECT_NE(api.find(fragment), std::string::npos) << fragment;
    }
    EXPECT_NE(readFile(scripts / "constants.lua").find("Ticks = {"), std::string::npos);
}

TEST_F(MidiScript, ThePortsAreAMidiProcessorWithATempoAndATransportPort)
{
    auto plugin = instance();
    EXPECT_TRUE(plugin->hasMidiInput());
    EXPECT_TRUE(plugin->hasMidiOutput());
    EXPECT_TRUE(plugin->hasWorker());
    EXPECT_EQ(plugin->audioInputs(), 0u);
    const std::string ttl = readFile(s_bundle->dir / "midiscript.ttl");
    EXPECT_NE(ttl.find("lv2:designation time:beatsPerMinute"), std::string::npos);
    EXPECT_NE(ttl.find("lv2:designation time:speed"), std::string::npos);
}

TEST_F(MidiScript, TheArpeggiatorPlaysOnTheExactSamplesWithTheWorkerOnItsOwnThread)
{
    auto plugin = instance();
    plugin->startWorkerThread();
    plugin->setControl("script", 1.0f);
    plugin->setControl("latency", 50.0f);
    plugin->reactivate();
    plugin->sendMidi(0, 0x90, 60, 100);
    plugin->sendMidi(0, 0x90, 64, 100);
    plugin->sendMidi(0, 0x90, 67, 100);
    constexpr uint64_t c_latency{2400};
    std::ignore = plugin->process(std::vector<float>(2 * c_latency + 6000 * 8 - 1000, 0.0f), 128, false,
                                  [&](size_t) { plugin->waitForWorker(); });
    plugin->waitForWorker();
    EXPECT_EQ(plugin->output("scriptStatus"), 1.0f);
    EXPECT_EQ(plugin->output("late"), 0.0f);
    const auto out = plugin->takeMidiOut();
    const std::vector<uint8_t> notes{60, 64, 67};
    size_t ons = 0;
    for (const MidiMessage& message : out)
    {
        if ((message.status & 0xF0) == 0x90)
        {
            EXPECT_EQ(message.sample, 2 * c_latency + 6000 * ons) << ons;
            EXPECT_EQ(message.data1, notes[ons % 3]) << ons;
            ++ons;
        }
    }
    EXPECT_EQ(ons, 8u);
}

}
