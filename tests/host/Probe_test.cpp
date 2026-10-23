// Tests of the probe test plugin through its built LV2 binary (<build>/lv2/jaypacks-probe.lv2):
// audio passes unchanged, the control outputs report the echo and the audio time.

#include <gtest/gtest.h>
#include <memory>
#include <vector>

#include "Lv2Host.h"
#include "dsp/Numbers/Random.h"

namespace jp::host
{

class Probe : public ::testing::Test
{
  protected:
    static void SetUpTestSuite()
    {
        s_bundle = std::make_unique<BundleInfo>(readBundle(fs::path(JP_LV2_DIR) / "jaypacks-probe.lv2"));
        s_library = std::make_unique<Library>(*s_bundle);
    }

    static void TearDownTestSuite()
    {
        s_library.reset();
        s_bundle.reset();
    }

    static std::unique_ptr<Instance> instance(const double sampleRate = 48000.0)
    {
        return std::make_unique<Instance>(s_library->descriptor(), *s_bundle, sampleRate);
    }

    static std::unique_ptr<BundleInfo> s_bundle;
    static std::unique_ptr<Library> s_library;
};

std::unique_ptr<BundleInfo> Probe::s_bundle;
std::unique_ptr<Library> Probe::s_library;

TEST_F(Probe, AudioPassesUnchanged)
{
    Random random(5u);
    std::vector<float> input(10000);
    for (auto& x : input)
    {
        x = 2.0f * random.uniform() - 1.0f;
    }
    EXPECT_EQ(instance()->process(input, 128), input);
    EXPECT_EQ(instance()->process(input, 128, true), input);
}

TEST_F(Probe, EchoFollowsTheInputAfterOneBlock)
{
    auto probe = instance();
    EXPECT_EQ(probe->output("echoOut"), 0.0f) << "written by activate()";
    probe->setControl("echoIn", 123.456f);
    EXPECT_EQ(probe->output("echoOut"), 0.0f);
    std::ignore = probe->process(std::vector<float>(64, 0.0f), 64);
    EXPECT_EQ(probe->output("echoOut"), 123.456f);
    probe->setControl("echoIn", 5000.0f);
    std::ignore = probe->process(std::vector<float>(64, 0.0f), 64);
    EXPECT_EQ(probe->output("echoOut"), 1000.0f) << "clamped to the port range by the adapter";
}

TEST_F(Probe, SecondsCountTheAudioTime)
{
    for (const double rate : {44100.0, 48000.0, 96000.0})
    {
        auto probe = instance(rate);
        EXPECT_EQ(probe->output("seconds"), 0.0f);
        std::ignore = probe->process(std::vector<float>(static_cast<size_t>(rate * 2.5), 0.0f), 128);
        EXPECT_FLOAT_EQ(probe->output("seconds"), 2.5f) << rate;
        probe->reactivate();
        std::ignore = probe->process(std::vector<float>(static_cast<size_t>(rate / 2), 0.0f), 128);
        EXPECT_FLOAT_EQ(probe->output("seconds"), 0.5f) << "activate() restarts the count";
    }
}

TEST_F(Probe, MidiEventsAreCountedWithChannelsAndTheLastMessage)
{
    auto probe = instance();
    ASSERT_TRUE(probe->hasMidiInput());
    EXPECT_EQ(probe->output("midiEvents"), 0.0f);
    EXPECT_EQ(probe->output("midiChannels"), 0.0f);
    probe->sendMidi(0, 0x91, 60, 100);    // channel 2
    probe->sendMidi(300, 0xB5, 74, 90);   // channel 6
    probe->sendMidi(300, 0xE0, 0, 64);    // channel 1
    probe->sendMidi(900, 0xD3, 55, 0, 2); // channel 4, two bytes
    std::ignore = probe->process(std::vector<float>(1024, 0.0f), 128);
    EXPECT_EQ(probe->output("midiEvents"), 4.0f);
    EXPECT_EQ(probe->output("midiChannels"), static_cast<float>((1u << 1) | (1u << 5) | (1u << 0) | (1u << 3)));
    EXPECT_EQ(probe->output("midiStatus"), 0xD3);
    EXPECT_EQ(probe->output("midiData1"), 55.0f);
    EXPECT_EQ(probe->output("midiData2"), 0.0f);
}

TEST_F(Probe, AllSixteenChannelsShowUpAsOneMask)
{
    auto probe = instance();
    for (uint8_t channel = 0; channel < 16; ++channel)
    {
        probe->sendMidi(static_cast<uint64_t>(channel) * 10, static_cast<uint8_t>(0x90 | channel), 60, 100);
    }
    std::ignore = probe->process(std::vector<float>(512, 0.0f), 64);
    EXPECT_EQ(probe->output("midiEvents"), 16.0f);
    EXPECT_EQ(probe->output("midiChannels"), 65535.0f);
}

TEST_F(Probe, SystemMessagesCountButBelongToNoChannel)
{
    auto probe = instance();
    probe->sendMidi(5, 0xF8, 0, 0, 1); // timing clock
    std::ignore = probe->process(std::vector<float>(64, 0.0f), 64);
    EXPECT_EQ(probe->output("midiEvents"), 1.0f);
    EXPECT_EQ(probe->output("midiChannels"), 0.0f);
    EXPECT_EQ(probe->output("midiStatus"), 0xF8);
}

TEST_F(Probe, ActivateClearsTheMidiCounters)
{
    auto probe = instance();
    probe->sendMidi(0, 0x92, 60, 100);
    std::ignore = probe->process(std::vector<float>(64, 0.0f), 64);
    ASSERT_EQ(probe->output("midiEvents"), 1.0f);
    probe->reactivate();
    EXPECT_EQ(probe->output("midiEvents"), 0.0f);
    EXPECT_EQ(probe->output("midiChannels"), 0.0f);
    EXPECT_EQ(probe->output("midiStatus"), 0.0f);
}

TEST_F(Probe, MidiDoesNotChangeTheAudio)
{
    Random random(9u);
    std::vector<float> input(2000);
    for (auto& x : input)
    {
        x = 2.0f * random.uniform() - 1.0f;
    }
    auto probe = instance();
    probe->sendMidi(0, 0x90, 60, 100);
    probe->sendMidi(777, 0x80, 60, 0);
    EXPECT_EQ(probe->process(input, 128), input);
}

std::vector<MidiMessage> sendAll(Instance& probe, const std::vector<MidiMessage>& messages)
{
    for (const MidiMessage& message : messages)
    {
        probe.sendMidi(message.sample, message.status, message.data1, message.data2, message.size);
    }
    return messages;
}

bool sameMessages(const std::vector<MidiMessage>& a, const std::vector<MidiMessage>& b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].sample != b[i].sample || a[i].status != b[i].status || a[i].data1 != b[i].data1 ||
            a[i].data2 != b[i].data2 || a[i].size != b[i].size)
        {
            return false;
        }
    }
    return true;
}

