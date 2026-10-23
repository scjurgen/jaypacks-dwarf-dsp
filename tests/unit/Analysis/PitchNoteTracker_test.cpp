#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
#include <optional>
#include <vector>

#include "dsp/Analysis/PitchNoteTracker.h"
#include "dsp/Numbers/Convert.h"

namespace jp::test
{

namespace
{
constexpr float c_level{0.1f};
constexpr float c_confidence{0.95f};

/// A voiced reading at a fractional MIDI note.
PitchObservation voiced(const float pitch, const float level = c_level, const float confidence = c_confidence,
                        const uint64_t stamp = 0)
{
    return {noteToFrequency(pitch), confidence, level, stamp};
}

PitchObservation unvoiced(const uint64_t stamp = 0)
{
    return {0.0f, 0.0f, 0.0f, stamp};
}

/// Feeds readings and collects the events with the index of the reading that completed each.
struct Feed
{
    PitchNoteTracker tracker;
    std::vector<std::pair<size_t, PitchNote>> events;
    size_t count{0};

    explicit Feed(const PitchNoteTrackerSettings& settings = {})
        : tracker(settings)
    {
    }

    void feed(const PitchObservation& reading, const size_t times = 1)
    {
        for (size_t i = 0; i < times; ++i)
        {
            if (const auto event = tracker.update(reading))
            {
                events.emplace_back(count, *event);
            }
            ++count;
        }
    }
};
}

TEST(PitchNoteTracker, ANoteNeedsStableHopsAndComesWithTheMeanPitch)
{
    Feed run;
    run.feed(voiced(69.0f), 2);
    EXPECT_TRUE(run.events.empty());
    run.feed(voiced(69.2f));
    ASSERT_EQ(run.events.size(), 1u);
    const PitchNote& note = run.events.front().second;
    EXPECT_EQ(run.events.front().first, 2u);
    EXPECT_EQ(note.note, 69);
    EXPECT_FALSE(note.released);
    EXPECT_NEAR(note.pitch, 69.0f + 0.2f / 3.0f, 1e-4f);
    EXPECT_NEAR(note.hz, noteToFrequency(note.pitch), 1e-3f);
    EXPECT_TRUE(run.tracker.isPlaying());
    EXPECT_EQ(run.tracker.getNote(), 69);
}

TEST(PitchNoteTracker, SilenceAndUnvoicedReadingsNeverMakeANote)
{
    Feed run;
    run.feed(unvoiced(), 100);
    run.feed({0.0f, 1.0f, 1.0f, 0}, 10);  // a zero frequency is unvoiced however confident
    run.feed({-5.0f, 1.0f, 1.0f, 0}, 10); // so is a negative one
    EXPECT_TRUE(run.events.empty());
    EXPECT_FALSE(run.tracker.isPlaying());
}

TEST(PitchNoteTracker, LevelAndConfidenceGateTheReadings)
{
    PitchNoteTrackerSettings settings;
    settings.minLevelDb = -40.0f;
    settings.minConfidence = 0.8f;
    {
        Feed run(settings);
        run.feed(voiced(60.0f, dbToGain(-41.0f)), 20);
        EXPECT_TRUE(run.events.empty()) << "below the level threshold";
    }
    {
        Feed run(settings);
        run.feed(voiced(60.0f, dbToGain(-39.0f)), 3);
        EXPECT_EQ(run.events.size(), 1u) << "above the level threshold";
    }
    {
        Feed run(settings);
        run.feed(voiced(60.0f, c_level, 0.79f), 20);
        EXPECT_TRUE(run.events.empty()) << "below the confidence threshold";
    }
    {
        Feed run(settings);
        run.feed(voiced(60.0f, c_level, 0.81f), 3);
        EXPECT_EQ(run.events.size(), 1u) << "above the confidence threshold";
    }
}

TEST(PitchNoteTracker, ReadingsThatSpreadBeyondTheToleranceNeverMakeANote)
{
    Feed run;
    for (size_t i = 0; i < 20; ++i)
    {
        run.feed(voiced(i % 2 == 0 ? 60.0f : 61.0f)); // 100 cent apart
    }
    EXPECT_TRUE(run.events.empty());
    // the same two pitches 40 cent apart do make a note
    Feed close;
    for (size_t i = 0; i < 6; ++i)
    {
        close.feed(voiced(i % 2 == 0 ? 60.0f : 60.4f));
    }
    EXPECT_EQ(close.events.size(), 1u);
}

TEST(PitchNoteTracker, AnUnvoicedReadingBreaksTheStabilityCount)
{
    Feed run;
    run.feed(voiced(64.0f), 2);
    run.feed(unvoiced());
    run.feed(voiced(64.0f), 2);
    EXPECT_TRUE(run.events.empty());
    run.feed(voiced(64.0f));
    EXPECT_EQ(run.events.size(), 1u);
}

TEST(PitchNoteTracker, VibratoAndAPitchBetweenTwoNotesDoNotFlicker)
{
    Feed run;
    run.feed(voiced(69.0f), 3);
    ASSERT_EQ(run.events.size(), 1u);
    // vibrato of 30 cent and a pitch that wanders to 0.6 semitone: inside the hysteresis band
    for (size_t i = 0; i < 200; ++i)
    {
        run.feed(voiced(69.0f + 0.3f * std::sin(0.4f * static_cast<float>(i))));
    }
    run.feed(voiced(69.6f), 10);
    run.feed(voiced(68.4f), 10);
    EXPECT_EQ(run.events.size(), 1u);
    // alternating just outside and inside the band never builds a candidate
    for (size_t i = 0; i < 20; ++i)
    {
        run.feed(voiced(i % 2 == 0 ? 69.7f : 69.3f));
    }
    EXPECT_EQ(run.events.size(), 1u);
}

TEST(PitchNoteTracker, ASustainedChangeIsANewNoteAfterTheStableHops)
{
    Feed run;
    run.feed(voiced(69.0f), 3);
    run.feed(voiced(70.0f), 2);
    EXPECT_EQ(run.events.size(), 1u);
    run.feed(voiced(70.0f));
    ASSERT_EQ(run.events.size(), 2u);
    EXPECT_EQ(run.events.back().second.note, 70);
    EXPECT_FALSE(run.events.back().second.released);
    EXPECT_EQ(run.tracker.getNote(), 70);
}

TEST(PitchNoteTracker, AStrayOctaveReadingIsRejected)
{
    Feed run;
    run.feed(voiced(57.0f), 3);
    for (size_t i = 0; i < 10; ++i)
    {
        run.feed(voiced(57.0f), 2);
        run.feed(voiced(69.0f), 2); // fewer octave readings in a row than the stable hops
    }
    run.feed(voiced(57.0f), 5);
    EXPECT_EQ(run.events.size(), 1u);
    EXPECT_EQ(run.tracker.getNote(), 57);
}

TEST(PitchNoteTracker, ANoteEndsAfterTheReleaseHopsAndOnlyOnce)
{
    Feed run;
    run.feed(voiced(69.0f), 3);
    run.feed(unvoiced(), 2);
    EXPECT_EQ(run.events.size(), 1u) << "two unvoiced readings do not end a note";
    EXPECT_TRUE(run.tracker.isPlaying());
    run.feed(voiced(69.0f));
    run.feed(unvoiced(), 2);
    run.feed(voiced(69.0f));
    EXPECT_EQ(run.events.size(), 1u) << "and they do not make the same note start again";
    run.feed(unvoiced(), 3);
    ASSERT_EQ(run.events.size(), 2u);
    EXPECT_TRUE(run.events.back().second.released);
    EXPECT_EQ(run.events.back().second.note, 69);
    EXPECT_FALSE(run.tracker.isPlaying());
    run.feed(unvoiced(), 50);
    EXPECT_EQ(run.events.size(), 2u);
}

TEST(PitchNoteTracker, TheSameNoteAgainNeedsAReleaseOrAnAttack)
{
    Feed run;
    run.feed(voiced(69.0f), 3);
    run.feed(voiced(69.0f), 50);
    EXPECT_EQ(run.events.size(), 1u) << "holding a note is one event";
    // after a release the same pitch is a new note
    run.feed(unvoiced(), 3);
    run.feed(voiced(69.0f), 3);
    ASSERT_EQ(run.events.size(), 3u);
    EXPECT_EQ(run.events.back().second.note, 69);
    EXPECT_FALSE(run.events.back().second.released);
}

TEST(PitchNoteTracker, AnAttackWhileTheNotePlaysStrikesItAgainButADecayDoesNot)
{
    Feed run;
    run.feed(voiced(69.0f, 0.2f), 3);
    // a decay by 20 dB, then a slow swell of 4 dB: no strike
    for (size_t i = 0; i < 40; ++i)
    {
        run.feed(voiced(69.0f, 0.2f * std::pow(0.95f, static_cast<float>(i))));
    }
    const float lowest = 0.2f * std::pow(0.95f, 39.0f);
    run.feed(voiced(69.0f, lowest * 1.5f), 3);
    EXPECT_EQ(run.events.size(), 1u);
    // a rise of 6 dB over the lowest level is a strike, with the same note and no release in between
    run.feed(voiced(69.0f, lowest * 2.05f));
    ASSERT_EQ(run.events.size(), 2u);
    EXPECT_EQ(run.events.back().second.note, 69);
    EXPECT_FALSE(run.events.back().second.released);
    EXPECT_NEAR(run.events.back().second.level, lowest * 2.05f, 1e-6f);
    // the level to beat is now the strike's own
    run.feed(voiced(69.0f, lowest * 2.05f * 1.5f), 3);
    EXPECT_EQ(run.events.size(), 2u);
}

TEST(PitchNoteTracker, ADecayingNoteIsOneNoteAndOneRelease)
{
    Feed run;
    float level = 0.3f;
    float confidence = 0.98f;
    for (size_t i = 0; i < 120; ++i)
    {
        run.feed(voiced(52.0f, level, confidence));
        level *= 0.96f;
        confidence = std::max(0.3f, confidence - 0.006f);
    }
    ASSERT_EQ(run.events.size(), 2u);
    EXPECT_FALSE(run.events[0].second.released);
    EXPECT_TRUE(run.events[1].second.released);
    EXPECT_EQ(run.events[1].second.note, 52);
}

TEST(PitchNoteTracker, TheStampOfTheCompletingReadingIsCarried)
{
    Feed run;
    run.feed(voiced(60.0f, c_level, c_confidence, 1000));
    run.feed(voiced(60.0f, c_level, c_confidence, 1960));
    run.feed(voiced(60.0f, c_level, c_confidence, 2920));
    ASSERT_EQ(run.events.size(), 1u);
    EXPECT_EQ(run.events.front().second.stamp, 2920u);
    run.feed(unvoiced(3880), 3);
    ASSERT_EQ(run.events.size(), 2u);
    EXPECT_EQ(run.events.back().second.stamp, 3880u) << "a release carries the stamp of its own reading";
}

TEST(PitchNoteTracker, ResetForgetsTheNoteAndTheCandidate)
{
    Feed run;
    run.feed(voiced(60.0f), 3);
    run.feed(voiced(65.0f), 2);
    run.tracker.reset();
    EXPECT_FALSE(run.tracker.isPlaying());
    run.feed(voiced(65.0f));
    EXPECT_EQ(run.events.size(), 1u) << "the candidate was dropped";
    run.feed(voiced(65.0f), 2);
    EXPECT_EQ(run.events.size(), 2u);
}

}
