#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

#include "tests/unit/TestHelpers.h"
#include "plugins/morphexsynth/MorphexsynthPedal.h"

namespace jp::test
{

namespace
{
constexpr float c_rate{48000.0f};

struct Stereo
{
    std::vector<float> left;
    std::vector<float> right;
};

/// The user's scripts folder of the tests: below the working directory, one per test process.
std::string userFolder()
{
    return (std::filesystem::current_path() / "morphex-user" / std::to_string(::getpid())).string();
}

/// A pedal and the time: render() runs the audio, note() and the others queue a MIDI message at the current time.
class Player
{
  public:
    explicit Player(const float sampleRate = c_rate)
        : m_pedal(std::make_unique<MorphexsynthPedal>())
    {
        m_pedal->setScriptDirectories(std::string(JP_SOURCE_DIR) + "/plugins/morphexsynth/scripts", userFolder());
        m_pedal->setSampleRate(sampleRate);
        m_pedal->reset();
    }

    MorphexsynthPedal& pedal()
    {
        return *m_pedal;
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

    void noteOff(const int channel, const int note)
    {
        midi(static_cast<uint8_t>(0x80 | (channel - 1)), static_cast<uint8_t>(note), 64);
    }

    void bend(const int channel, const int value14)
    {
        midi(static_cast<uint8_t>(0xE0 | (channel - 1)), static_cast<uint8_t>(value14 & 0x7F),
             static_cast<uint8_t>(value14 >> 7));
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

    /// The effects off, so that a measurement sees the voices alone.
    void effectsOff()
    {
        m_pedal->setReverb(-60.0f);
        m_pedal->setChorus(0.0f);
        m_pedal->setPhaser(0.0f);
    }

  private:
    std::unique_ptr<MorphexsynthPedal> m_pedal;
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

bool finiteAndBounded(const Stereo& out, const float limit)
{
    return std::all_of(out.left.begin(), out.left.end(),
                       [limit](float v) { return std::isfinite(v) && std::abs(v) < limit; }) &&
           std::all_of(out.right.begin(), out.right.end(),
                       [limit](float v) { return std::isfinite(v) && std::abs(v) < limit; });
}

/// The frequency of the strongest component within +-100 cent of centreHz, by a search on a 1 cent grid.
double peakNear(const std::vector<float>& signal, const double centreHz)
{
    double best = 0.0;
    double bestFrequency = centreHz;
    for (int cent = -100; cent <= 100; ++cent)
    {
        const double frequency = centreHz * std::pow(2.0, cent / 1200.0);
        const double level = std::abs(toneAmplitude(signal, frequency, c_rate));
        if (level > best)
        {
            best = level;
            bestFrequency = frequency;
        }
    }
    return bestFrequency;
}

double cents(const double measured, const double expected)
{
    return 1200.0 * std::log2(measured / expected);
}

std::vector<float> slice(const std::vector<float>& x, const size_t from, const size_t to)
{
    return {x.begin() + static_cast<std::ptrdiff_t>(from), x.begin() + static_cast<std::ptrdiff_t>(to)};
}

std::vector<float> slice(const Stereo& x, const size_t from, const size_t to)
{
    return slice(x.left, from, to);
}

constexpr size_t c_crystalBells{2};
constexpr size_t c_warmBass{9};
constexpr size_t c_airyPad{1};
constexpr size_t c_retroPoly{7};
}

TEST(MorphexsynthPedal, ThereIsNoSoundUntilANoteArrives)
{
    Player player;
    const auto out = player.render(24000);
    EXPECT_EQ(rms(out.left, 0, out.left.size()), 0.0);
    EXPECT_EQ(rms(out.right, 0, out.right.size()), 0.0);
    EXPECT_EQ(player.pedal().activeVoices(), 0u);
}

TEST(MorphexsynthPedal, ANoteSoundsInBothChannelsAndDiesAfterItsRelease)
{
    Player player;
    player.pedal().setPatch(static_cast<float>(c_warmBass));
    player.effectsOff();
    player.noteOn(1, 45);
    const auto held = player.render(24000);
    EXPECT_GT(rms(held.left, 12000, 24000), 0.02);
    EXPECT_GT(rms(held.right, 12000, 24000), 0.02);
    EXPECT_EQ(player.pedal().activeVoices(), 1u);
    player.noteOff(1, 45);
    (void) player.render(24000);
    EXPECT_EQ(player.pedal().activeVoices(), 0u);
    const auto after = player.render(4800);
    EXPECT_LT(rms(after.left, 0, after.left.size()), 1e-5);
}

TEST(MorphexsynthPedal, EveryPatchPlaysChordsFiniteAndBounded)
{
    for (size_t patch = 0; patch < c_factoryPatchCount; ++patch)
    {
        Player player;
        player.pedal().setPatch(static_cast<float>(patch));
        for (const int note : {36, 48, 60, 64, 67, 72})
        {
            player.noteOn(1, note, 110);
        }
        player.bend(1, 12000);
        player.midi(0xB0, 74, 100);
        player.midi(0xD0, 90);
        const auto out = player.render(48000);
        EXPECT_TRUE(finiteAndBounded(out, 4.0f)) << patch;
        EXPECT_GT(rms(out.left, 4800, 24000), 1e-3) << patch;
    }
}

TEST(MorphexsynthPedal, ThePatchPortClampsToTheTable)
{
    Player player;
    player.pedal().setPatch(99.0f);
    EXPECT_EQ(player.pedal().getPatchIndex(), c_factoryPatchCount - 1);
    player.pedal().setPatch(-5.0f);
    EXPECT_EQ(player.pedal().getPatchIndex(), 0u);
}

TEST(MorphexsynthPedal, OnlyTheMasterAndTheMemberChannelsOfTheZonePlay)
{
    Player player;
    player.pedal().setMpeUpper(5.0f);
    player.noteOn(8, 60);
    EXPECT_EQ(player.pedal().activeVoices(), 0u) << "channel 8 is above the zone";
    player.noteOn(3, 60);
    EXPECT_EQ(player.pedal().activeVoices(), 1u);
    player.noteOn(1, 64);
    EXPECT_EQ(player.pedal().activeVoices(), 2u) << "the master channel plays";
    player.pedal().setMpeMaster(9.0f);
    player.noteOn(1, 67);
    EXPECT_EQ(player.pedal().activeVoices(), 2u) << "channel 1 is no longer the master";
    player.noteOn(9, 67);
    EXPECT_EQ(player.pedal().activeVoices(), 3u);
}

TEST(MorphexsynthPedal, ANoteOnWithVelocityZeroIsANoteOff)
{
    Player player;
    player.pedal().setPatch(static_cast<float>(c_warmBass));
    player.noteOn(1, 40);
    (void) player.render(4800);
    player.noteOn(1, 40, 0);
    (void) player.render(24000);
    EXPECT_EQ(player.pedal().activeVoices(), 0u);
}

TEST(MorphexsynthPedal, ThePitchBendActsOnTheNotesOfItsChannelOnly)
{
    Player player;
    player.pedal().setPatch(static_cast<float>(c_crystalBells));
    player.pedal().setBendRange(0.0f); // the range of the patch, 2 semitones
    player.effectsOff();
    player.noteOn(2, 57);
    player.noteOn(3, 69);
    (void) player.render(24000);
    player.bend(3, 16383);
    const auto bent = player.render(48000);
    const auto window = slice(bent, 12000, 36000);
    EXPECT_NEAR(cents(peakNear(window, 220.0), 220.0), 0.0, 8.0) << "channel 2 stays";
    EXPECT_NEAR(cents(peakNear(window, 440.0 * std::pow(2.0, 2.0 / 12.0)), 440.0 * std::pow(2.0, 2.0 / 12.0)), 0.0, 8.0)
        << "channel 3 went up two semitones";
}

TEST(MorphexsynthPedal, TheBendRangeSettingReplacesTheRangeOfThePatch)
{
    const double expected = 440.0 * 2.0;
    Player player;
    player.pedal().setPatch(static_cast<float>(c_crystalBells));
    player.pedal().setBendRange(12.0f);
    player.effectsOff();
    player.noteOn(2, 69);
    player.bend(2, 16383);
    const auto out = player.render(48000);
    EXPECT_NEAR(cents(peakNear(slice(out, 12000, 36000), expected), expected), 0.0, 8.0);
}

TEST(MorphexsynthPedal, AValueBeforeTheNoteOnOfItsChannelIsApplied)
{
    const double expected = 440.0 * std::pow(2.0, 2.0 / 12.0);
    Player player;
    player.pedal().setPatch(static_cast<float>(c_crystalBells));
    player.pedal().setBendRange(0.0f);
    player.effectsOff();
    player.bend(2, 16383);
    player.noteOn(2, 69);
    const auto out = player.render(48000);
    EXPECT_NEAR(cents(peakNear(slice(out, 12000, 36000), expected), expected), 0.0, 8.0);
}

TEST(MorphexsynthPedal, TheSustainPedalHoldsReleasedNotes)
{
    Player player;
    player.pedal().setPatch(static_cast<float>(c_warmBass));
    player.midi(0xB0, 64, 127);
    player.noteOn(1, 40);
    (void) player.render(4800);
    player.noteOff(1, 40);
    (void) player.render(48000);
    EXPECT_EQ(player.pedal().activeVoices(), 1u) << "held by the pedal";
    player.midi(0xB0, 64, 0);
    (void) player.render(24000);
    EXPECT_EQ(player.pedal().activeVoices(), 0u);
}

TEST(MorphexsynthPedal, AllNotesOffReleasesEverything)
{
    Player player;
    player.pedal().setPatch(static_cast<float>(c_warmBass));
    for (int note = 40; note < 46; ++note)
    {
        player.noteOn(1, note);
    }
    (void) player.render(4800);
    EXPECT_EQ(player.pedal().activeVoices(), 6u);
    player.midi(0xB0, 123, 0);
    (void) player.render(24000);
    EXPECT_EQ(player.pedal().activeVoices(), 0u);
}

TEST(MorphexsynthPedal, ThePoolHasTwelveVoicesAndStealsTheOldest)
{
    Player player;
    player.pedal().setPatch(static_cast<float>(c_airyPad));
    for (int note = 40; note < 53; ++note)
    {
        player.noteOn(1, note);
        (void) player.render(480);
    }
    EXPECT_EQ(player.pedal().activeVoices(), 12u);
    const auto out = player.render(48000);
    EXPECT_TRUE(finiteAndBounded(out, 4.0f));
}

TEST(MorphexsynthPedal, TheOutputDoesNotDependOnHowTheBlocksAreCut)
{
    const auto run = [](const size_t blockSize)
    {
        Player player;
        player.pedal().setPatch(static_cast<float>(c_crystalBells));
        Stereo out;
        out.left.resize(24000);
        out.right.resize(24000);
        // events at fixed times, the audio between them in blocks of the given size
        struct Event
        {
            size_t at;
            uint8_t status, d1, d2;
        };
        const std::array<Event, 5> events{{{0, 0x91, 60, 100},
                                           {301, 0x92, 67, 90},
                                           {2000, 0xE1, 0x00, 0x60},
                                           {5001, 0x81, 60, 64},
                                           {9000, 0xB2, 74, 100}}};
        size_t position = 0;
        size_t next = 0;
        while (position < out.left.size())
        {
            while (next < events.size() && events[next].at == position)
            {
                player.midi(events[next].status, events[next].d1, events[next].d2);
                ++next;
            }
            size_t n = std::min(blockSize, out.left.size() - position);
            if (next < events.size())
            {
                n = std::min(n, events[next].at - position);
            }
            std::array<float*, 2> outputs{out.left.data() + position, out.right.data() + position};
            player.pedal().process(nullptr, outputs.data(), n);
            position += n;
        }
        return out;
    };
    const auto reference = run(24000);
    for (const size_t blockSize : {1u, 7u, 16u, 100u, 128u})
    {
        const auto out = run(blockSize);
        EXPECT_EQ(out.left, reference.left) << blockSize;
        EXPECT_EQ(out.right, reference.right) << blockSize;
    }
}

TEST(MorphexsynthPedal, ChangingThePatchAppliesItCompletely)
{
    // Grand Stage routes the velocity, Retro Poly does not: nothing of the slot may stay
    const auto play = [](const bool viaOtherPatch)
    {
        Player player;
        player.pedal().setPatch(static_cast<float>(c_airyPad));
        if (viaOtherPatch)
        {
            player.pedal().setPatch(4.0f);
            player.noteOn(1, 60, 30);
            (void) player.render(4800);
            player.pedal().setPatch(static_cast<float>(c_airyPad));
            (void) player.render(96000);
        }
        else
        {
            (void) player.render(4800 + 96000);
        }
        player.noteOn(1, 64, 20);
        return player.render(48000);
    };
    const auto direct = play(false);
    const auto indirect = play(true);
    // the effects keep their own state (sweep phase, the delay of the chorus), so compare the level
    EXPECT_NEAR(rms(indirect.left, 24000, 48000), rms(direct.left, 24000, 48000), 0.1 * rms(direct.left, 24000, 48000));
}

TEST(MorphexsynthPedal, VolumeAndTheReverbAndChorusPortsChangeTheSound)
{
    const auto level = [](const float vol)
    {
        Player player;
        player.pedal().setPatch(static_cast<float>(c_warmBass));
        player.pedal().setVol(vol);
        player.effectsOff();
        player.noteOn(1, 45);
        const auto out = player.render(24000);
        return rms(out.left, 12000, 24000);
    };
    EXPECT_NEAR(level(-6.0206f) / level(0.0f), 0.5, 0.01);

    // Warm Bass has a short release: the dry sound is gone 0.3 s after the note off, what is left is the reverb
    const auto tail = [](const float reverbDb)
    {
        Player player;
        player.pedal().setPatch(static_cast<float>(c_warmBass));
        player.pedal().setChorus(0.0f);
        player.pedal().setPhaser(0.0f);
        player.pedal().setReverb(reverbDb);
        player.noteOn(1, 45, 127);
        (void) player.render(4800);
        player.noteOff(1, 45);
        (void) player.render(14400);
        const auto after = player.render(9600);
        return rms(after.left, 0, after.left.size());
    };
    EXPECT_GT(tail(0.0f), 1e-5);
    EXPECT_LT(tail(-60.0f), 0.001 * tail(0.0f)) << "the lowest value switches the reverb off (to -100 dB)";
    EXPECT_NEAR(tail(12.0f) / tail(0.0f), 3.98, 0.1) << "12 dB";
}

TEST(MorphexsynthPedal, CutoffAndResonanceMoveThePatchsValues)
{
    const auto brightness = [](const float cutoff)
    {
        Player player;
        player.pedal().setPatch(static_cast<float>(c_warmBass));
        player.pedal().setCutoff(cutoff);
        player.effectsOff();
        player.noteOn(1, 45);
        const auto out = player.render(24000);
        const auto window = slice(out, 12000, 24000);
        double high = 0.0;
        for (double frequency = 1000.0; frequency < 3000.0; frequency += 110.0)
        {
            high += std::abs(toneAmplitude(window, frequency, c_rate));
        }
        return high;
    };
    EXPECT_GT(brightness(24.0f), 4.0 * brightness(0.0f));
    EXPECT_LT(brightness(-24.0f), 0.5 * brightness(0.0f));
}

TEST(MorphexsynthPedal, AttackOffsetLengthensAndShortensTheRise)
{
    // level of the first 40 ms after the note on; Warm Bass attacks in 2 ms, Airy Pad in 800 ms
    const auto early = [](const size_t patch, const float attackMs)
    {
        Player player;
        player.pedal().setAttack(attackMs);
        player.pedal().setPatch(static_cast<float>(patch));
        player.effectsOff();
        player.noteOn(1, 45);
        const auto out = player.render(24000);
        return rms(out.left, 0, 1920);
    };
    const double bass = early(c_warmBass, 0.0f);
    const double slow = early(c_warmBass, 300.0f);
    const double pad = early(c_airyPad, 0.0f);
    const double fast = early(c_airyPad, -700.0f);
    std::printf("[ attack   ] warm bass %.5f, +300 ms %.5f; airy pad %.5f, -700 ms %.5f\n", bass, slow, pad, fast);
    EXPECT_GT(bass, 0.0);
    EXPECT_LT(slow, 0.2 * bass) << "a 2 ms attack lengthened to 302 ms";
    EXPECT_GT(fast, 5.0 * pad) << "an 800 ms attack shortened to 100 ms";
}

TEST(MorphexsynthPedal, ReleaseOffsetLengthensTheTail)
{
    // level 60 to 100 ms after the note off; Warm Bass releases in 120 ms
    const auto tail = [](const float releaseMs)
    {
        Player player;
        player.pedal().setPatch(static_cast<float>(c_warmBass));
        player.pedal().setRelease(releaseMs);
        player.effectsOff();
        player.noteOn(1, 45);
        (void) player.render(24000);
        player.noteOff(1, 45);
        (void) player.render(2880);
        const auto out = player.render(1920);
        return rms(out.left, 0, out.left.size());
    };
    const double plain = tail(0.0f);
    const double longer = tail(2000.0f);
    const double shorter = tail(-110.0f);
    std::printf("[ release  ] tail %.6f, +2000 ms %.6f, -110 ms %.6f\n", plain, longer, shorter);
    EXPECT_GT(plain, 0.0);
    EXPECT_GT(longer, 1.5 * plain) << "the tail of 2120 ms is still near full level";
    EXPECT_LT(shorter, 0.1 * plain) << "a release of 10 ms is over after 60 ms";
}

TEST(MorphexsynthPedal, ZeroEnvelopeOffsetsLeaveThePatchUntouched)
{
    const auto render = [](const bool touch)
    {
        Player player;
        player.pedal().setPatch(static_cast<float>(c_warmBass));
        if (touch)
        {
            player.pedal().setAttack(200.0f);
            player.pedal().setRelease(500.0f);
            player.pedal().setAttack(0.0f);
            player.pedal().setRelease(0.0f);
        }
        player.noteOn(1, 45);
        auto out = player.render(12000);
        player.noteOff(1, 45);
        const auto rest = player.render(12000);
        out.left.insert(out.left.end(), rest.left.begin(), rest.left.end());
        return out.left;
    };
    EXPECT_EQ(render(false), render(true));
}

TEST(MorphexsynthPedal, ChannelPressureAndPolyPressureBothMoveTheSound)
{
    // Section Brass routes Z to the cutoff, which starts closed
    const auto level = [](const int kind, const int value)
    {
        Player player;
        player.pedal().setPatch(8.0f);
        player.effectsOff();
        player.noteOn(2, 60);
        if (kind == 1)
        {
            player.midi(0xD1, static_cast<uint8_t>(value));
        }
        else if (kind == 2)
        {
            player.midi(0xA1, 60, static_cast<uint8_t>(value));
        }
        else if (kind == 3)
        {
            player.midi(0xA1, 62, static_cast<uint8_t>(value)); // another note: no effect on this one
        }
        const auto out = player.render(48000);
        return rms(out.left, 24000, 48000);
    };
    const double none = level(0, 0);
    EXPECT_GT(level(1, 127), 1.3 * none) << "channel pressure";
    EXPECT_GT(level(2, 127), 1.3 * none) << "poly pressure of the note";
    EXPECT_NEAR(level(3, 127), none, 1e-6) << "poly pressure of another note";
}

TEST(MorphexsynthPedal, ThePressureSettingScalesWhatZDoes)
{
    const auto change = [](const float percent)
    {
        const auto level = [percent](const int pressure)
        {
            Player player;
            player.pedal().setPatch(8.0f);
            player.pedal().setPressure(percent);
            player.effectsOff();
            player.noteOn(2, 60);
            player.midi(0xD1, static_cast<uint8_t>(pressure));
            const auto out = player.render(48000);
            return rms(out.left, 24000, 48000);
        };
        return level(127) / level(0);
    };
    EXPECT_NEAR(change(0.0f), 1.0, 1e-6) << "no effect at 0 %";
    // the cutoff opens until the saw passes whole, so the level gains less and less
    EXPECT_GT(change(100.0f), 1.15);
    EXPECT_GT(change(200.0f), change(100.0f) + 0.1);
    EXPECT_GT(change(400.0f), change(200.0f));
}

TEST(MorphexsynthPedal, TheDefaultsAreQuieterThanTheBarePatchAndBendTwelveSemitones)
{
    // Vol -6 dB and Reverb -10 dB are the defaults: setting 0 for both gives the bare patch, 6 dB louder dry
    Player quiet;
    quiet.pedal().setPatch(static_cast<float>(c_warmBass));
    quiet.pedal().setReverb(-60.0f);
    quiet.pedal().setChorus(0.0f);
    quiet.pedal().setPhaser(0.0f);
    Player bare;
    bare.pedal().setPatch(static_cast<float>(c_warmBass));
    bare.pedal().setVol(0.0f);
    bare.effectsOff();
    quiet.noteOn(1, 45);
    bare.noteOn(1, 45);
    const auto a = quiet.render(24000);
    const auto b = bare.render(24000);
    EXPECT_NEAR(rms(b.left, 12000, 24000) / rms(a.left, 12000, 24000), 2.0, 0.02) << "6 dB";

    const double expected = 440.0 * 2.0;
    Player bent;
    bent.pedal().setPatch(static_cast<float>(c_crystalBells));
    bent.effectsOff();
    bent.noteOn(2, 69);
    bent.bend(2, 16383);
    const auto out = bent.render(48000);
    EXPECT_NEAR(cents(peakNear(slice(out, 12000, 36000), expected), expected), 0.0, 8.0) << "the default range is 12";
}

TEST(MorphexsynthPedal, ResetEndsTheNotesAndClearsTheTails)
{
    Player player;
    player.pedal().setPatch(static_cast<float>(c_airyPad));
    player.noteOn(1, 60);
    player.noteOn(2, 64);
    (void) player.render(48000);
    player.pedal().reset();
    EXPECT_EQ(player.pedal().activeVoices(), 0u);
    const auto out = player.render(48000);
    EXPECT_EQ(rms(out.left, 0, out.left.size()), 0.0);
    EXPECT_EQ(player.pedal().getPatchIndex(), c_airyPad) << "the settings stay";
    player.noteOn(1, 60);
    EXPECT_GT(rms(player.render(24000).left, 12000, 24000), 1e-3) << "and the patch is applied again";
}

TEST(MorphexsynthPedal, ItWorksAtOtherSampleRates)
{
    for (const float rate : {44100.0f, 96000.0f})
    {
        Player player(rate);
        player.noteOn(1, 60);
        const auto out = player.render(static_cast<size_t>(rate));
        EXPECT_TRUE(finiteAndBounded(out, 4.0f)) << rate;
        EXPECT_GT(rms(out.left, static_cast<size_t>(rate) / 2, static_cast<size_t>(rate)), 1e-3) << rate;
    }
}

TEST(MorphexsynthPedal, ControlsBeforeTheSampleRateAndProcessingWithoutAnEngineAreHarmless)
{
    MorphexsynthPedal pedal;
    pedal.setPatch(3.0f);
    pedal.setVol(-3.0f);
    const std::array<uint8_t, 3> message{0x90, 60, 100};
    pedal.midi(message.data(), 3);
    std::array<float, 64> left;
    std::array<float, 64> right;
    left.fill(1.0f);
    right.fill(1.0f);
    std::array<float*, 2> outputs{left.data(), right.data()};
    pedal.process(nullptr, outputs.data(), 64);
    EXPECT_EQ(left[10], 0.0f);
    pedal.setSampleRate(c_rate);
    EXPECT_EQ(pedal.getPatchIndex(), 3u) << "the control values wait for the engine";
}

namespace
{

constexpr std::array<const char*, 10> c_patchNames{"4 Ever Mini",   "Airy Pad",   "Crystal Bells", "Cutting Lead",
                                                   "Grand Stage",   "Harp Pluck", "Kick Punch",    "Retro Poly",
                                                   "Section Brass", "Warm Bass"};

/// What the player does to the note: velocity, and the value of a pressure message or a pitch bend (-1: none).
struct Expression
{
    int velocity{100};
    int pressure{-1};
    int bend{-1};
};

/// A window of the left output of middle C on channel 1 (a keyboard without MPE) with the effects off; the length is a
/// power of two (for the spectrum).
std::vector<float> playMiddleC(const size_t patch, const Expression& expression, const size_t start,
                               const size_t length)
{
    Player player;
    player.pedal().setPatch(static_cast<float>(patch));
    player.effectsOff();
    if (expression.bend >= 0)
    {
        player.bend(1, expression.bend);
    }
    player.noteOn(1, 60, expression.velocity);
    if (expression.pressure >= 0)
    {
        player.midi(0xD0, static_cast<uint8_t>(expression.pressure));
    }
    return slice(player.render(start + length), start, start + length);
}

/// Energy in dB of the window above fromHz (0: all of it).
double energyDb(const std::vector<float>& window, const double fromHz)
{
    const auto magnitude = hannMagnitudeSpectrum(window);
    const double binHz = static_cast<double>(c_rate) / static_cast<double>(window.size());
    double sum = 0.0;
    for (size_t i = static_cast<size_t>(fromHz / binHz); i < magnitude.size(); ++i)
    {
        sum += magnitude[i] * magnitude[i];
    }
    return 10.0 * std::log10(std::max(sum, 1e-300));
}

constexpr size_t c_earlyStart{960}; // 20 ms into the note, a window of 85 ms (percussive patches decay quickly)
constexpr size_t c_earlyLength{4096};
constexpr size_t c_lateStart{7200}; // 150 ms into the note, 170 ms
constexpr size_t c_lateLength{8192};

}

TEST(MorphexsynthPedal, EveryPatchBrightensWithPressure)
{
    // the brightness is the energy above 1 kHz, or above 3 kHz where a patch is bright already
    for (size_t patch = 0; patch < c_patchNames.size(); ++patch)
    {
        const auto rest = playMiddleC(patch, {100, 0, -1}, c_earlyStart, c_earlyLength);
        const auto pressed = playMiddleC(patch, {100, 127, -1}, c_earlyStart, c_earlyLength);
        const double above1k = energyDb(pressed, 1000.0) - energyDb(rest, 1000.0);
        const double above3k = energyDb(pressed, 3000.0) - energyDb(rest, 3000.0);
        std::printf("[ pressure ] %-14s Z 127 against 0: energy above 1 kHz %+6.2f dB, above 3 kHz %+6.2f dB\n",
                    c_patchNames[patch], above1k, above3k);
        EXPECT_GT(std::max(above1k, above3k), 3.0) << c_patchNames[patch];
    }
}

TEST(MorphexsynthPedal, EveryPatchIsLouderAndBrighterWithVelocity)
{
    for (size_t patch = 0; patch < c_patchNames.size(); ++patch)
    {
        const auto soft = playMiddleC(patch, {40, -1, -1}, c_earlyStart, c_earlyLength);
        const auto hard = playMiddleC(patch, {127, -1, -1}, c_earlyStart, c_earlyLength);
        const auto share = [&](const double fromHz)
        { return (energyDb(hard, fromHz) - energyDb(hard, 0.0)) - (energyDb(soft, fromHz) - energyDb(soft, 0.0)); };
        const double level = energyDb(hard, 0.0) - energyDb(soft, 0.0);
        std::printf("[ velocity ] %-14s velocity 127 against 40: level %+6.2f dB, share above 1 kHz %+6.2f dB, above 3 "
                    "kHz %+6.2f dB\n",
                    c_patchNames[patch], level, share(1000.0), share(3000.0));
        EXPECT_GT(level, 8.0) << c_patchNames[patch];
        EXPECT_GT(std::max(share(1000.0), share(3000.0)), 1.0) << c_patchNames[patch];
    }
}

TEST(MorphexsynthPedal, EveryPatchBendsThePitch)
{
    // the default Bend range is 12 semitones: a full bend up is an octave, which moves the spectrum up
    for (size_t patch = 0; patch < c_patchNames.size(); ++patch)
    {
        const auto plain = playMiddleC(patch, {100, -1, -1}, c_lateStart, c_lateLength);
        const auto up = playMiddleC(patch, {100, -1, 16383}, c_lateStart, c_lateLength);
        const auto down = playMiddleC(patch, {100, -1, 0}, c_lateStart, c_lateLength);
        const double centroid = spectralCentroid(plain, c_rate, 20.0, 8000.0);
        const double up2 = spectralCentroid(up, c_rate, 20.0, 8000.0) / centroid;
        const double down2 = spectralCentroid(down, c_rate, 20.0, 8000.0) / centroid;
        std::printf("[ bend     ] %-14s spectral centroid %7.1f Hz, full bend up x%.2f, down x%.2f\n",
                    c_patchNames[patch], centroid, up2, down2);
        EXPECT_GT(up2, 1.25) << c_patchNames[patch];
        EXPECT_LT(down2, 0.85) << c_patchNames[patch];
    }
}
TEST(MorphexsynthPedal, ANoteOfOnePitchSoundsTheSameWhicheverVoiceItLandsIn)
{
    // Retro Poly: the same note twice 150 ms apart (the first still sounds), after n other notes have used up n voices
    // and gone silent; the idle voices keep no phase of what they played
    for (const float releaseOffset : {0.0f, 2000.0f})
    {
        double lowest = 1e9;
        double highest = 0.0;
        for (int used = 0; used < 11; ++used)
        {
            Player player;
            player.pedal().setPatch(static_cast<float>(c_retroPoly));
            player.pedal().setRelease(releaseOffset);
            player.effectsOff();
            for (int i = 0; i < used; ++i)
            {
                player.noteOn(1, 40 + 3 * i);
                (void) player.render(1000 + 137 * static_cast<size_t>(i));
                player.noteOff(1, 40 + 3 * i);
            }
            (void) player.render(48000 * 4);
            player.noteOn(1, 60);
            (void) player.render(7200);
            player.noteOn(1, 60);
            const double level = rms(player.render(9600).left, 4800, 9600);
            lowest = std::min(lowest, level);
            highest = std::max(highest, level);
        }
        std::printf("[ voices   ] release %+5.0f ms: second note after 0 to 10 used voices, spread %.2f dB\n",
                    static_cast<double>(releaseOffset), toDb(highest / lowest));
        EXPECT_LT(toDb(highest / lowest), 0.5) << "release offset " << releaseOffset;
    }
}

}
