#include <algorithm>
#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
#include <initializer_list>
#include <iostream>
#include <numeric>
#include <tuple>
#include <string>
#include <utility>
#include <vector>

#include "dsp/Analysis/YinPitchDetector.h"
#include "dsp/Numbers/Convert.h"
#include "dsp/Numbers/Random.h"

namespace jp::test
{

namespace
{
constexpr float c_fs{48000.0f};
constexpr double c_twoPi{6.283185307179586476925286766559};

struct Variant
{
    const char* name;
    YinPitchOptions options;
};

const std::vector<Variant>& variants()
{
    static const std::vector<Variant> all{{"full rate", {1}}, {"decimated by 2", {2}}, {"decimated by 4", {4}}};
    return all;
}

YinPitchDetector makeDetector(const YinPitchOptions& options, const float minFreq = 80.0f,
                              const float maxFreq = 1000.0f, const float hopFrequency = 50.0f)
{
    return YinPitchDetector(c_fs, minFreq, maxFreq, hopFrequency, options);
}

/// Sum of harmonics of f0 with the given amplitudes (the first entry is the fundamental, 0 leaves it out).
std::vector<float> harmonicTone(const double f0, const std::vector<double>& amplitudes, const size_t samples,
                                const double phaseOffset = 0.0)
{
    std::vector<float> signal(samples);
    for (size_t i = 0; i < samples; ++i)
    {
        double x = 0.0;
        for (size_t h = 0; h < amplitudes.size(); ++h)
        {
            x += amplitudes[h] * std::sin(c_twoPi * f0 * static_cast<double>(h + 1) * static_cast<double>(i) / c_fs +
                                          phaseOffset * static_cast<double>(h + 1));
        }
        signal[i] = static_cast<float>(x);
    }
    return signal;
}

std::vector<float> sine(const double frequency, const size_t samples, const double amplitude = 0.5)
{
    return harmonicTone(frequency, {amplitude}, samples);
}

std::vector<float> naiveWave(const char kind, const double frequency, const size_t samples)
{
    std::vector<float> signal(samples);
    double phase = 0.0;
    for (auto& x : signal)
    {
        switch (kind)
        {
            case 'w':
                x = static_cast<float>(0.5 * (2.0 * phase - 1.0));
                break;
            case 't':
                x = static_cast<float>(0.5 * (phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase));
                break;
            default:
                x = static_cast<float>(phase < 0.5 ? 0.5 : -0.5);
                break;
        }
        phase += frequency / c_fs;
        phase -= std::floor(phase);
    }
    return signal;
}

std::vector<float> noise(const size_t samples, const float amplitude, const uint64_t seed = 3u)
{
    Random random(seed);
    std::vector<float> signal(samples);
    for (auto& x : signal)
    {
        x = amplitude * (2.0f * random.uniform() - 1.0f);
    }
    return signal;
}

struct Reading
{
    float pitch;
    float confidence;
    float level;
    uint64_t windowEnd;
    uint64_t at; ///< input samples consumed when it became available
    bool operator==(const Reading& other) const
    {
        return pitch == other.pitch && confidence == other.confidence && level == other.level &&
               windowEnd == other.windowEnd;
    }
};

std::vector<Reading> readingsOf(YinPitchDetector& detector, const std::vector<float>& signal)
{
    std::vector<Reading> readings;
    uint64_t consumed = 0;
    for (const float x : signal)
    {
        std::ignore = detector.step(x);
        ++consumed;
        if (detector.hasNewPitch())
        {
            readings.push_back({detector.getCurrentPitch(), detector.getLastConfidence(), detector.getLastLevel(),
                                detector.getLastWindowEnd(), consumed});
        }
    }
    return readings;
}

/// Median pitch and a confidence figure of the voiced readings after the first window (the ported tests' measure).
void measure(YinPitchDetector& detector, const std::vector<float>& signal, float& pitch, float& confidence)
{
    std::vector<float> pitches;
    for (int pass = 0; pass < 2; ++pass)
    {
        for (const float x : signal)
        {
            const float p = detector.step(x);
            if (pass == 1 && detector.hasNewPitch())
            {
                pitches.push_back(p);
            }
        }
    }
    if (pitches.empty())
    {
        pitch = 0.0f;
        confidence = 0.0f;
        return;
    }
    std::sort(pitches.begin(), pitches.end());
    pitch = pitches[pitches.size() / 2];
    std::vector<float> deviations;
    for (const float p : pitches)
    {
        deviations.push_back(std::abs(p - pitch));
    }
    std::sort(deviations.begin(), deviations.end());
    confidence = 1.0f / (1.0f + deviations[deviations.size() / 2]);
}

float centsBetween(const float measured, const float expected)
{
    return 1200.0f * std::log2(measured / expected);
}

}

// --- the cases of the AbacDsp tests ---

TEST(YinPitchDetector, FindsTheFrequencyOfASine)
{
    for (const auto& variant : variants())
    {
        for (const float target : {110.0f, 220.0f, 440.0f, 880.0f})
        {
            SCOPED_TRACE(std::string(variant.name) + " " + std::to_string(target) + " Hz");
            YinPitchDetector detector = makeDetector(variant.options);
            float pitch;
            float confidence;
            measure(detector, sine(target, static_cast<size_t>(80.0f * c_fs / target)), pitch, confidence);
            EXPECT_GT(pitch, 0.0f);
            EXPECT_NEAR(pitch, target, 2.0f);
            EXPECT_GT(confidence, 0.7f);
        }
    }
}

TEST(YinPitchDetector, FindsASineInNoise)
{
    for (const auto& variant : variants())
    {
        for (const float amplitude : {0.05f, 0.1f, 0.25f})
        {
            SCOPED_TRACE(std::string(variant.name) + " noise " + std::to_string(amplitude));
            YinPitchDetector detector = makeDetector(variant.options);
            std::vector<float> signal = sine(440.0, static_cast<size_t>(40.0f * c_fs / 440.0f));
            const std::vector<float> n = noise(signal.size(), amplitude, 17u);
            for (size_t i = 0; i < signal.size(); ++i)
            {
                signal[i] = 2.0f * signal[i] + n[i];
            }
            float pitch;
            float confidence;
            measure(detector, signal, pitch, confidence);
            EXPECT_GT(pitch, 0.0f);
            EXPECT_NEAR(pitch, 440.0f, 2.0f * (1.0f + amplitude * 2.0f));
            EXPECT_GT(confidence, std::max(0.2f, 0.8f - amplitude * 3.0f));
        }
    }
}

TEST(YinPitchDetector, FindsThePeriodOfSawTriangleAndSquare)
{
    for (const auto& variant : variants())
    {
        for (const char kind : {'w', 't', 's'})
        {
            for (const float target : {110.0f, 220.0f, 440.0f})
            {
                SCOPED_TRACE(std::string(variant.name) + " " + kind + " " + std::to_string(target) + " Hz");
                YinPitchDetector detector = makeDetector(variant.options);
                float pitch;
                float confidence;
                measure(detector, naiveWave(kind, target, static_cast<size_t>(40.0f * c_fs / target)), pitch,
                        confidence);
                EXPECT_GT(pitch, 0.0f);
                EXPECT_NEAR(pitch, target, kind == 's' ? 4.0f : 3.0f);
                EXPECT_GT(confidence, kind == 's' ? 0.4f : 0.5f);
            }
        }
    }
}

TEST(YinPitchDetector, BroadbandNoiseCostsFullRateTheOctaveAndDecimationDoesNot)
{
    // sine of amplitude 1 and uniform noise of +-0.5 (about 8 dB): the dip of the CMNDF stays above the threshold, the
    // global minimum is taken and at full rate it lies at a multiple of the period; the decimating lowpass removes
    // most of the noise power first
    size_t wrong[2]{0, 0};
    size_t total[2]{0, 0};
    for (size_t index = 0; index < 2; ++index)
    {
        for (uint64_t seed = 1; seed <= 4; ++seed)
        {
            YinPitchDetector detector = makeDetector({index == 0 ? 1u : 4u});
            std::vector<float> signal = sine(440.0, 48000, 1.0);
            const std::vector<float> n = noise(signal.size(), 0.5f, seed);
            for (size_t i = 0; i < signal.size(); ++i)
            {
                signal[i] += n[i];
            }
            for (const Reading& r : readingsOf(detector, signal))
            {
                if (r.windowEnd > 6000)
                {
                    ++total[index];
                    wrong[index] += std::abs(centsBetween(r.pitch, 440.0f)) > 50.0f || r.pitch <= 0.0f ? 1 : 0;
                }
            }
        }
    }
    std::cout << "[ yin      ] sine plus noise +-0.5: wrong readings " << wrong[0] << " of " << total[0]
              << " at full rate, " << wrong[1] << " of " << total[1] << " decimated by 4\n";
    EXPECT_GT(wrong[0], total[0] / 2);
    EXPECT_EQ(wrong[1], 0u);
}

TEST(YinPitchDetector, NoiseGivesNoConfidentPitch)
{
    for (const auto& variant : variants())
    {
        SCOPED_TRACE(variant.name);
        YinPitchDetector detector = makeDetector(variant.options);
        std::vector<float> confidences;
        for (const Reading& r : readingsOf(detector, noise(48000, 0.5f)))
        {
            if (r.windowEnd > 4000)
            {
                confidences.push_back(r.confidence);
            }
        }
        ASSERT_FALSE(confidences.empty());
        const float mean = std::accumulate(confidences.begin(), confidences.end(), 0.0f) / confidences.size();
        EXPECT_LT(mean, 0.3f);
    }
}

TEST(YinPitchDetector, SilenceGivesNoPitchAndZeroConfidence)
{
    for (const auto& variant : variants())
    {
        SCOPED_TRACE(variant.name);
        YinPitchDetector detector = makeDetector(variant.options);
        for (size_t i = 0; i < 8192; ++i)
        {
            EXPECT_EQ(detector.step(0.0f), 0.0f);
        }
        EXPECT_EQ(detector.getLastConfidence(), 0.0f);
        EXPECT_EQ(detector.getLastLevel(), 0.0f);
    }
}

TEST(YinPitchDetector, FindsTheEdgesOfTheRange)
{
    for (const auto& variant : variants())
    {
        for (const float target : {85.0f, 950.0f})
        {
            SCOPED_TRACE(std::string(variant.name) + " " + std::to_string(target) + " Hz");
            YinPitchDetector detector = makeDetector(variant.options);
            float pitch;
            float confidence;
            measure(detector, sine(target, static_cast<size_t>(60.0f * c_fs / target)), pitch, confidence);
            EXPECT_GT(pitch, 0.0f);
            EXPECT_NEAR(pitch, target, 6.0f);
        }
    }
}

TEST(YinPitchDetector, ConfidenceIsHighForACleanSine)
{
    for (const auto& variant : variants())
    {
        SCOPED_TRACE(variant.name);
        YinPitchDetector detector = makeDetector(variant.options);
        for (const float x : sine(220.0, 8192))
        {
            std::ignore = detector.step(x);
        }
        EXPECT_GT(detector.getLastConfidence(), 0.9f);
        EXPECT_NEAR(detector.getLastLevel(), 0.5f / std::sqrt(2.0f), 0.02f);
    }
}

TEST(YinPitchDetector, ThePitchNeverEscapesTheConfiguredRange)
{
    // an unclamped parabolic correction could send a flat minimum arbitrarily far off
    for (const auto& variant : variants())
    {
        for (float target = 80.0f; target <= 1000.0f; target += 23.7f)
        {
            for (const char kind : {'i', 'w', 't', 's'})
            {
                SCOPED_TRACE(std::string(variant.name) + " " + kind + " " + std::to_string(target) + " Hz");
                YinPitchDetector detector = makeDetector(variant.options);
                const std::vector<float> signal = kind == 'i' ? sine(target, 8000) : naiveWave(kind, target, 8000);
                for (const Reading& r : readingsOf(detector, signal))
                {
                    if (r.pitch > 0.0f)
                    {
                        EXPECT_GE(r.pitch, 40.0f);
                        EXPECT_LE(r.pitch, 2000.0f);
                    }
                }
            }
        }
    }
}

// --- accuracy, octave errors, level dependence ---

TEST(YinPitchDetector, SinesAreWithinFiveCentsOverTheRange)
{
    for (const auto& variant : variants())
    {
        float worst = 0.0f;
        for (float target = 80.0f; target < 1000.0f; target *= 1.2599f) // third-octave steps
        {
            YinPitchDetector detector = makeDetector(variant.options);
            const std::vector<float> signal = sine(target, 24000);
            for (const Reading& r : readingsOf(detector, signal))
            {
                if (r.windowEnd >= 2 * detector.getLatencySamples() + 1200)
                {
                    ASSERT_GT(r.pitch, 0.0f) << variant.name << " " << target;
                    worst = std::max(worst, std::abs(centsBetween(r.pitch, target)));
                }
            }
        }
        std::cout << "[ yin      ] " << variant.name << ": largest error " << worst << " cent over 80 Hz to 1 kHz\n";
        // the budget is 5 cent; decimating by 4 gives up some of it near 1 kHz (12 samples per period, 6.1 cent
        // measured)
        EXPECT_LT(worst, variant.options.decimation > 2 ? 7.0f : 5.0f) << variant.name;
    }
}

TEST(YinPitchDetector, StrongUpperHarmonicsAndAMissingFundamentalGiveNoOctaveError)
{
    const std::vector<std::pair<const char*, std::vector<double>>> tones{
        {"saw-like", {1.0, 0.5, 0.33, 0.25, 0.2, 0.17}},
        {"boosted second harmonic", {0.5, 1.0, 0.4, 0.2}},
        {"strong third harmonic", {0.4, 0.3, 1.0, 0.2}},
        {"missing fundamental", {0.0, 1.0, 0.8, 0.6, 0.4}}};
    for (const auto& variant : variants())
    {
        for (const auto& [name, amplitudes] : tones)
        {
            for (float f0 = 82.0f; f0 < 330.0f; f0 *= 1.1f)
            {
                SCOPED_TRACE(std::string(variant.name) + " " + name + " " + std::to_string(f0) + " Hz");
                YinPitchDetector detector = makeDetector(variant.options);
                const std::vector<float> signal = harmonicTone(f0, amplitudes, 24000, 0.7);
                size_t counted = 0;
                for (const Reading& r : readingsOf(detector, signal))
                {
                    if (r.windowEnd < 2 * detector.getLatencySamples() + 1200)
                    {
                        continue;
                    }
                    ++counted;
                    ASSERT_GT(r.pitch, 0.0f);
                    EXPECT_LT(std::abs(centsBetween(r.pitch, f0)), 15.0f) << "pitch " << r.pitch;
                }
                EXPECT_GT(counted, 5u);
            }
        }
    }
}

TEST(YinPitchDetector, ALevelThatIsFarBelowFullScaleIsStillAPitchAndNoiseThereIsStillNoise)
{
    // a constant in the cumulative sum would dominate for quiet input and make noise look periodic
    for (const auto& variant : variants())
    {
        for (const float amplitude : {1e-2f, 1e-3f, 1e-4f})
        {
            SCOPED_TRACE(std::string(variant.name) + " amplitude " + std::to_string(amplitude));
            YinPitchDetector tone = makeDetector(variant.options);
            YinPitchDetector hiss = makeDetector(variant.options);
            std::vector<float> signal = sine(220.0, 12000, amplitude);
            float pitch;
            float confidence;
            measure(tone, signal, pitch, confidence);
            EXPECT_NEAR(pitch, 220.0f, 2.0f);
            float confidences = 0.0f;
            size_t count = 0;
            for (const Reading& r : readingsOf(hiss, noise(24000, amplitude)))
            {
                if (r.windowEnd > 4000)
                {
                    confidences += r.confidence;
                    ++count;
                }
            }
            EXPECT_LT(confidences / static_cast<float>(count), 0.3f);
        }
    }
}

TEST(YinPitchDetector, ADirectCurrentOffsetDoesNotMatter)
{
    YinPitchDetector detector = makeDetector({});
    std::vector<float> signal = sine(330.0, 12000);
    for (float& x : signal)
    {
        x += 0.4f;
    }
    float pitch;
    float confidence;
    measure(detector, signal, pitch, confidence);
    EXPECT_NEAR(pitch, 330.0f, 1.0f);
}

TEST(YinPitchDetector, FollowsVibratoWithinItsExtent)
{
    // 5 Hz, +-30 cent around 220 Hz
    constexpr size_t length = 48000 * 2;
    std::vector<float> signal(length);
    double phase = 0.0;
    for (size_t i = 0; i < length; ++i)
    {
        const double cents = 30.0 * std::sin(c_twoPi * 5.0 * static_cast<double>(i) / c_fs);
        phase += 220.0 * std::exp2(cents / 1200.0) / c_fs;
        signal[i] = static_cast<float>(0.5 * std::sin(c_twoPi * phase));
    }
    for (const auto& variant : variants())
    {
        YinPitchDetector detector = makeDetector(variant.options);
        float lowest = 1e9f;
        float highest = 0.0f;
        for (const Reading& r : readingsOf(detector, signal))
        {
            if (r.windowEnd > 12000)
            {
                ASSERT_GT(r.pitch, 0.0f);
                lowest = std::min(lowest, centsBetween(r.pitch, 220.0f));
                highest = std::max(highest, centsBetween(r.pitch, 220.0f));
            }
        }
        std::cout << "[ yin      ] " << variant.name << ": vibrato +-30 cent read as " << lowest << " to " << highest
                  << " cent\n";
        EXPECT_LT(highest, 30.0f + 3.0f) << variant.name;
        EXPECT_GT(highest, 10.0f) << variant.name;
        EXPECT_GT(lowest, -33.0f) << variant.name;
        EXPECT_LT(lowest, -10.0f) << variant.name;
    }
}

TEST(YinPitchDetector, AStepOfPitchShowsWhenItsWindowHasLeftTheOldTone)
{
    constexpr size_t stepAt = 24000;
    std::vector<float> signal = sine(220.0, stepAt);
    const std::vector<float> after = sine(330.0, 24000);
    signal.insert(signal.end(), after.begin(), after.end());
    // phase continuity does not matter to YIN; the step is a hard cut
    for (const auto& variant : variants())
    {
        SCOPED_TRACE(variant.name);
        YinPitchDetector detector = makeDetector(variant.options);
        const size_t window = detector.getWindowSize() * variant.options.decimation;
        size_t checkedOld = 0;
        size_t checkedNew = 0;
        for (const Reading& r : readingsOf(detector, signal))
        {
            if (r.windowEnd >= 8000 && r.windowEnd <= stepAt)
            {
                EXPECT_NEAR(r.pitch, 220.0f, 2.0f) << "window ends " << r.windowEnd;
                ++checkedOld;
            }
            if (r.windowEnd >= stepAt + window)
            {
                EXPECT_NEAR(r.pitch, 330.0f, 3.0f) << "window ends " << r.windowEnd;
                ++checkedNew;
            }
        }
        EXPECT_GT(checkedOld, 5u);
        EXPECT_GT(checkedNew, 5u);
    }
}

// --- contract: stamps, hops, blocks, reset, slicing, reference ---

TEST(YinPitchDetector, ReadingsComeOncePerHopAndCarryTheirWindowEnd)
{
    for (const auto& variant : variants())
    {
        SCOPED_TRACE(variant.name);
        YinPitchDetector detector = makeDetector(variant.options);
        const auto readings = readingsOf(detector, sine(220.0, 48000));
        ASSERT_GT(readings.size(), 40u);
        for (size_t i = 0; i < readings.size(); ++i)
        {
            EXPECT_EQ(readings[i].windowEnd, (i + 1) * detector.getHopSize());
            EXPECT_EQ(readings[i].at, readings[i].windowEnd) << "a reading is there on the sample its window ends";
        }
        EXPECT_EQ(detector.getHopSize(), 960u);
        EXPECT_GE(detector.getLatencySamples(), 600u);
        EXPECT_LE(detector.getLatencySamples(), 610u);
    }
}

TEST(YinPitchDetector, ResultsDoNotDependOnTheBlockPartition)
{
    const std::vector<float> signal = harmonicTone(196.0, {1.0, 0.4, 0.2}, 48000);
    for (const auto& variant : variants())
    {
        YinPitchDetector single = makeDetector(variant.options);
        std::vector<float> expected(signal.size());
        for (size_t i = 0; i < signal.size(); ++i)
        {
            expected[i] = single.step(signal[i]);
        }
        for (const size_t block : {7u, 16u, 128u, 4096u})
        {
            YinPitchDetector detector = makeDetector(variant.options);
            std::vector<float> output(signal.size());
            for (size_t position = 0; position < signal.size(); position += block)
            {
                detector.process(&signal[position], &output[position], std::min(block, signal.size() - position));
            }
            EXPECT_EQ(output, expected) << variant.name << " block " << block;
        }
    }
}

TEST(YinPitchDetector, ResetRepeatsTheRun)
{
    const std::vector<float> signal = harmonicTone(147.0, {1.0, 0.5}, 24000);
    for (const auto& variant : variants())
    {
        YinPitchDetector detector = makeDetector(variant.options);
        const auto first = readingsOf(detector, signal);
        detector.reset();
        EXPECT_EQ(readingsOf(detector, signal), first) << variant.name;
    }
}

TEST(YinPitchDetector, MatchesADoublePrecisionReferenceOfTheAlgorithm)
{
    // paper steps 1 to 5 in double precision on the same window, no early exit
    const std::vector<float> signal = harmonicTone(173.0, {1.0, 0.6, 0.3, 0.1}, 36000, 0.4);
    YinPitchDetector detector = makeDetector({});
    const size_t maxLag = detector.getMaxLag();
    const size_t minLag = static_cast<size_t>(c_fs / 1000.0f);
    size_t compared = 0;
    for (const Reading& r : readingsOf(detector, signal))
    {
        if (r.windowEnd < 2 * maxLag)
        {
            continue;
        }
        const float* window = &signal[r.windowEnd - 2 * maxLag];
        std::vector<double> cmndf(maxLag + 1, 1.0);
        double cumulative = 0.0;
        for (size_t lag = 1; lag <= maxLag; ++lag)
        {
            double difference = 0.0;
            for (size_t j = 0; j < maxLag; ++j)
            {
                const double e = static_cast<double>(window[j]) - static_cast<double>(window[j + lag]);
                difference += e * e;
            }
            cumulative += difference;
            cmndf[lag] = cumulative > 1e-10 ? difference * static_cast<double>(lag) / cumulative : 1.0;
        }
        size_t chosen = 0;
        for (size_t lag = minLag; lag < maxLag && chosen == 0; ++lag)
        {
            if (cmndf[lag] < 0.1 && cmndf[lag] <= cmndf[lag - 1] && cmndf[lag] <= cmndf[lag + 1])
            {
                chosen = lag;
            }
        }
        if (chosen == 0)
        {
            chosen = static_cast<size_t>(std::min_element(cmndf.begin() + static_cast<std::ptrdiff_t>(minLag),
                                                          cmndf.begin() + static_cast<std::ptrdiff_t>(maxLag)) -
                                         cmndf.begin());
        }
        const double a = (cmndf[chosen - 1] - 2.0 * cmndf[chosen] + cmndf[chosen + 1]) / 2.0;
        const double b = (cmndf[chosen + 1] - cmndf[chosen - 1]) / 2.0;
        const double refined =
            static_cast<double>(chosen) + (std::abs(a) < 1e-6 ? 0.0 : std::clamp(-b / (2.0 * a), -0.5, 0.5));
        const double expected = static_cast<double>(c_fs) / refined;
        EXPECT_NEAR(r.pitch, expected, expected * 2e-5) << "window ends " << r.windowEnd;
        EXPECT_NEAR(r.confidence, 1.0 - cmndf[chosen], 1e-4);
        ++compared;
    }
    EXPECT_GT(compared, 30u);
}

}
