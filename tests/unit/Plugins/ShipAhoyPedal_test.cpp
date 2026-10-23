// Ship Ahoy as a plugin: the bindings, and the pedal driven the way the LV2 adapter and the host drive it (blocks, the
// host's worker run between them), with the scripts of the plugin and scripts written for the test. The engine
// (glide, filter, drift) has its own tests in ShipAhoyEngine_test.cpp.
//
// Times: the pedal counts samples from 1; the clock starts one lead L after the first block (tick 0 at 1 + L), a
// step is `stepTicks` ticks apart and everything a step asks for happens a lead later (at 1 + 2 L for step 0). The
// lead is two blocks of the largest block the pedal has seen; the rig fixes it with setExpectedBlock (L = 480 by
// default) so that a test can run in blocks of any size.
// A rig sample s is the state after s samples were processed, so a command due at pedal time t has been applied once
// s >= t (it acts on the sample with the index t - 1, which is processed when s goes from t - 1 to t).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

#include "lv2/PedalServices.h"
#include "plugins/shipahoy/ShipAhoyPedal.h"
#include "scripting/BindingCheck.h"

namespace jp::test
{

namespace fs = std::filesystem;

namespace
{

constexpr float c_rate{48000.0f};

/// 240 BPM at 1/16: 125 samples a tick, 24 ticks (3000 samples) a step.
constexpr double c_tempo{240.0};
constexpr uint64_t c_step{3000};

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

/// A pedal with the plugin's scripts folder and a user folder of its own, and the time: run() does what the host does,
/// the pedal's work() (the worker thread) after every block. The input is silence; what is observed is the engine's
/// state at the end of a block.
class Rig
{
  public:
    explicit Rig(const std::string& name, const uint32_t leadBlock = 240)
        : m_folder(fs::current_path() / "shipahoy-test" / (name + "-" + std::to_string(::getpid())))
        , m_pedal(std::make_unique<ShipAhoyPedal>())
        , m_input(1 << 16, 0.0f)
        , m_output(1 << 16, 0.0f)
    {
        fs::remove_all(m_folder);
        fs::create_directories(m_folder / "user");
        m_pedal->setScriptDirectories(std::string(JP_SOURCE_DIR) + "/plugins/shipahoy/scripts",
                                      (m_folder / "user").string());
        m_scheduler.setSchedule(&m_worker.schedule);
        m_pedal->setWorkScheduler(m_scheduler);
        m_pedal->setSampleRate(c_rate);
        m_pedal->setExpectedBlock(leadBlock);
        m_pedal->setTempo(static_cast<float>(c_tempo));
        m_pedal->setX(5.0f); // the defaults of the ports X and Y
        m_pedal->setY(85.0f);
        m_lead = m_pedal->leadFrames();
    }

    ~Rig()
    {
        m_pedal.reset();
        fs::remove_all(m_folder);
    }

    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    void activate()
    {
        m_pedal->reset();
    }

    ShipAhoyPedal& pedal()
    {
        return *m_pedal;
    }

    [[nodiscard]] const ShipAhoyEngine& engine() const
    {
        return m_pedal->engine();
    }

    [[nodiscard]] uint64_t lead() const
    {
        return m_lead;
    }

    /// The pedal time at which the command of the step at tick `tick` takes effect (see the top of the file).
    [[nodiscard]] uint64_t dueOfTick(const uint64_t tick, const double samplesPerTick = 125.0) const
    {
        return 1 + 2 * m_lead + static_cast<uint64_t>(static_cast<double>(tick) * samplesPerTick);
    }

    void writeUser(const int slot, const std::string& text)
    {
        const fs::path path = m_folder / "user" / ("user-" + std::to_string(slot) + ".lua");
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << text;
        }
        fs::last_write_time(path, fs::file_time_type::clock::now() + std::chrono::seconds(++m_edits));
    }

    /// The script of a user slot, selected and loaded (activation).
    void useUser(const int slot, const std::string& text)
    {
        writeUser(slot, text);
        m_pedal->setScript(static_cast<float>(8 + slot));
    }

    /// What the plugin hears: the input sample with the given index (from 0), silence if none is set.
    void setSignal(std::function<float(uint64_t)> signal)
    {
        m_signal = std::move(signal);
    }

    /// The worker thread comes by after every `blocks`-th block only (a worker that is not scheduled in time).
    void setWorkerEvery(const uint32_t blocks)
    {
        m_workerEvery = blocks;
    }

    /// Keeps the output of run() (from now on) for output().
    void record()
    {
        m_recording = true;
    }

    [[nodiscard]] const std::vector<float>& output() const
    {
        return m_recorded;
    }

    /// Runs up to the sample `until` in blocks of `block`.
    void run(const uint64_t until, const uint32_t block = 128)
    {
        while (m_sample < until)
        {
            const auto n = static_cast<uint32_t>(std::min<uint64_t>(block, until - m_sample));
            if (m_signal)
            {
                for (uint32_t k = 0; k < n; ++k)
                {
                    m_input[k] = m_signal(m_sample + k);
                }
            }
            for (uint32_t done = 0; done < n;)
            {
                const uint32_t part = std::min<uint32_t>(n - done, static_cast<uint32_t>(m_input.size()));
                m_pedal->process(m_input.data(), m_output.data(), part);
                done += part;
            }
            if (m_recording)
            {
                m_recorded.insert(m_recorded.end(), m_output.begin(), m_output.begin() + n);
            }
            if (++m_blocks % m_workerEvery == 0)
            {
                WorkResponder responder(nullptr, nullptr);
                m_pedal->work(nullptr, 0, responder);
            }
            m_sample += n;
        }
    }

    /// Runs one sample at a time to `until` and returns the base note after each of them (index s - 1 is after s
    /// samples).
    std::vector<float> trace(const uint64_t until)
    {
        std::vector<float> notes;
        while (m_sample < until)
        {
            run(m_sample + 1, 1);
            notes.push_back(engine().getNote());
        }
        return notes;
    }

    [[nodiscard]] uint64_t sample() const
    {
        return m_sample;
    }

  private:
    fs::path m_folder;
    std::unique_ptr<ShipAhoyPedal> m_pedal;
    WorkerStub m_worker;
    WorkScheduler m_scheduler;
    std::vector<float> m_input;
    std::vector<float> m_output;
    std::vector<float> m_recorded;
    std::function<float(uint64_t)> m_signal;
    bool m_recording{false};
    uint64_t m_sample{0};
    uint64_t m_lead{480};
    uint32_t m_workerEvery{1};
    uint32_t m_blocks{0};
    int m_edits{0};
};

/// The samples (counted as in Rig::trace: s after s samples) at which the note changes, from a trace.
std::vector<size_t> changes(const std::vector<float>& trace)
{
    std::vector<size_t> at;
    for (size_t i = 1; i < trace.size(); ++i)
    {
        if (trace[i] != trace[i - 1])
        {
            at.push_back(i + 1);
        }
    }
    return at;
}

/// The steps that have taken effect, for a script like c_stepNotes: the note is 60 + step after step `step`.
uint64_t appliedSteps(const Rig& rig)
{
    const float note = rig.engine().getNote();
    return note < 82.0f ? static_cast<uint64_t>(std::llround(note)) - 59 : 0;
}

/// A script that sets note 60 + step at every step, at once.
const char* const c_stepNotes = "function OnStep(step)\n    SetCutoff(60 + step, { glide = 0 })\nend\n";

}