TEST_F(Probe, EveryMidiMessageLeavesAtTheSampleItCameIn)
{
    const std::vector<MidiMessage> messages{{0, 0x90, 60, 100, 3},  {5, 0xB1, 74, 64, 3}, {5, 0xD2, 40, 0, 2},
                                            {127, 0x80, 60, 0, 3},  {128, 0xF8, 0, 0, 1}, {1000, 0xE3, 1, 2, 3},
                                            {1999, 0x93, 61, 99, 3}};
    for (const size_t block : {1u, 7u, 64u, 128u, 513u, 2000u})
    {
        auto probe = instance();
        ASSERT_TRUE(probe->hasMidiOutput());
        sendAll(*probe, messages);
        std::vector<MidiMessage> out;
        std::ignore = probe->process(std::vector<float>(2000, 0.0f), block);
        out = probe->takeMidiOut();
        EXPECT_TRUE(sameMessages(out, messages)) << "block " << block << ": " << out.size() << " messages";
        EXPECT_EQ(probe->output("midiSent"), static_cast<float>(messages.size()));
        EXPECT_EQ(probe->output("midiDropped"), 0.0f);
    }
}

TEST_F(Probe, ASilentBlockWritesAnEmptySequence)
{
    auto probe = instance();
    probe->sendMidi(10, 0x90, 60, 100);
    std::ignore = probe->process(std::vector<float>(64, 0.0f), 64);
    EXPECT_EQ(probe->takeMidiOut().size(), 1u);
    std::ignore = probe->process(std::vector<float>(64, 0.0f), 64);
    EXPECT_TRUE(probe->takeMidiOut().empty()) << "events of an earlier block do not stay in the sequence";
}

TEST_F(Probe, ANoteOnTravelsToTheWorkerAndTheAnswerComesBackAtTheNextBlock)
{
    auto probe = instance();
    ASSERT_TRUE(probe->hasWorker());
    probe->sendMidi(0, 0x90, 60, 100);
    probe->sendMidi(10, 0x80, 60, 0); // a note off is no job
    probe->sendMidi(20, 0x91, 64, 0); // a note on with velocity 0 is a note off
    probe->sendMidi(30, 0x92, 67, 90);
    std::ignore = probe->process(std::vector<float>(64, 0.0f), 64);
    EXPECT_EQ(probe->jobsScheduled(), 2u);
    EXPECT_EQ(probe->output("workerJobs"), 0.0f) << "the jobs are worked after the run() that scheduled them";
    EXPECT_EQ(probe->output("workerAnswers"), 0.0f);
    std::ignore = probe->process(std::vector<float>(64, 0.0f), 64);
    EXPECT_EQ(probe->output("workerJobs"), 2.0f);
    EXPECT_EQ(probe->output("workerAnswers"), 2.0f) << "the answers arrive at the start of the next run()";
    EXPECT_EQ(probe->output("workerNote"), 67.0f);
}

TEST_F(Probe, TheWorkerRunsOnAThreadOfItsOwnWithTheSameResult)
{
    auto probe = instance();
    probe->startWorkerThread();
    for (uint64_t i = 0; i < 20; ++i)
    {
        probe->sendMidi(i * 10, 0x90, static_cast<uint8_t>(40 + i), 100);
    }
    std::ignore = probe->process(std::vector<float>(256, 0.0f), 128);
    probe->waitForWorker();
    std::ignore = probe->process(std::vector<float>(128, 0.0f), 128);
    EXPECT_EQ(probe->output("workerJobs"), 20.0f);
    EXPECT_EQ(probe->output("workerAnswers"), 20.0f);
    EXPECT_EQ(probe->output("workerNote"), 59.0f);
}

TEST_F(Probe, ARefusedJobDoesNotStopTheMidi)
{
    auto probe = instance();
    probe->refuseJobs(true);
    probe->sendMidi(3, 0x90, 60, 100);
    std::ignore = probe->process(std::vector<float>(64, 0.0f), 64);
    std::ignore = probe->process(std::vector<float>(64, 0.0f), 64);
    EXPECT_EQ(probe->output("workerJobs"), 0.0f);
    EXPECT_EQ(probe->output("workerAnswers"), 0.0f);
    EXPECT_EQ(probe->output("midiSent"), 1.0f);
}

TEST_F(Probe, ActivateClearsTheWorkerAndMidiOutputCounters)
{
    auto probe = instance();
    probe->sendMidi(0, 0x90, 60, 100);
    std::ignore = probe->process(std::vector<float>(128, 0.0f), 64);
    ASSERT_EQ(probe->output("workerAnswers"), 1.0f);
    probe->reactivate();
    EXPECT_EQ(probe->output("workerJobs"), 0.0f);
    EXPECT_EQ(probe->output("workerAnswers"), 0.0f);
    EXPECT_EQ(probe->output("workerNote"), 0.0f);
    EXPECT_EQ(probe->output("midiSent"), 0.0f);
}

}