TEST(ShipAhoyScript, TheBindingTableIsValid)
{
    EXPECT_EQ(script::checkBindings(ship_ahoy::bindings()), "");
    EXPECT_STREQ(ship_ahoy::c_commands[ship_ahoy::c_timerCommandBase].name, "SetTimer");
    EXPECT_STREQ(ship_ahoy::c_commands[ship_ahoy::c_timerCommandBase + 1].name, "StopTimer");
    EXPECT_STREQ(ship_ahoy::c_hooks[ship_ahoy::c_stepHook].name, "OnStep");
}

TEST(ShipAhoyScript, EveryNoteValueIsAWholeNumberOfTicksAndTheLabelsMatch)
{
    // a quarter is 96 ticks; 1/64 is a sixteenth of that
    const std::array<double, ship_ahoy::c_stepCount> noteValue{8,    4,     2,        1,        0.5,
                                                               0.25, 0.125, 1.0 / 16, 1.0 / 32, 1.0 / 64};
    for (size_t i = 0; i < ship_ahoy::c_stepCount; ++i)
    {
        EXPECT_DOUBLE_EQ(static_cast<double>(ship_ahoy::c_stepTicks[i]),
                         noteValue[i] * 4.0 * ship_ahoy::c_ticksPerQuarter * 1.0)
            << ship_ahoy::c_stepLabels[i];
    }
    for (const auto& entry : ship_ahoy::c_tickEntries)
    {
        EXPECT_EQ(entry.value % 2, 0) << entry.name;
    }
}

TEST(ShipAhoyScript, SixtyBpmAtAnEighthIsTwoStepsPerSecond)
{
    // the author's example: 60 BPM at 1/8 is 2 Hz; the ratio of a script multiplies it
    ShipAhoyPedal pedal;
    pedal.setSampleRate(c_rate);
    pedal.setTempo(60.0f);
    pedal.setStep(6.0f); // 1/8
    EXPECT_NEAR(pedal.stepRate(), 2.0, 1e-12);
    pedal.setStep(7.0f); // 1/16
    EXPECT_NEAR(pedal.stepRate(), 4.0, 1e-12);
    pedal.setStep(9.0f); // 1/64
    EXPECT_NEAR(pedal.stepRate(), 16.0, 1e-12);
    pedal.setStep(0.0f); // 8/1
    EXPECT_NEAR(pedal.stepRate(), 1.0 / 32.0, 1e-12);
    pedal.setTempo(105.0f);
    pedal.setStep(7.0f);
    EXPECT_NEAR(pedal.stepRate(), 7.0, 1e-12) << "the default: the old 7 Hz";
}

TEST(ShipAhoyPedalClock, TheScriptStepsOnTheStepsOfTheClock)
{
    Rig rig("clock-steps");
    rig.useUser(1, c_stepNotes);
    rig.activate();
    const uint64_t first = 2 * rig.lead(); // sample index of the first step
    rig.run(first, 1);
    EXPECT_EQ(appliedSteps(rig), 0u);
    rig.run(first + 1, 1);
    EXPECT_EQ(appliedSteps(rig), 1u);
    rig.run(first + c_step, 1);
    EXPECT_EQ(appliedSteps(rig), 1u);
    rig.run(first + c_step + 1, 1);
    EXPECT_EQ(appliedSteps(rig), 2u);
    rig.run(first + 10 * c_step + 1, 128);
    EXPECT_EQ(appliedSteps(rig), 11u);
}

TEST(ShipAhoyPedalClock, TempoAndStepSetThePeriodOfTheSteps)
{
    struct Case
    {
        float tempo;
        float step;
        uint64_t period;
    };
    for (const Case& c : {Case{60.0f, 6.0f, 24000}, Case{60.0f, 7.0f, 12000}, Case{120.0f, 5.0f, 24000},
                          Case{240.0f, 9.0f, 750}, Case{20.0f, 3.0f, 576000}})
    {
        Rig rig("clock-period");
        rig.useUser(1, c_stepNotes);
        rig.pedal().setTempo(c.tempo);
        rig.pedal().setStep(c.step);
        rig.activate();
        const uint64_t first = 2 * rig.lead();
        rig.run(first + 1, 1);
        ASSERT_EQ(appliedSteps(rig), 1u);
        rig.run(first + c.period, 128);
        EXPECT_EQ(appliedSteps(rig), 1u) << c.tempo << " BPM, step " << c.step;
        rig.run(first + c.period + 1, 1);
        EXPECT_EQ(appliedSteps(rig), 2u) << c.tempo << " BPM, step " << c.step;
    }
}

TEST(ShipAhoyPedalClock, ATempoChangeActsFromTheNextTickWithoutAJumpOfTheClock)
{
    Rig rig("clock-tempo");
    rig.useUser(1, c_stepNotes);
    rig.activate();
    const uint64_t first = 2 * rig.lead();
    rig.run(first + c_step + 1, 128);
    ASSERT_EQ(appliedSteps(rig), 2u);
    rig.pedal().setTempo(120.0f); // half the speed: a step is 6000 samples
    rig.run(first + 6 * c_step, 128);
    // the next step (tick 48) comes after the 24 ticks that were already due plus the slower ones: between 3000 and
    // 6000 samples after the second step, never before the old period
    const uint64_t steps = appliedSteps(rig);
    EXPECT_LE(steps, 4u);
    EXPECT_GE(steps, 3u);
    rig.run(first + 40 * c_step, 128);
    EXPECT_NEAR(static_cast<double>(appliedSteps(rig)), 2.0 + (40.0 * 3000.0 - 3000.0) / 6000.0, 2.0);
}

TEST(ShipAhoyPedalClock, TheKnobIsTheTempoInFreeRunAndTheBoardTempoWithTheTransport)
{
    Rig rig("knobs-sources");
    rig.activate();
    // free run: the board's tempo (a port the host drives) does not matter, the knob does
    rig.pedal().setHostTempo(60.0f);
    EXPECT_NEAR(rig.pedal().clock().tempo(), c_tempo, 1e-9);
    rig.pedal().setTempo(100.0f);
    EXPECT_NEAR(rig.pedal().clock().tempo(), 100.0, 1e-9);
    // synced to the transport: the board's tempo counts, the knob does not
    rig.pedal().setSync(1.0f);
    EXPECT_NEAR(rig.pedal().clock().tempo(), 60.0, 1e-9);
    rig.pedal().setTempo(200.0f);
    EXPECT_NEAR(rig.pedal().clock().tempo(), 60.0, 1e-9);
    rig.pedal().setHostTempo(90.0f);
    EXPECT_NEAR(rig.pedal().clock().tempo(), 90.0, 1e-9);
    rig.pedal().setSync(0.0f);
    EXPECT_NEAR(rig.pedal().clock().tempo(), 200.0, 1e-9);
}

TEST(ShipAhoyPedalClock, WithTheTransportTheFilterMovesOnlyWhileItRolls)
{
    Rig rig("clock-transport");
    rig.useUser(1, c_stepNotes);
    rig.pedal().setSync(1.0f);
    rig.pedal().setHostTempo(static_cast<float>(c_tempo)); // the board's tempo is what counts now
    rig.activate();
    rig.run(20 * c_step, 128);
    EXPECT_EQ(appliedSteps(rig), 0u) << "stopped";
    rig.pedal().setTransport(1.0f);
    rig.run(rig.sample() + 2 * rig.lead() + 1 + 3 * c_step, 128);
    EXPECT_GE(appliedSteps(rig), 3u);
    EXPECT_LE(appliedSteps(rig), 5u);
    rig.pedal().setTransport(0.0f);
    rig.run(rig.sample() + 2 * rig.lead() + 1, 128); // what was announced before the stop still arrives
    const uint64_t steps = appliedSteps(rig);
    rig.run(rig.sample() + 20 * c_step, 128);
    EXPECT_EQ(appliedSteps(rig), steps) << "stopped again";
}

TEST(ShipAhoyPedalScript, StepsArriveAtTheirSampleWhateverTheBlockSize)
{
    // a lead of two blocks of 4096 covers the worker for the longest block; with the lead fixed, the block size does
    // not change where anything happens
    constexpr uint32_t c_leadBlock{4096};
    constexpr uint64_t d0 = 1 + 2 * 2 * c_leadBlock; // step 0 acts on the sample with the index d0 - 1
    std::vector<std::vector<float>> reference;
    const std::vector<uint64_t> checkpoints{d0 - 1,          d0,         d0 + c_step - 1, d0 + c_step,
                                            d0 + 2 * c_step, d0 + 10000, d0 + 20000,      d0 + 30000};
    for (const uint32_t block : {1u, 7u, 16u, 128u, 256u, 4096u})
    {
        Rig rig("blocks-" + std::to_string(block), c_leadBlock);
        rig.useUser(1, c_stepNotes);
        rig.activate();
        std::vector<float> notes;
        for (const uint64_t at : checkpoints)
        {
            rig.run(at, block);
            notes.push_back(rig.engine().getNote());
        }
        if (reference.empty())
        {
            reference.push_back(notes);
            EXPECT_NE(notes[0], 60.0f) << "before";
            EXPECT_EQ(notes[1], 60.0f);
            EXPECT_EQ(notes[2], 60.0f) << "step 1 is a step later";
            EXPECT_EQ(notes[3], 60.0f + 1.0f);
            EXPECT_EQ(notes[5], 60.0f + 3.0f);
        }
        else
        {
            EXPECT_EQ(notes, reference[0]) << "block " << block;
        }
    }
}

TEST(ShipAhoyPedalScript, TheGlideIsAFractionOfTheStepAndLandsOnTheTarget)
{
    struct Case
    {
        const char* glide;
        double fraction;
    };
    for (const Case& c : {Case{"0.5", 0.5}, Case{"1", 1.0}, Case{"0.25", 0.25}})
    {
        Rig rig(std::string("glide-") + c.glide);
        rig.useUser(1, std::string("function OnStep(step)\n    if step == 0 then SetCutoff(60, { glide = 0 })\n"
                                   "    elseif step == 1 then SetCutoff(72, { glide = ") +
                           c.glide + " }) end\nend\n");
        rig.activate();
        const uint64_t due = rig.dueOfTick(24); // step 1
        const auto length = static_cast<uint64_t>(std::llround(c.fraction * static_cast<double>(c_step)));
        rig.run(due - 1, 128);
        EXPECT_EQ(rig.engine().getNote(), 60.0f);
        rig.run(due + length / 2 - 1, 1);
        EXPECT_NEAR(rig.engine().getNote(), 66.0f, 0.03f) << "halfway through the glide of " << c.glide;
        rig.run(due - 1 + length - 1, 1);
        EXPECT_LT(rig.engine().getNote(), 72.0f);
        rig.run(due - 1 + length, 1);
        EXPECT_EQ(rig.engine().getNote(), 72.0f) << "arrives after the glide of " << c.glide;
        rig.run(due + 2 * c_step, 128);
        EXPECT_EQ(rig.engine().getNote(), 72.0f) << "then holds";
    }
}

TEST(ShipAhoyPedalScript, ADefaultGlideIsFivePercentOfTheStepAndAStepOfItsOwnOverridesTheClock)
{
    Rig rig("glide-default");
    rig.useUser(1, "function OnStep(step)\n    if step == 0 then SetCutoff(60, { glide = 0 })\n"
                   "    elseif step == 1 then SetCutoff(72)\n    elseif step == 2 then SetCutoff(48, { ms = 100, glide "
                   "= 0.5 }) end\n"
                   "end\n");
    rig.activate();
    const uint64_t due = rig.dueOfTick(24);
    rig.run(due - 1, 128);
    rig.run(due - 1 + 150 - 1, 1);
    EXPECT_LT(rig.engine().getNote(), 72.0f);
    rig.run(due - 1 + 150, 1);
    EXPECT_EQ(rig.engine().getNote(), 72.0f) << "5 % of 3000 samples";
    const uint64_t second = rig.dueOfTick(48);
    rig.run(second - 1, 128);
    rig.run(second - 1 + 2400, 1); // ms = 100 -> 4800 samples, glide 0.5 -> 2400
    EXPECT_EQ(rig.engine().getNote(), 48.0f);
}

TEST(ShipAhoyPedalScript, ACommandDuringAGlideContinuesFromWhereTheFrequencyIs)
{
    Rig rig("glide-retarget");
    rig.useUser(1, "function OnStep(step)\n    if step == 0 then SetCutoff(60, { glide = 0 })\n"
                   "    elseif step == 1 then SetCutoff(84, { glide = 1 })\n"
                   "    elseif step == 2 then SetCutoff(48, { glide = 0.5 }) end\nend\n");
    rig.activate();
    const uint64_t second = rig.dueOfTick(48);
    rig.run(second - 2, 128);
    const float before = rig.engine().getNote();
    EXPECT_GT(before, 60.0f);
    EXPECT_LT(before, 84.0f);
    rig.run(second - 1, 1);
    const float at = rig.engine().getNote();
    EXPECT_NEAR(at, before, 24.0f / 3000.0f * 1.5f) << "no jump when the next command comes";
    rig.run(second - 1 + 1500, 1);
    EXPECT_EQ(rig.engine().getNote(), 48.0f);
}

TEST(ShipAhoyPedalScript, SpeedRatioScalesTheClockFromItsSampleOnAndIsLimited)
{
    struct Case
    {
        const char* ratio;
        double interval;
    };
    for (const Case& c : {Case{"2", 1500.0}, Case{"0.5", 6000.0}, Case{"10", 300.0}, Case{"100", 300.0}})
    {
        Rig rig(std::string("ratio-") + c.ratio);
        rig.useUser(1, std::string("function OnStep(step)\n    if step == 0 then SpeedRatio(") + c.ratio +
                           ") end\n    SetCutoff(60 + step, { glide = 0 })\nend\n");
        rig.activate();
        const auto steps = changes(rig.trace(static_cast<uint64_t>(rig.lead() * 2 + c_step * 4 + 40 * c.interval)));
        ASSERT_GE(steps.size(), 8u) << c.ratio;
        // the interval between two steps that are well after the ratio took effect
        EXPECT_NEAR(static_cast<double>(steps[5] - steps[4]), c.interval, 1.0) << "ratio " << c.ratio;
        EXPECT_NEAR(static_cast<double>(steps[6] - steps[5]), c.interval, 1.0) << "ratio " << c.ratio;
    }
}

TEST(ShipAhoyPedalScript, ANewScriptStartsAtSpeedRatioOne)
{
    Rig rig("ratio-reset");
    rig.useUser(1, "function OnStep(step)\n    if step == 0 then SpeedRatio(4) end\n    SetCutoff(60 + step, { glide = "
                   "0 })\nend\n");
    rig.activate();
    rig.run(20000, 128);
    EXPECT_NEAR(rig.pedal().clock().tempo(), c_tempo * 4.0, 1e-9);
    rig.writeUser(1, c_stepNotes);
    rig.run(20000 + 3 * 24000, 128); // the file is looked at every half second
    EXPECT_NEAR(rig.pedal().clock().tempo(), c_tempo, 1e-9);
}

TEST(ShipAhoyPedalScript, ATempoKnobAndTheRatioMultiply)
{
    Rig rig("ratio-knob");
    rig.useUser(1, "function OnStart() SpeedRatio(0.5) end\n");
    rig.activate();
    rig.run(500, 128);
    EXPECT_NEAR(rig.pedal().clock().tempo(), c_tempo * 0.5, 1e-9);
    rig.pedal().setTempo(100.0f);
    EXPECT_NEAR(rig.pedal().clock().tempo(), 50.0, 1e-9);
}

TEST(ShipAhoyPedalScript, AScriptWithoutCommandsHoldsTheStartingFrequency)
{
    Rig rig("takeover");
    rig.useUser(1, "-- no commands at all\nfunction OnStart() end\n");
    rig.activate();
    rig.run(2000, 128);
    EXPECT_NEAR(rig.engine().getPeakFrequency(), ShipAhoyEngine::c_defaultFrequency, 0.5f) << "where it starts";
}

TEST(ShipAhoyPedalScript, AFileThatDoesNotLoadMovesNothingAndTheStatusSaysWhy)
{
    Rig rig("missing");
    rig.pedal().setScript(13.0f); // user-5: no such file
    rig.activate();
    rig.run(3 * c_step, 128);
    EXPECT_NEAR(rig.engine().getPeakFrequency(), ShipAhoyEngine::c_defaultFrequency, 0.5f) << "nothing moves it";
    EXPECT_EQ(rig.pedal().getScriptStatus(), static_cast<float>(script::ScriptState::FileMissing));
}

TEST(ShipAhoyPedalScript, ABrokenReloadKeepsTheRunningScript)
{
    Rig rig("broken");
    rig.useUser(1, c_stepNotes);
    rig.activate();
    rig.run(2 * c_step, 128);
    rig.writeUser(1, "function OnStep(step  -- unfinished");
    rig.run(2 * c_step + 40000, 128);
    EXPECT_EQ(rig.pedal().getScriptStatus(), static_cast<float>(script::ScriptState::CompileError));
    const float note = rig.engine().getNote();
    rig.run(2 * c_step + 40000 + 3 * c_step, 128);
    EXPECT_GT(rig.engine().getNote(), note) << "the old script goes on";
}

TEST(ShipAhoyPedalScript, AFailingHookHoldsTheFrequencyAndTheScriptGoesOn)
{
    Rig rig("failing");
    rig.useUser(1, "function OnStep(step)\n    if step == 2 then error('boom') end\n"
                   "    SetCutoff(60 + step, { glide = 0 })\nend\n");
    rig.activate();
    rig.run(rig.dueOfTick(24) + 10, 128);
    EXPECT_EQ(rig.engine().getNote(), 61.0f);
    rig.run(rig.dueOfTick(48) + 10, 128); // step 2: the error
    EXPECT_EQ(rig.engine().getNote(), 61.0f) << "holds";
    EXPECT_EQ(rig.pedal().host().hookFailures(), 1u);
    EXPECT_EQ(rig.pedal().getScriptStatus(), static_cast<float>(script::ScriptState::RuntimeError));
    rig.run(rig.dueOfTick(72) + 10, 128);
    EXPECT_EQ(rig.engine().getNote(), 63.0f) << "the next step works again";
}

TEST(ShipAhoyPedalScript, ANewScriptTakesOverFromWhereTheFrequencyIsWithTheDefaultSettings)
{
    Rig rig("takeover-settings");
    rig.useUser(1, "function OnStart()\n    SetResonance(10)\n    SetFilterType(FilterType.Hp24)\n    SetCutoff(76, { "
                   "glide = 0 })\nend\n");
    rig.activate();
    rig.run(3 * c_step, 128);
    EXPECT_NEAR(rig.engine().getPeakFrequency(), noteToFrequency(76.0f), 0.5f);
    EXPECT_EQ(rig.engine().getFilterPresetIndex(), ShipAhoyEngine::c_filterTypePresets[9]);
    const float resonanceBefore = rig.engine().getFilterResonance();
    rig.writeUser(1, c_stepNotes);
    rig.run(rig.sample() + 3 * 24000, 128); // the file is looked at every half second
    EXPECT_EQ(rig.engine().getFilterPresetIndex(), ShipAhoyEngine::c_filterTypePresets[1]) << "Lp12 again";
    EXPECT_GT(rig.engine().getFilterResonance(), 5.0f * resonanceBefore) << "85 % again";
    EXPECT_GT(rig.engine().getNote(), 59.0f);
}

TEST(ShipAhoyPedalScript, ACommandThatComesAfterItsSampleIsCountedLate)
{
    Rig rig("late", 0);    // the lead of the first block: 256 frames
    rig.setWorkerEvery(8); // the worker answers up to eight blocks after the event, four times the lead
    rig.useUser(1, c_stepNotes);
    rig.activate();
    rig.run(10 * c_step, 128);
    EXPECT_GT(rig.pedal().getLate(), 0.0f);
    EXPECT_GT(rig.engine().getNote(), 60.0f) << "and it is applied anyway";
}

TEST(ShipAhoyPedalScript, TheLeadIsTwoBlocksOfTheLargestBlockSeenSinceTheActivation)
{
    Rig rig("lead", 0);
    rig.activate();
    rig.run(128, 128);
    EXPECT_EQ(rig.pedal().leadFrames(), 256u);
    rig.run(128 + 512, 512);
    EXPECT_EQ(rig.pedal().leadFrames(), 1024u) << "a larger block raises it";
    rig.run(128 + 512 + 128, 128);
    EXPECT_EQ(rig.pedal().leadFrames(), 1024u) << "a smaller one does not lower it";
    rig.activate();
    rig.run(rig.sample() + 64, 64);
    EXPECT_EQ(rig.pedal().leadFrames(), 128u) << "the activation starts again";
    rig.pedal().setExpectedBlock(240);
    rig.activate();
    rig.run(rig.sample() + 64, 64);
    EXPECT_EQ(rig.pedal().leadFrames(), 480u) << "the expected block is a floor that stays";
}

TEST(ShipAhoyPedalScript, NothingIsLateWhateverTheBlockSizeWhenTheWorkerAnswersAfterEveryBlock)
{
    for (const uint32_t block : {16u, 64u, 128u, 513u, 1024u})
    {
        Rig rig("lead-late-" + std::to_string(block), 0);
        rig.useUser(1, c_stepNotes);
        rig.activate();
        rig.run(20 * c_step, block);
        EXPECT_EQ(rig.pedal().getLate(), 0.0f) << "block " << block;
        EXPECT_GT(rig.engine().getNote(), 60.0f) << "block " << block;
    }
}

TEST(ShipAhoyPedalFactory, SequencePlaysItsNotesWithAGlideOf70PercentOfTheStep)
{
    const std::array<float, 8> notes{57, 60, 64, 67, 64, 60, 62, 59};
    const auto matches = [&](const bool shipped, const std::string& text)
    {
        Rig rig("sequence");
        rig.pedal().setX(70.0f);
        if (shipped)
        {
            rig.pedal().setScript(1.0f);
        }
        else
        {
            rig.useUser(1, text);
        }
        rig.activate();
        bool all = true;
        for (uint64_t n = 0; n < 10; ++n)
        {
            const uint64_t due = rig.dueOfTick(24 * n);
            const float target = notes[n % notes.size()];
            rig.run(due - 1, 128);
            const float from = rig.engine().getNote();
            rig.run(due - 1 + 1050, 1);
            if (n > 0)
            {
                all = all && std::abs(rig.engine().getNote() - (from + (target - from) * 0.5f)) < 0.01f;
            }
            rig.run(due - 1 + 2100, 1);
            all = all && rig.engine().getNote() == target;
        }
        return all;
    };
    EXPECT_TRUE(matches(true, ""));
    // the same check on a copy with one note changed must fail: the oracle can see a difference
    std::ifstream in(std::string(JP_SOURCE_DIR) + "/plugins/shipahoy/scripts/sequence.lua");
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ASSERT_NE(text.find("57, 60"), std::string::npos);
    EXPECT_TRUE(matches(false, text)) << "the copy as it is passes";
    text.replace(text.find("57, 60"), 6, "58, 60");
    EXPECT_FALSE(matches(false, text));
}

TEST(ShipAhoyPedalFactory, RandomHoldStaysInTwoOctavesAroundMiddleCAndRepeatsWithTheSeed)
{
    std::vector<std::vector<float>> runs;
    for (int i = 0; i < 2; ++i)
    {
        Rig rig("random-hold");
        rig.pedal().setScript(2.0f);
        rig.activate();
        std::vector<float> notes;
        for (uint64_t n = 0; n < 40; ++n)
        {
            rig.run(rig.dueOfTick(24 * n) - 1 + 150, 128); // after the 5 % glide
            notes.push_back(rig.engine().getNote());
        }
        runs.push_back(notes);
    }
    EXPECT_EQ(runs[0], runs[1]);
    float lowest = 127.0f;
    float highest = 0.0f;
    for (const float note : runs[0])
    {
        lowest = std::min(lowest, note);
        highest = std::max(highest, note);
    }
    EXPECT_GE(lowest, 48.0f);
    EXPECT_LE(highest, 72.0f);
    EXPECT_GT(highest - lowest, 12.0f) << "uses the range";
}

TEST(ShipAhoyPedalFactory, AlternatingTrillsBetweenTheLowerAndTheUpperPart)
{
    Rig rig("alternating");
    rig.pedal().setX(10.0f);
    rig.pedal().setScript(3.0f);
    rig.activate();
    for (uint64_t n = 0; n < 30; ++n)
    {
        rig.run(rig.dueOfTick(24 * n) - 1 + 300, 128); // after the 10 % glide
        const float note = rig.engine().getNote();
        if (n % 2 == 0)
        {
            EXPECT_GE(note, 48.0f) << n;
            EXPECT_LE(note, 48.0f + 0.4f * 24.0f) << n;
        }
        else
        {
            EXPECT_GE(note, 72.0f - 0.4f * 24.0f) << n;
            EXPECT_LE(note, 72.0f) << n;
        }
    }
}

// --- played notes (OnNewPitch, OnPitchEnd, notes.lua and the scripts that follow what is played) ---

namespace
{

constexpr double c_twoPi{6.283185307179586};
constexpr uint64_t c_phraseEnd{134400}; // 2.8 s

struct Tone
{
    double start;
    double end;
    double hz;
};

/// A note with a little of its harmonics: 5 ms attack, steady until it ends, silence outside the tones.
std::function<float(uint64_t)> playing(std::vector<Tone> tones)
{
    return [tones = std::move(tones)](const uint64_t index)
    {
        const double t = static_cast<double>(index) / static_cast<double>(c_rate);
        for (const Tone& tone : tones)
        {
            if (t >= tone.start && t < tone.end)
            {
                const double local = t - tone.start;
                const double w = c_twoPi * tone.hz * local;
                const double attack = std::min(1.0, local / 0.005);
                return static_cast<float>(0.25 * attack *
                                          (std::sin(w) + 0.4 * std::sin(2.0 * w) + 0.2 * std::sin(3.0 * w)));
            }
        }
        return 0.0f;
    };
}

/// White noise of amplitude 0.5 from `start` to `end` seconds (a hash of the sample index: the same for any block
/// size).
std::function<float(uint64_t)> noiseBetween(const double start, const double end)
{
    return [start, end](const uint64_t index)
    {
        const double t = static_cast<double>(index) / static_cast<double>(c_rate);
        if (t < start || t >= end)
        {
            return 0.0f;
        }
        auto x = static_cast<uint32_t>(index * 2654435761ULL);
        x ^= x >> 15;
        x *= 2246822519U;
        x ^= x >> 13;
        x *= 3266489917U;
        x ^= x >> 16;
        return static_cast<float>(x) / 2147483648.0f * 0.5f - 0.5f;
    };
}

/// B3 at 0.30 s to 1.00 s, F4 at 1.40 s to 2.20 s: MIDI notes 59 and 65, neither in the fallback scale of notes.lua.
std::vector<Tone> phrase()
{
    return {{0.30, 1.00, 246.9417}, {1.40, 2.20, 349.2282}};
}

/// The note of the first hook goes through a timer to the filter (a clock event, so with the lead).
const char* const c_heardToTimer = "local heard = 0\n"
                                   "function OnNewPitch(note, hz, pitch, periodicity, level)\n"
                                   "    heard = pitch\n"
                                   "    SetTimer(0, { ms = 1 })\n"
                                   "end\n"
                                   "function OnTimer(id)\n"
                                   "    SetCutoff(heard, { glide = 0 })\n"
                                   "end\n";

struct Change
{
    double seconds;
    float note;
};

/// Runs the phrase in blocks of 64 and lists when the base note changed.
std::vector<Change> noteChanges(Rig& rig, const uint64_t until = c_phraseEnd)
{
    std::vector<Change> changes;
    rig.run(4800, 64);
    float last = rig.engine().getNote();
    while (rig.sample() < until)
    {
        rig.run(rig.sample() + 64, 64);
        const float note = rig.engine().getNote();
        if (note != last)
        {
            changes.push_back({static_cast<double>(rig.sample()) / static_cast<double>(c_rate), note});
            last = note;
        }
    }
    return changes;
}

/// The notes of the window of notes.lua: the end of the fallback scale, then the played notes, the last five.
std::vector<float> windowOf(const std::vector<int>& played)
{
    std::vector<float> all{57, 60, 62, 64, 67};
    all.insert(all.end(), played.begin(), played.end());
    return {all.end() - 5, all.end()};
}

/// The base note of the filter when it has had time to land, at each of the first `steps` steps of the clock (240
/// BPM at 1/16: a step is 62.5 ms; step n calls OnStep at 10 ms + n * 62.5 ms, a lead of 10 ms after the clock's
/// tick, and its command acts a lead later).
std::vector<float> landedNotes(Rig& rig, const size_t steps, const uint64_t landing)
{
    std::vector<float> landed;
    for (uint64_t n = 0; n < steps; ++n)
    {
        rig.run(rig.dueOfTick(24 * n) - 1 + landing, 128);
        landed.push_back(rig.engine().getNote());
    }
    return landed;
}

}

TEST(ShipAhoyPedalPitch, APlayedNoteReachesTheScriptWithItsPitchAboutEightyMillisecondsAfterItsOnset)
{
    Rig rig("pitch-played");
    rig.useUser(1, c_heardToTimer);
    rig.setSignal(playing(phrase()));
    rig.activate();
    const std::vector<Change> changes = noteChanges(rig);
    ASSERT_EQ(changes.size(), 2u) << "one note each, no retrigger, no release";
    EXPECT_NEAR(changes[0].note, 59.0f, 0.05f);
    EXPECT_NEAR(changes[1].note, 65.0f, 0.05f);
    EXPECT_GT(changes[0].seconds, 0.30 + 0.060);
    EXPECT_LT(changes[0].seconds, 0.30 + 0.150);
    EXPECT_GT(changes[1].seconds, 1.40 + 0.060);
    EXPECT_LT(changes[1].seconds, 1.40 + 0.150);
}

TEST(ShipAhoyPedalPitch, NoiseAndSilenceGiveNoNote)
{
    Rig rig("pitch-noise");
    rig.useUser(1, c_heardToTimer);
    rig.setSignal(noiseBetween(0.3, 2.2));
    rig.activate();
    EXPECT_TRUE(noteChanges(rig).empty());
}

TEST(ShipAhoyPedalPitch, TheEndOfANoteReachesOnPitchEndAndOneHookIsEnoughToListen)
{
    Rig rig("pitch-end");
    rig.useUser(1, "local ended = 0\n"
                   "function OnPitchEnd(note)\n"
                   "    ended = note\n"
                   "    SetTimer(0, { ms = 1 })\n"
                   "end\n"
                   "function OnTimer(id)\n"
                   "    SetCutoff(100 + ended - 59, { glide = 0 })\n"
                   "end\n");
    rig.setSignal(playing(phrase()));
    rig.activate();
    const std::vector<Change> changes = noteChanges(rig);
    ASSERT_EQ(changes.size(), 2u);
    EXPECT_EQ(changes[0].note, 100.0f) << "B3 (59) ended";
    EXPECT_EQ(changes[1].note, 106.0f) << "F4 (65) ended";
    EXPECT_GT(changes[0].seconds, 1.00);
    EXPECT_LT(changes[0].seconds, 1.25);
    EXPECT_GT(changes[1].seconds, 2.20);
    EXPECT_LT(changes[1].seconds, 2.45);
}

TEST(ShipAhoyPedalPitch, TheCommandsOfAPitchHookAreAppliedAndAFailingHookDoesNotStopTheClockHooks)
{
    {
        Rig rig("pitch-command");
        rig.useUser(1, "function OnNewPitch(note, hz, pitch, periodicity, level)\n"
                       "    SetCutoff(pitch + 12, { glide = 0 })\n"
                       "end\n");
        rig.setSignal(playing(phrase()));
        rig.activate();
        const std::vector<Change> changes = noteChanges(rig);
        ASSERT_EQ(changes.size(), 2u);
        EXPECT_NEAR(changes[0].note, 71.0f, 0.05f);
        EXPECT_NEAR(changes[1].note, 77.0f, 0.05f);
        EXPECT_EQ(rig.pedal().getScriptStatus(), 1.0f);
    }
    {
        Rig rig("pitch-failing");
        rig.useUser(1, "function OnNewPitch(note, hz, pitch, periodicity, level)\n"
                       "    error('boom')\n"
                       "end\n"
                       "function OnStep(step)\n"
                       "    SetCutoff(70, { glide = 0 })\n"
                       "end\n");
        rig.setSignal(playing(phrase()));
        rig.activate();
        noteChanges(rig);
        EXPECT_EQ(rig.engine().getNote(), 70.0f) << "OnStep works again after a hook that failed";
    }
}

TEST(ShipAhoyPedalPitch, TheDetectorRunsOnlyWhileTheScriptDefinesAPitchHook)
{
    Rig none("listen-none");
    none.activate();
    none.run(4096);
    EXPECT_FALSE(none.pedal().isListening()) << "the default script plays no notes";

    Rig stepsOnly("listen-steps");
    stepsOnly.useUser(1, c_stepNotes);
    stepsOnly.activate();
    stepsOnly.run(4096);
    EXPECT_FALSE(stepsOnly.pedal().isListening()) << "a script without a pitch hook";

    Rig listening("listen-hook");
    listening.useUser(1, c_heardToTimer);
    listening.activate();
    listening.run(4096);
    EXPECT_TRUE(listening.pedal().isListening());
    listening.pedal().setScript(0.0f);
    listening.run(8192);
    EXPECT_FALSE(listening.pedal().isListening()) << "a script without a pitch hook again";
}

TEST(ShipAhoyPedalPitch, TheOutputDoesNotDependOnTheBlockSize)
{
    std::vector<std::vector<float>> outputs;
    for (const uint32_t block : {16u, 128u, 512u})
    {
        Rig rig("pitch-blocks-" + std::to_string(block), 720);
        rig.useUser(1, "local heard = 0\n"
                       "function OnNewPitch(note, hz, pitch, periodicity, level)\n"
                       "    heard = pitch\n"
                       "    SetTimer(0, { ms = 1 })\n"
                       "end\n"
                       "function OnTimer(id)\n"
                       "    SetCutoff(heard - 24, { glide = 0 })\n"
                       "end\n");
        rig.setSignal(playing(phrase()));
        rig.record();
        rig.activate();
        rig.run(c_phraseEnd, block);
        outputs.push_back(rig.output());
        EXPECT_LT(rig.engine().getNote(), 60.0f) << "the notes moved the filter, block " << block;
    }
    EXPECT_EQ(outputs[0], outputs[1]);
    EXPECT_EQ(outputs[0], outputs[2]);
}

TEST(ShipAhoyNotes, RememberTheLastFiveNotesAndBuildWindowsPitchClassesAndSequences)
{
    // the checks run in the script; it moves the filter to 100 if all hold and to 20 if one does not
    Rig rig("notes-lua");
    rig.useUser(1, "import \"notes\"\n"
                   "local ok = true\n"
                   "local function check(condition) if not condition then ok = false end end\n"
                   "local function same(a, b)\n"
                   "    if #a ~= #b then return false end\n"
                   "    for i = 1, #a do if a[i] ~= b[i] then return false end end\n"
                   "    return true\n"
                   "end\n"
                   "check(Notes.played() == 0 and Notes.last(1) == nil)\n"
                   "check(same(Notes.window(), {57, 60, 62, 64, 67}))\n"
                   "check(same(Notes.pitchClasses(), {0, 2, 4, 7, 9}))\n"
                   "Notes.remember(72)\n"
                   "check(Notes.played() == 1 and Notes.last(1) == 72)\n"
                   "check(same(Notes.window(), {60, 62, 64, 67, 72}))\n"
                   "for _, n in ipairs({74, 76, 77, 79}) do Notes.remember(n) end\n"
                   "check(same(Notes.window(), {72, 74, 76, 77, 79}) and Notes.played() == 5)\n"
                   "Notes.remember(81)\n"
                   "check(same(Notes.window(), {74, 76, 77, 79, 81}) and Notes.played() == 5)\n"
                   "check(Notes.last(1) == 81 and Notes.last(5) == 74 and Notes.last(6) == nil)\n"
                   "check(same(Notes.pitchClasses(), {2, 4, 5, 7, 9}))\n"
                   "check(Notes.has(4) and not Notes.has(3))\n"
                   "check(same(Notes.intervals(74), {0, 2, 3, 5, 7}))\n"
                   "check(same(Notes.intervals(77), {0, 2, 4, 9, 11}))\n"
                   "Notes.remember(81)\n"
                   "check(same(Notes.window(), {76, 77, 79, 81, 81}))\n"
                   "check(same(Notes.pitchClasses(), {4, 5, 7, 9}))\n"
                   "check(same(Notes.sequence(7), {76, 77, 79, 81, 81, 76, 77}))\n"
                   "Notes.clear()\n"
                   "check(Notes.played() == 0 and same(Notes.window(), {57, 60, 62, 64, 67}))\n"
                   "function OnStep(step)\n"
                   "    SetCutoff(ok and 100 or 20, { glide = 0 })\n"
                   "end\n");
    rig.activate();
    rig.run(rig.dueOfTick(0) + 100);
    EXPECT_EQ(rig.engine().getNote(), 100.0f);
    EXPECT_EQ(rig.pedal().getScriptStatus(), 1.0f);
}

TEST(ShipAhoyPedalFactory, FollowStepsThroughTheLastFiveNotesPlayed)
{
    Rig rig("follow");
    rig.pedal().setX(70.0f);
    rig.pedal().setScript(4.0f);
    rig.setSignal(playing(phrase()));
    rig.activate();
    const std::vector<float> landed = landedNotes(rig, 41, 2200);
    const std::vector<float> before = windowOf({});
    const std::vector<float> afterFirst = windowOf({59});
    const std::vector<float> afterSecond = windowOf({59, 65});
    // the notes arrive at 0.36 to 0.45 s and 1.46 to 1.55 s; the steps in between are not asserted
    for (size_t n = 0; n <= 4; ++n)
    {
        EXPECT_EQ(landed[n], before[n % 5]) << "step " << n;
    }
    for (size_t n = 8; n <= 22; ++n)
    {
        EXPECT_EQ(landed[n], afterFirst[n % 5]) << "step " << n;
    }
    for (size_t n = 26; n <= 40; ++n)
    {
        EXPECT_EQ(landed[n], afterSecond[n % 5]) << "step " << n;
    }
}

TEST(ShipAhoyPedalFactory, AnswerClimbsThroughThePitchClassesOfThePlayedNotesFromTheNewestOne)
{
    Rig rig("answer");
    rig.pedal().setX(50.0f);
    rig.pedal().setScript(5.0f);
    rig.setSignal(playing(phrase()));
    rig.activate();
    const std::vector<float> landed = landedNotes(rig, 41, 1700);

    // the phrase climbs from an octave above the newest note (65) through the distances above it of the pitch classes
    // of the window (each once, ascending) and comes back down
    const std::vector<float> window = windowOf({59, 65});
    const int root = 65;
    std::vector<int> distances;
    for (const float note : window)
    {
        const int distance = ((static_cast<int>(note) - root) % 12 + 12) % 12;
        if (std::find(distances.begin(), distances.end(), distance) == distances.end())
        {
            distances.push_back(distance);
        }
    }
    std::sort(distances.begin(), distances.end());
    const auto count = static_cast<int>(distances.size());
    const int period = std::max(1, 2 * count - 2);
    bool matched = false;
    for (int first : {24, 25}) // the first step after the second note arrived: it restarts the phrase there
    {
        bool all = true;
        for (int n = 26; n <= 40; ++n)
        {
            const int k = (n - first) % period;
            const int index = k < count ? k : period - k;
            all = all && landed[static_cast<size_t>(n)] ==
                             static_cast<float>(root + 12 + distances[static_cast<size_t>(index)]);
        }
        matched = matched || all;
    }
    EXPECT_TRUE(matched);
}

TEST(ShipAhoyPedalFactory, EchoMovesToThePlayedNoteAndStaysThere)
{
    Rig rig("echo");
    rig.pedal().setX(20.0f);
    rig.pedal().setScript(6.0f);
    rig.setSignal(playing(phrase()));
    rig.activate();
    const std::vector<float> landed = landedNotes(rig, 41, 900);
    for (size_t n = 0; n <= 4; ++n)
    {
        EXPECT_NEAR(landed[n], 57.0f, 1e-5f) << "the first note of the fallback until something is played";
    }
    for (size_t n = 8; n <= 22; ++n)
    {
        EXPECT_NEAR(landed[n], 59.0f, 0.05f) << "step " << n;
    }
    for (size_t n = 26; n <= 40; ++n)
    {
        EXPECT_NEAR(landed[n], 65.0f, 0.05f) << "step " << n;
    }
}

// --- the first factory script, X and Y, the setters ---

TEST(ShipAhoyPedalHop, HopStaysInThreeOctavesAroundOneKilohertzAndRepeatsWithTheSeed)
{
    std::vector<std::vector<float>> runs;
    for (int i = 0; i < 2; ++i)
    {
        Rig rig("hop");
        rig.activate(); // script 0
        std::vector<float> notes;
        for (uint64_t n = 0; n < 60; ++n)
        {
            rig.run(rig.dueOfTick(24 * n) - 1 + 150, 128); // after the 5 % glide
            notes.push_back(rig.engine().getNote());
        }
        runs.push_back(notes);
    }
    EXPECT_EQ(runs[0], runs[1]);
    const float centre = 69.0f + 12.0f * std::log2(1000.0f / 440.0f);
    const auto [lowest, highest] = std::minmax_element(runs[0].begin(), runs[0].end());
    EXPECT_GE(*lowest, centre - 18.0f);
    EXPECT_LE(*highest, centre + 18.0f);
    EXPECT_GT(*highest - *lowest, 20.0f) << "uses the range";
}

TEST(ShipAhoyPedalHop, XIsTheGlideAndYTheResonance)
{
    Rig rig("hop-xy");
    rig.pedal().setX(100.0f);
    rig.pedal().setY(0.0f);
    rig.activate();
    rig.run(24000, 128);
    EXPECT_NEAR(rig.engine().getFilterResonance(), 0.0f, 0.02f) << "Y = 0 is no resonance (OnStart)";
    // with X = 100 the glide lasts the whole step: the note is still on its way halfway through the step and just
    // before the next one (the glide of step 1 starts at due and ends a step later)
    const uint64_t due = rig.dueOfTick(24 * 20); // well after the 24000 samples already run
    rig.run(due - 1, 128);
    const float from = rig.engine().getNote();
    rig.run(due - 1 + c_step / 2, 128);
    const float halfway = rig.engine().getNote();
    rig.run(due - 1 + c_step - 10, 1);
    const float late = rig.engine().getNote();
    EXPECT_NE(halfway, from);
    EXPECT_NE(late, halfway) << "still gliding";
    rig.pedal().setY(85.0f);
    rig.run(rig.sample() + 4800, 128);
    EXPECT_GT(rig.engine().getFilterResonance(), 0.5f) << "OnControl set the resonance";
}

TEST(ShipAhoyPedalControl, TheGettersGiveTheKnobsAndTheHookTellsOncePerChange)
{
    Rig rig("control");
    rig.useUser(1, "local calls = 0\n"
                   "function OnControl(x, y)\n"
                   "    calls = calls + 1\n"
                   "    SetCutoff(20 + x / 10 + y / 1000 + calls * 10, { glide = 0 })\n"
                   "end\n"
                   "function OnStart()\n"
                   "    SetCutoff(GetX() + GetY() / 1000, { glide = 0 })\n"
                   "end\n");
    rig.pedal().setX(40.0f);
    rig.pedal().setY(25.0f);
    rig.activate();
    rig.run(1024, 128);
    EXPECT_NEAR(rig.engine().getNote(), 40.025f, 1e-4f) << "the getters in OnStart";
    rig.pedal().setX(50.0f);
    rig.run(rig.sample() + 512, 128);
    EXPECT_NEAR(rig.engine().getNote(), 20.0f + 5.0f + 0.025f + 10.0f, 1e-4f) << "the hook with both values";
    rig.run(rig.sample() + 4096, 128);
    EXPECT_NEAR(rig.engine().getNote(), 35.025f, 1e-4f) << "and not again while nothing changes";
    rig.pedal().setY(75.0f);
    rig.run(rig.sample() + 512, 128);
    EXPECT_NEAR(rig.engine().getNote(), 20.0f + 5.0f + 0.075f + 20.0f, 1e-4f);
}

TEST(ShipAhoyPedalControl, AScriptThatIgnoresXAndYSoundsTheSame)
{
    std::vector<std::vector<float>> outputs;
    for (const float x : {0.0f, 100.0f})
    {
        Rig rig("control-ignored");
        rig.useUser(1, c_stepNotes);
        rig.pedal().setX(x);
        rig.pedal().setY(100.0f - x);
        rig.record();
        rig.setSignal(playing(phrase()));
        rig.activate();
        rig.run(20000, 128);
        outputs.push_back(rig.output());
    }
    EXPECT_EQ(outputs[0], outputs[1]);
}

TEST(ShipAhoyPedalControl, ACommandOfTheControlHookIsAppliedAtTheNextBlockWithoutTheLead)
{
    Rig rig("control-immediate");
    rig.useUser(1, "function OnControl(x, y)\n    SetCutoff(90, { glide = 0 })\nend\n");
    rig.activate();
    rig.run(2048, 128);
    EXPECT_NE(rig.engine().getNote(), 90.0f);
    rig.pedal().setX(60.0f);
    rig.run(rig.sample() + 128, 128); // the block that tells the hook; the worker answers after it
    EXPECT_NE(rig.engine().getNote(), 90.0f);
    rig.run(rig.sample() + 128, 128); // the next block applies the answer at its first sample
    EXPECT_EQ(rig.engine().getNote(), 90.0f) << "less than the lead of 480 samples";
}

TEST(ShipAhoyPedalSetters, ResonanceTypeAndDriftWorkFromTheFirstBlockAfterOnStart)
{
    Rig rig("setters");
    rig.useUser(1, "function OnStart()\n    SetResonance(40)\n    SetFilterType(FilterType.Bp12)\n"
                   "    SetDrift(3, { rate = 2 })\nend\n");
    rig.activate();
    rig.run(4800, 128);
    EXPECT_EQ(rig.engine().getFilterPresetIndex(), ShipAhoyEngine::c_filterTypePresets[5]);
    EXPECT_GT(rig.engine().getFilterResonance(), 0.0f);
    float widest = 0.0f;
    for (int i = 0; i < 100; ++i)
    {
        rig.run(rig.sample() + 480, 128);
        widest = std::max(widest, std::abs(rig.engine().getModulation()));
    }
    EXPECT_GT(widest, 0.5f) << "the drift moves";
}

TEST(ShipAhoyPedalSetters, AValueOutsideItsRangeIsClampedOrDropped)
{
    Rig rig("setters-range");
    rig.useUser(1, "function OnStart()\n    SetResonance(250)\n    SetFilterType(99)\nend\n");
    rig.activate();
    rig.run(4800, 128);
    EXPECT_EQ(rig.pedal().getScriptStatus(), 1.0f) << "a warning in the log, not an error";
    // 250 is clamped to 100 %, the type is dropped (an integer outside its range), so Lp12 stays
    EXPECT_EQ(rig.engine().getFilterPresetIndex(), ShipAhoyEngine::c_filterTypePresets[1]);
    Rig reference("setters-range-reference");
    reference.useUser(1, "function OnStart()\n    SetResonance(100)\nend\n");
    reference.activate();
    reference.run(4800, 128);
    EXPECT_FLOAT_EQ(rig.engine().getFilterResonance(), reference.engine().getFilterResonance());
}

// --- the factory scripts ---

TEST(ShipAhoyPedalFactory, EveryFactoryScriptLoadsAndRunsWithoutErrorsOrLateCommands)
{
    for (size_t index = 0; index < 9; ++index)
    {
        Rig rig("factory-" + std::to_string(index));
        rig.pedal().setScript(static_cast<float>(index));
        rig.setSignal(playing(phrase()));
        rig.activate();
        rig.run(c_phraseEnd, 128);
        EXPECT_EQ(rig.pedal().getScriptStatus(), 1.0f) << c_shipAhoyScriptEntries[index].name;
        EXPECT_EQ(rig.pedal().getLate(), 0.0f) << c_shipAhoyScriptEntries[index].name;
        EXPECT_EQ(rig.pedal().getDropped(), 0.0f) << c_shipAhoyScriptEntries[index].name;
        EXPECT_NE(rig.engine().getNote(), 69.0f + 12.0f * std::log2(1000.0f / 440.0f))
            << c_shipAhoyScriptEntries[index].name << " moved the filter";
    }
}

TEST(ShipAhoyPedalFactory, EveryExampleLoadsAndRunsWithoutErrorsOrLateCommands)
{
    const fs::path folder = std::string(JP_SOURCE_DIR) + "/plugins/shipahoy/scripts/examples";
    std::vector<fs::path> files;
    for (const auto& file : fs::directory_iterator(folder))
    {
        if (file.path().extension() == ".lua")
        {
            files.push_back(file.path());
        }
    }
    std::sort(files.begin(), files.end());
    ASSERT_GE(files.size(), 6u);
    for (const fs::path& file : files)
    {
        std::ifstream in(file);
        ASSERT_TRUE(in.good()) << file;
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        Rig rig("example-" + file.stem().string());
        rig.useUser(1, text);
        rig.setSignal(playing(phrase()));
        rig.activate();
        rig.run(c_phraseEnd, 128);
        EXPECT_EQ(rig.pedal().getScriptStatus(), 1.0f) << file.filename();
        EXPECT_EQ(rig.pedal().host().hookFailures(), 0u) << file.filename();
        EXPECT_EQ(rig.pedal().getLate(), 0.0f) << file.filename();
        EXPECT_EQ(rig.pedal().getDropped(), 0.0f) << file.filename();
        EXPECT_NE(rig.engine().getNote(), 69.0f + 12.0f * std::log2(1000.0f / 440.0f))
            << file.filename() << " moved the filter";
    }
}

TEST(ShipAhoyPedalFactory, EveryScriptSaysWhatXAndYDo)
{
    size_t checked = 0;
    for (const ShipAhoyScriptEntry& entry : c_shipAhoyScriptEntries)
    {
        if (entry.origin != script::Origin::Factory)
        {
            continue;
        }
        std::ifstream in(std::string(JP_SOURCE_DIR) + "/plugins/shipahoy/scripts/" + entry.name + ".lua");
        ASSERT_TRUE(in.good()) << entry.name;
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        EXPECT_NE(text.find("\n-- X: "), std::string::npos) << entry.name << " has no X line";
        EXPECT_NE(text.find("\n-- Y: "), std::string::npos) << entry.name << " has no Y line";
        ++checked;
    }
    EXPECT_EQ(checked, 9u);
}

TEST(ShipAhoyPedalFactory, TheFactoryScriptsAreTheOnesOfTheScriptPort)
{
    EXPECT_STREQ(c_shipAhoyScriptEntries[0].name, "hop");
    size_t files = 0;
    for (const auto& file : fs::directory_iterator(std::string(JP_SOURCE_DIR) + "/plugins/shipahoy/scripts"))
    {
        if (file.path().extension() == ".lua" && file.path().stem() != "notes")
        {
            ++files;
        }
    }
    EXPECT_EQ(files, 9u) << "one file per factory entry, besides the module notes.lua";
}

}
