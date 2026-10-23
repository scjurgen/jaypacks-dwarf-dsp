#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <iostream>
#include <limits>
#include <tuple>
#include <vector>

#include "dsp/Analysis/ZeroCrossingFrequency.h"
#include "dsp/Numbers/Random.h"
#include "dsp/Synthesizer/MorphexsynthVoice.h"
#include "tests/unit/TestHelpers.h"

namespace jp::test
{

namespace
{
constexpr float c_rate{48000.0f};

struct Rig
{
    WaveTableBank bank{BasicWave::Triangle, BasicWave::SharkFin, BasicWave::Saw, BasicWave::Square, BasicWave::White};
    WaveShaperTableStore shaper;
};

const Rig& rig()
{
    static const Rig sharedRig;
    return sharedRig;
}

MorphexsynthVoice makeVoice(const uint64_t seed = 1u)
{
    return MorphexsynthVoice(c_rate, rig().bank, rig().shaper, seed);
}

/// One triangle oscillator at full level, the filter wide open, an instant attack, sustain 1: the voice as a
/// plain tone, to be changed by the test.
void tonePatch(MorphexsynthVoice& voice)
{
    voice.setWaveForm(0, 0);
    voice.setLevelOscillator(0, 1.0f);
    voice.setLevelOscillator(1, 0.0f);
    voice.setLevelOscillator(2, 0.0f);
    voice.setCutoff(127.0f);
    voice.setResonance(0.0f);
    voice.setEnvelopeAttack(0.0f);
    voice.setEnvelopeDecay(0.0f);
    voice.setEnvelopeSustainLevel(1.0f);
    voice.setEnvelopeRelease(50.0f);
}

struct Stereo
{
    std::vector<float> left;
    std::vector<float> right;
};

Stereo render(MorphexsynthVoice& voice, const size_t length, const size_t blockSize = 128)
{
    Stereo out{std::vector<float>(length), std::vector<float>(length)};
    for (size_t start = 0; start < length; start += blockSize)
    {
        const size_t n = std::min(blockSize, length - start);
        voice.processBlock(out.left.data() + start, out.right.data() + start, n);
    }
    return out;
}

double rms(const std::vector<float>& signal, const size_t from = 0, const size_t to = 0)
{
    const size_t end = to == 0 ? signal.size() : to;
    double sum = 0.0;
    for (size_t i = from; i < end; ++i)
    {
        sum += static_cast<double>(signal[i]) * static_cast<double>(signal[i]);
    }
    return std::sqrt(sum / static_cast<double>(end - from));
}

double frequencyOf(const std::vector<float>& signal, const size_t from, const size_t to)
{
    ZeroCrossingFrequency crossings(c_rate, 0.02f);
    for (size_t i = from; i < to; ++i)
    {
        crossings.add(signal[i]);
    }
    return crossings.frequency();
}

double cents(const double measured, const double expected)
{
    return 1200.0 * std::log2(measured / expected);
}

/// Energy above hz relative to the total, in dB, of the last 16384 samples (Hann window).
double highEnergyDb(const std::vector<float>& signal, const double hz)
{
    const std::vector<float> part(signal.end() - 16384, signal.end());
    return highBandEnergyDb(part, hz, c_rate);
}

/// Energy between two frequencies of the last 16384 samples (Hann window), in dB on an arbitrary scale: only
/// differences between voices are meaningful.
double bandDb(const std::vector<float>& signal, const double lowHz, const double highHz)
{
    const std::vector<float> part(signal.end() - 16384, signal.end());
    const auto spectrum = hannMagnitudeSpectrum(part);
    const double binHz = static_cast<double>(c_rate) / 16384.0;
    double sum = 0.0;
    for (size_t bin = static_cast<size_t>(lowHz / binHz);
         bin < spectrum.size() && static_cast<double>(bin) * binHz < highHz; ++bin)
    {
        sum += spectrum[bin] * spectrum[bin];
    }
    return 10.0 * std::log10(sum + 1e-30);
}

/// The largest value in a window of the signal, a coarse envelope.
float peakIn(const std::vector<float>& signal, const size_t from, const size_t to)
{
    float peak = 0.0f;
    for (size_t i = from; i < to; ++i)
    {
        peak = std::max(peak, std::abs(signal[i]));
    }
    return peak;
}
}

TEST(MorphexsynthVoice, IsSilentAndNotPlayingBeforeATrigger)
{
    auto voice = makeVoice();
    tonePatch(voice);
    EXPECT_FALSE(voice.isPlaying());
    const auto out = render(voice, 2000);
    EXPECT_EQ(peakIn(out.left, 0, out.left.size()), 0.0f);
    EXPECT_EQ(peakIn(out.right, 0, out.right.size()), 0.0f);
}

TEST(MorphexsynthVoice, ATriggerStartsTheVoiceAndTheReleaseEndsIt)
{
    auto voice = makeVoice();
    tonePatch(voice);
    voice.triggerVoice(60, 100, 60);
    EXPECT_TRUE(voice.isPlaying());
    std::ignore = render(voice, 4800);
    EXPECT_TRUE(voice.isPlaying()) << "sustained";
    voice.stopVoice();
    std::ignore = render(voice, static_cast<size_t>(0.05f * c_rate) + 200);
    EXPECT_FALSE(voice.isPlaying()) << "after the 50 ms release";
    const auto tail = render(voice, 1000);
    EXPECT_EQ(peakIn(tail.left, 0, tail.left.size()), 0.0f) << "silent again";
}

TEST(MorphexsynthVoice, TheFirstWindowAfterATriggerIsSilentBecauseTheOscillatorsRunOneWindowAhead)
{
    auto voice = makeVoice();
    tonePatch(voice);
    voice.triggerVoice(60, 127, 60);
    const auto out = render(voice, 64);
    EXPECT_LT(peakIn(out.left, 0, 16), 1e-4f);
    EXPECT_GT(peakIn(out.left, 16, 64), 0.05f);
}

TEST(MorphexsynthVoice, TheNoteSetsThePitchExactly)
{
    double worst = 0.0;
    for (const int note : {24, 36, 48, 60, 69, 72, 84, 96})
    {
        auto voice = makeVoice();
        tonePatch(voice);
        voice.triggerVoice(note, 100, note);
        const auto out = render(voice, 48000 * 3);
        const double expected = static_cast<double>(noteToFrequency(static_cast<float>(note)));
        const double error = cents(frequencyOf(out.left, 24000, out.left.size()), expected);
        worst = std::max(worst, std::abs(error));
        EXPECT_NEAR(error, 0.0, 0.05) << "note " << note;
    }
    std::cout << "[ pitch    ] worst error " << worst << " cent, notes 24 to 96\n";
}

TEST(MorphexsynthVoice, PitchFactorAndDetuneShiftAnOscillator)
{
    auto voice = makeVoice();
    tonePatch(voice);
    voice.setPitchFactor(0, 2.0f);
    voice.triggerVoice(57, 100, 57);
    auto out = render(voice, 48000 * 2);
    EXPECT_NEAR(cents(frequencyOf(out.left, 12000, out.left.size()), 440.0), 0.0, 0.1) << "an octave above 220 Hz";
    voice.setDetune(0, 0.5f);
    out = render(voice, 48000 * 2);
    EXPECT_NEAR(cents(frequencyOf(out.left, 12000, out.left.size()), 440.0), 50.0, 0.5) << "half a semitone up";
}

TEST(MorphexsynthVoice, ThirdOscillatorFollowsTheKeyByTheKeyFollowFactor)
{
    for (const auto& [factor, note, expectedNote] :
         {std::tuple{1.0f, 72, 72.0f}, std::tuple{0.0f, 72, 60.0f}, std::tuple{0.5f, 72, 66.0f}})
    {
        auto voice = makeVoice();
        tonePatch(voice);
        voice.setLevelOscillator(0, 0.0f);
        voice.setLevelOscillator(2, 1.0f);
        voice.setWaveForm(2, 0);
        voice.setOsc3KeyFollow(factor);
        voice.triggerVoice(note, 100, note);
        const auto out = render(voice, 48000 * 2);
        EXPECT_NEAR(
            cents(frequencyOf(out.left, 12000, out.left.size()), static_cast<double>(noteToFrequency(expectedNote))),
            0.0, 0.1)
            << factor;
    }
}

TEST(MorphexsynthVoice, OscillatorLevelsAreLinearAtSmallSignals)
{
    // the filter saturates its input (x / sqrt(1 + x^2)): linear only well below 1
    auto loud = makeVoice();
    auto quiet = makeVoice();
    tonePatch(loud);
    tonePatch(quiet);
    loud.setLevelOscillator(0, 0.2f);
    quiet.setLevelOscillator(0, 0.1f);
    loud.triggerVoice(48, 100, 48);
    quiet.triggerVoice(48, 100, 48);
    const auto a = render(loud, 48000);
    const auto b = render(quiet, 48000);
    EXPECT_NEAR(rms(a.left, 24000) / rms(b.left, 24000), 2.0, 0.03);
}

TEST(MorphexsynthVoice, TheOutputDoesNotDependOnTheBlockSize)
{
    std::vector<float> referenceLeft;
    std::vector<float> referenceRight;
    for (const size_t blockSize : {1u, 7u, 16u, 33u, 128u, 5000u})
    {
        auto voice = makeVoice(5u);
        tonePatch(voice);
        voice.setWaveForm(0, 2);
        voice.setWaveForm(1, 3);
        voice.setLevelOscillator(1, 0.6f);
        voice.setWaveForm(2, 4);
        voice.setLevelOscillator(2, 0.1f);
        voice.setPwmOscillator(1, 0.7f);
        voice.setCutoff(60.0f);
        voice.setResonance(0.8f);
        voice.setContourF(2.0f);
        voice.setEnvelopeAttackFilter(30.0f);
        voice.setEnvelopeDecayFilter(200.0f);
        voice.setEnvelopeSustainLevelFilter(0.3f);
        voice.setLfoWaveForm(LfoType::Triangle);
        voice.setLfoPitchFactor(5.0f);
        voice.setLfoOscModulationDepth(0.05f);
        voice.setLfoFilterModulationDepth(0.5f);
        voice.setPitchAttack(0.0f);
        voice.setPitchDecay(80.0f);
        voice.setPresetWaveTable(4);
        voice.setCtrlDimension(0, CtrlDimension::Z);
        voice.setCtrlTarget(0, CtrlTarget::FilterResonance);
        voice.setCtrlDepth(0, 0.5f);
        voice.triggerVoice(52, 90, 40);
        voice.mpeZ(9000);
        const auto out = render(voice, 30000, blockSize);
        if (referenceLeft.empty())
        {
            referenceLeft = out.left;
            referenceRight = out.right;
        }
        else
        {
            EXPECT_EQ(out.left, referenceLeft) << "block size " << blockSize;
            EXPECT_EQ(out.right, referenceRight) << "block size " << blockSize;
        }
    }
}

TEST(MorphexsynthVoice, VelocityScalesTheGainByTheCubeOfTheVelocityResponse)
{
    auto loud = makeVoice();
    auto soft = makeVoice();
    tonePatch(loud);
    tonePatch(soft);
    loud.setDynamicRangeForVelocity(0.15f);
    soft.setDynamicRangeForVelocity(0.15f);
    loud.triggerVoice(48, 127, 48);
    soft.triggerVoice(48, 64, 48);
    const double factor = 1.0 - std::cbrt(0.15);
    const double velocityGain = std::pow(64.0 / 127.0 * factor + 1.0 - factor, 3.0);
    EXPECT_NEAR(rms(render(soft, 48000).left, 24000) / rms(render(loud, 48000).left, 24000), velocityGain, 0.01);
    auto flat = makeVoice();
    tonePatch(flat);
    flat.setDynamicRangeForVelocity(1.0f);
    flat.triggerVoice(48, 20, 48);
    auto reference = makeVoice();
    tonePatch(reference);
    reference.setDynamicRangeForVelocity(1.0f);
    reference.triggerVoice(48, 127, 48);
    EXPECT_NEAR(rms(render(flat, 48000).left, 24000), rms(render(reference, 48000).left, 24000), 1e-4)
        << "a range of 1 makes every velocity equally loud";
}

TEST(MorphexsynthVoice, WithoutASettingTheVelocityRangeIs0Point15)
{
    // 7 dB between velocity 64 and 127, 12 dB down to velocity 30 (the factor 0.1 it started with gave 2 dB)
    const auto level = [](const int velocity)
    {
        auto voice = makeVoice();
        tonePatch(voice);
        voice.triggerVoice(48, velocity, 48);
        return rms(render(voice, 48000).left, 24000);
    };
    const double half = 20.0 * std::log10(level(64) / level(127));
    const double soft = 20.0 * std::log10(level(30) / level(127));
    std::printf("[ velocity ] default range: velocity 64 %.2f dB, velocity 30 %.2f dB\n", half, soft);
    EXPECT_NEAR(half, -6.9, 0.2);
    EXPECT_NEAR(soft, -11.6, 0.2);
}

TEST(MorphexsynthVoice, ASilentVoiceStartsAtPhaseZeroWhateverItPlayedBefore)
{
    auto fresh = makeVoice();
    tonePatch(fresh);
    fresh.setLfoWaveForm(LfoType::Sine);
    fresh.setLfoPitchFactor(5.0f);
    fresh.setLfoOscModulationDepth(0.02f);
    auto used = makeVoice();
    tonePatch(used);
    used.setLfoWaveForm(LfoType::Sine);
    used.setLfoPitchFactor(5.0f);
    used.setLfoOscModulationDepth(0.02f);
    used.triggerVoice(50, 100, 50);
    (void) render(used, 1234);
    used.stopVoice();
    (void) render(used, 24000);
    ASSERT_FALSE(used.isPlaying());
    fresh.triggerVoice(60, 100, 60);
    used.triggerVoice(60, 100, 60);
    const auto a = render(fresh, 9600).left;
    const auto b = render(used, 9600).left;
    // from 20 ms on (the first 16 samples of a fresh voice ramp up from silence, where a voice that played before
    // starts higher: not part of this test); the rest is a few degrees (0.04 of an amplitude of 0.33, a lead of 4
    // samples), an arbitrary phase would differ by up to twice the amplitude
    double steady = 0.0;
    for (size_t i = 960; i < a.size(); ++i)
    {
        steady = std::max(steady, static_cast<double>(std::abs(a[i] - b[i])));
    }
    std::printf("[ voice    ] a fresh and a used silent voice: largest difference from 20 ms on %.4f\n", steady);
    EXPECT_LT(steady, 0.06);
}

TEST(MorphexsynthVoice, AVoiceThatStillSoundsKeepsItsPhaseWhenItIsTriggeredAgain)
{
    // a steal or a retrigger: the waveform goes on, it does not jump to phase 0 (after 1000 samples a phase 0 would be
    // 0.73 of a period off)
    auto retriggered = makeVoice();
    auto plain = makeVoice();
    tonePatch(retriggered);
    tonePatch(plain);
    retriggered.triggerVoice(48, 100, 48);
    plain.triggerVoice(48, 100, 48);
    (void) render(retriggered, 1000);
    (void) render(plain, 1000);
    retriggered.triggerVoice(48, 100, 48);
    const auto a = render(retriggered, 3000).left;
    const auto b = render(plain, 3000).left;
    double worst = 0.0;
    for (size_t i = 1000; i < a.size(); ++i)
    {
        worst = std::max(worst, static_cast<double>(std::abs(a[i] - b[i])));
    }
    std::printf("[ voice    ] a retriggered and an undisturbed voice: largest difference %.4f\n", worst);
    EXPECT_LT(worst, 0.05);
}

TEST(MorphexsynthVoice, TheFilterIsAFourPoleLowpassAtTheCutoff)
{
    auto open = makeVoice();
    auto closed = makeVoice();
    for (auto* voice : {&open, &closed})
    {
        tonePatch(*voice);
        voice->setWaveForm(0, 2); // saw
    }
    closed.setCutoff(60.0f); // 261 Hz
    open.triggerVoice(45, 100, 45);
    closed.triggerVoice(45, 100, 45);
    const auto a = render(open, 48000);
    const auto b = render(closed, 48000);
    const double openDb = highEnergyDb(a.left, 3000.0);
    const double closedDb = highEnergyDb(b.left, 3000.0);
    std::cout << "[ filter   ] energy above 3 kHz of a saw at 110 Hz: " << openDb << " dB open, " << closedDb
              << " dB at 261 Hz\n";
    EXPECT_LT(closedDb, openDb - 30.0);
    EXPECT_LT(rms(b.left, 24000), rms(a.left, 24000));
}

TEST(MorphexsynthVoice, ResonanceRaisesThePeakAtTheCutoffAndStaysBounded)
{
    auto plain = makeVoice();
    auto resonant = makeVoice();
    for (auto* voice : {&plain, &resonant})
    {
        tonePatch(*voice);
        voice->setWaveForm(0, 4); // white noise
        voice->setLevelOscillator(0, 0.3f);
        voice->setCutoff(84.0f); // 1.05 kHz
    }
    resonant.setResonance(0.9f);
    plain.triggerVoice(48, 127, 48);
    resonant.triggerVoice(48, 127, 48);
    const auto a = render(plain, 48000 * 2);
    const auto b = render(resonant, 48000 * 2);
    const std::vector<float> noisePlain(a.left.end() - 32768, a.left.end());
    const std::vector<float> noiseResonant(b.left.end() - 32768, b.left.end());
    const auto spectrumPlain = hannMagnitudeSpectrum(noisePlain);
    const auto spectrumResonant = hannMagnitudeSpectrum(noiseResonant);
    const double binHz = static_cast<double>(c_rate) / 32768.0;
    double bestRatio = 0.0;
    double bestHz = 0.0;
    for (size_t bin = static_cast<size_t>(300.0 / binHz); bin < static_cast<size_t>(3000.0 / binHz); ++bin)
    {
        double sumPlain = 0.0;
        double sumResonant = 0.0;
        for (size_t k = bin; k < bin + 20; ++k)
        {
            sumPlain += spectrumPlain[k] * spectrumPlain[k];
            sumResonant += spectrumResonant[k] * spectrumResonant[k];
        }
        if (sumResonant / sumPlain > bestRatio)
        {
            bestRatio = sumResonant / sumPlain;
            bestHz = (static_cast<double>(bin) + 10.0) * binHz;
        }
    }
    std::cout << "[ resonance] peak +" << 10.0 * std::log10(bestRatio) << " dB at " << bestHz
              << " Hz (cutoff 1046 Hz, resonance 0.9)\n";
    EXPECT_GT(10.0 * std::log10(bestRatio), 6.0);
    EXPECT_GT(bestHz, 800.0);
    EXPECT_LT(bestHz, 2500.0);
    EXPECT_LT(peakIn(b.left, 0, b.left.size()), 4.0f);
}

TEST(MorphexsynthVoice, ResonanceAtTwoSelfOscillatesBoundedAndFinite)
{
    auto voice = makeVoice();
    tonePatch(voice);
    voice.setWaveForm(0, 4);
    voice.setLevelOscillator(0, 0.2f);
    voice.setCutoff(72.0f);
    voice.setResonance(2.0f);
    voice.triggerVoice(48, 127, 48);
    const auto out = render(voice, 48000 * 2);
    for (const float x : out.left)
    {
        ASSERT_TRUE(std::isfinite(x));
        ASSERT_LE(std::abs(x), 4.0f);
    }
    EXPECT_GT(rms(out.left, 48000), 0.01) << "it rings";
}

TEST(MorphexsynthVoice, TheAmplitudeEnvelopeHasTheSetTimesAndLevels)
{
    auto voice = makeVoice();
    tonePatch(voice);
    voice.setEnvelopeAttack(100.0f);
    voice.setEnvelopeDecay(200.0f);
    voice.setEnvelopeSustainLevel(0.5f);
    voice.setEnvelopeRelease(300.0f);
    voice.triggerVoice(60, 127, 60);
    const auto out = render(voice, 48000 * 2);
    const float peak = peakIn(out.left, 0, out.left.size());
    EXPECT_NEAR(peakIn(out.left, 4700, 5100) / peak, 1.0f, 0.03f) << "the peak at the end of the attack (4800)";
    EXPECT_NEAR(peakIn(out.left, 560, 600) / peak, 0.195f, 0.03f)
        << "the curve factor 1 starts fast: 0.195 of the way at an eighth of the attack";
    EXPECT_LT(peakIn(out.left, 0, 60) / peak, 0.05f) << "starts from 0";
    EXPECT_NEAR(peakIn(out.left, 40000, 48000) / peak, 0.5f, 0.03f) << "the sustain level";
    voice.stopVoice();
    const auto tail = render(voice, 48000);
    EXPECT_NEAR(peakIn(tail.left, 0, 200) / peak, 0.5f, 0.1f);
    EXPECT_LT(peakIn(tail.left, 14400 + 1000, 48000) / peak, 0.01f) << "gone after 300 ms";
    EXPECT_FALSE(voice.isPlaying());
}

TEST(MorphexsynthVoice, TheSustainLevelHoldsFromTheFirstNoteOfAFreshVoice)
{
    // AbacDsp ramps the sustain connector from 0 on the first note of a voice (over 43 ms), so the level only arrives
    // later; here the trigger sets it and refreshes the hold segment of the envelope
    for (const float sustain : {0.2f, 0.5f, 0.8f, 1.0f})
    {
        auto voice = makeVoice();
        tonePatch(voice);
        voice.setEnvelopeAttack(10.0f);
        voice.setEnvelopeDecay(50.0f);
        voice.setEnvelopeSustainLevel(sustain);
        voice.triggerVoice(60, 127, 60);
        const auto out = render(voice, 48000);
        const float peak = peakIn(out.left, 0, 4800);
        EXPECT_NEAR(peakIn(out.left, 24000, 48000) / peak, sustain, 0.03f) << sustain;
        EXPECT_NEAR(peak, 0.46f, 0.03f)
            << "a triangle of peak 0.87, saturated by the filter to 0.66, times the pan 0.71";
    }
}

TEST(MorphexsynthVoice, AControllerMovesTheCutoffWithoutLagAndTheResonanceToo)
{
    // AbacDsp smooths the cutoff over 128 control ticks, restarted at every tick, which is a time constant of 43 ms
    // (90 % after 100 ms): a slide of Y lagged. Here the cutoff reaches 90 % of a jump of Y within 30 ms.
    const auto stepResponseMs = [](const bool resonance)
    {
        auto voice = makeVoice();
        tonePatch(voice);
        voice.setWaveForm(0, 2);
        voice.setCutoff(resonance ? 100.0f : 40.0f);
        voice.setCtrlDimension(0, CtrlDimension::Y);
        voice.setCtrlTarget(0, resonance ? CtrlTarget::FilterResonance : CtrlTarget::FilterCutoff);
        voice.setCtrlDepth(0, resonance ? 0.5f : 2.0f);
        voice.triggerVoice(48, 100, 48);
        std::ignore = render(voice, 24000);
        voice.mpeY(16383);
        const auto out = render(voice, 12000);
        const auto level = [&out](const size_t from) { return rms(out.left, from, from + 480); };
        const double final = level(11000);
        const double initial = level(0);
        for (size_t from = 0; from + 480 <= out.left.size(); from += 48)
        {
            if (std::abs(level(from) - initial) >= 0.9 * std::abs(final - initial))
            {
                return static_cast<double>(from + 480) / c_rate * 1000.0;
            }
        }
        return 1000.0;
    };
    EXPECT_LT(stepResponseMs(false), 30.0) << "the cutoff";
    EXPECT_LT(stepResponseMs(true), 30.0) << "the resonance";
}

TEST(MorphexsynthVoice, TwoSlotsOnOneTargetAddAndDoNotOverwriteEachOther)
{
    // Y and Z both open the cutoff (Cutting Lead does): the cutoff is the sum of the two, whichever message came last.
    // (AbacDsp set it to the value of the dimension that arrived, so a stream of Y and Z messages made it jump between
    // the two values.)
    const auto level = [](const int y, const int z, const bool alternate)
    {
        auto voice = makeVoice();
        tonePatch(voice);
        voice.setWaveForm(0, 2);
        voice.setCutoff(40.0f);
        voice.setCtrlDimension(0, CtrlDimension::Y);
        voice.setCtrlTarget(0, CtrlTarget::FilterCutoff);
        voice.setCtrlDepth(0, 1.0f);
        voice.setCtrlDimension(1, CtrlDimension::Z);
        voice.setCtrlTarget(1, CtrlTarget::FilterCutoff);
        voice.setCtrlDepth(1, 1.0f);
        voice.triggerVoice(48, 100, 48);
        std::vector<float> all;
        for (int block = 0; block < 400; ++block)
        {
            if (y >= 0 && (alternate || block == 0))
            {
                voice.mpeY(y);
            }
            if (z >= 0 && (alternate || block == 0))
            {
                voice.mpeZ(z);
            }
            const auto out = render(voice, 48);
            all.insert(all.end(), out.left.begin(), out.left.end());
        }
        return rms(all, 12000, 19200);
    };
    const double yOnly = level(8192, -1, false);
    const double zOnly = level(-1, 8192, false);
    const double both = level(8192, 8192, false);
    EXPECT_GT(both, 1.05 * std::max(yOnly, zOnly)) << "the two add";
    // a stream of both messages gives the same sound as one of each
    EXPECT_NEAR(level(8192, 8192, true), both, 0.01 * both);
    // and the order does not matter: the sum is the same
    EXPECT_NEAR(level(4096, 12288, true), level(12288, 4096, true), 0.01 * both);
}

TEST(MorphexsynthVoice, ARetriggerDoesNotJumpToZero)
{
    auto voice = makeVoice();
    tonePatch(voice);
    voice.setEnvelopeAttack(50.0f);
    voice.triggerVoice(60, 127, 60);
    const auto first = render(voice, 12000);
    const float level = peakIn(first.left, 11000, 12000);
    voice.triggerVoice(64, 127, 60);
    const auto second = render(voice, 2400);
    EXPECT_GT(peakIn(second.left, 800, 2400), 0.7f * level) << "the envelope starts from its current gain";
    EXPECT_TRUE(voice.isPlaying());
}

TEST(MorphexsynthVoice, EmergencyStopEndsTheVoiceWithinTheGivenTime)
{
    auto voice = makeVoice();
    tonePatch(voice);
    voice.setEnvelopeRelease(2000.0f);
    voice.triggerVoice(60, 127, 60);
    std::ignore = render(voice, 4800);
    voice.emergencyStop(10.0f);
    std::ignore = render(voice, 480 + 200);
    EXPECT_FALSE(voice.isPlaying());
}

TEST(MorphexsynthVoice, PerNotePitchBendMovesThePitchBySemitones)
{
    auto voice = makeVoice();
    tonePatch(voice);
    voice.setCtrlDimension(0, CtrlDimension::X);
    voice.setCtrlTarget(0, CtrlTarget::PitchBend);
    voice.setCtrlType(0, CtrlValueType::BiPolar);
    voice.setCtrlCurve(0, CtrlCurve::Linear);
    voice.setCtrlDepth(0, 2.0f / 12.0f);
    voice.triggerVoice(57, 100, 57);
    auto out = render(voice, 24000);
    const double base = frequencyOf(out.left, 12000, 24000);
    EXPECT_NEAR(base, 220.0, 0.1);
    voice.mpeX(16383);
    out = render(voice, 24000);
    EXPECT_NEAR(cents(frequencyOf(out.left, 12000, 24000), 220.0), 200.0, 0.5) << "+2 semitones";
    voice.mpeX(0);
    out = render(voice, 24000);
    EXPECT_NEAR(cents(frequencyOf(out.left, 12000, 24000), 220.0), -200.0, 0.5) << "-2 semitones";
    voice.mpeX(8192);
    out = render(voice, 24000);
    EXPECT_NEAR(cents(frequencyOf(out.left, 12000, 24000), 220.0), 0.0, 0.5);
}

TEST(MorphexsynthVoice, ThePitchBendMovesSmoothlyNotInAStep)
{
    auto voice = makeVoice();
    tonePatch(voice);
    voice.setCtrlDimension(0, CtrlDimension::X);
    voice.setCtrlTarget(0, CtrlTarget::PitchBend);
    voice.setCtrlType(0, CtrlValueType::BiPolar);
    voice.setCtrlDepth(0, 1.0f);
    voice.triggerVoice(48, 100, 48);
    std::ignore = render(voice, 12000);
    voice.mpeX(16383); // a full octave
    const auto out = render(voice, 12000);
    // the frequency in windows of 480 samples: rises through the values between, not in one jump
    std::vector<double> frequencies;
    for (size_t start = 0; start + 960 <= 12000; start += 480)
    {
        frequencies.push_back(frequencyOf(out.left, start, start + 960));
    }
    double largestStep = 0.0;
    for (size_t i = 1; i < frequencies.size(); ++i)
    {
        largestStep = std::max(largestStep, std::abs(cents(frequencies[i], frequencies[i - 1])));
    }
    EXPECT_NEAR(cents(frequencies.back(), 130.8128), 1200.0, 2.0);
    EXPECT_LT(largestStep, 400.0) << "no step of an octave";
}

TEST(MorphexsynthVoice, ChannelPressureCanOpenTheFilter)
{
    auto closed = makeVoice();
    auto pressed = makeVoice();
    for (auto* voice : {&closed, &pressed})
    {
        tonePatch(*voice);
        voice->setWaveForm(0, 2);
        voice->setCutoff(55.0f);
        voice->setCtrlDimension(0, CtrlDimension::Z);
        voice->setCtrlTarget(0, CtrlTarget::FilterCutoff);
        voice->setCtrlType(0, CtrlValueType::Abs);
        voice->setCtrlCurve(0, CtrlCurve::Linear);
        voice->setCtrlDepth(0, 1.0f);
        voice->triggerVoice(45, 100, 45);
    }
    pressed.mpeZ(16383);
    const auto a = render(closed, 48000);
    const auto b = render(pressed, 48000);
    EXPECT_GT(highEnergyDb(b.left, 2000.0), highEnergyDb(a.left, 2000.0) + 10.0);
}

TEST(MorphexsynthVoice, ASlotCanPanTheVoice)
{
    auto voice = makeVoice();
    tonePatch(voice);
    voice.setCtrlDimension(0, CtrlDimension::Z);
    voice.setCtrlTarget(0, CtrlTarget::Pan);
    voice.setCtrlType(0, CtrlValueType::Abs);
    voice.setCtrlDepth(0, 1.0f);
    voice.triggerVoice(48, 100, 48);
    auto out = render(voice, 12000);
    EXPECT_NEAR(rms(out.left, 6000), rms(out.right, 6000), 1e-4) << "centred";
    voice.mpeZ(16383);
    out = render(voice, 24000);
    EXPECT_GT(rms(out.right, 18000), 20.0 * rms(out.left, 18000)) << "panned to the right";
}

TEST(MorphexsynthVoice, GlideRunsFromTheLastNoteToTheNewOne)
{
    auto voice = makeVoice();
    tonePatch(voice);
    voice.setGlide(1000.0f); // ms per octave
    voice.triggerVoice(72, 100, 60);
    const auto out = render(voice, 48000 * 2);
    const double early = frequencyOf(out.left, 480, 1900);
    const double late = frequencyOf(out.left, 48000, 96000);
    EXPECT_NEAR(cents(late, 523.2511), 0.0, 1.0);
    EXPECT_LT(cents(early, 261.6256), 120.0) << "starts at the old note";
    EXPECT_GT(cents(early, 261.6256), -5.0);
}

TEST(MorphexsynthVoice, ThePitchEnvelopeBendsTheStartOfANote)
{
    auto voice = makeVoice();
    tonePatch(voice);
    voice.setPitchAttack(0.0f);
    voice.setPitchEnvelopeDepth(12.0f);
    voice.setPitchDecay(300.0f);
    voice.triggerVoice(48, 100, 48);
    const auto out = render(voice, 48000 * 2);
    EXPECT_GT(cents(frequencyOf(out.left, 480, 3000), 130.8128), 300.0) << "starts high";
    EXPECT_NEAR(cents(frequencyOf(out.left, 48000, 96000), 130.8128), 0.0, 1.0) << "settles on the note";
}

TEST(MorphexsynthVoice, TheLfoModulatesThePitch)
{
    auto voice = makeVoice();
    tonePatch(voice);
    voice.setLfoWaveForm(LfoType::Sine);
    voice.setLfoPitchFactor(4.0f);
    voice.setLfoOscModulationDepth(0.1f); // 1.2 semitones
    voice.triggerVoice(48, 100, 48);
    const auto out = render(voice, 48000 * 3);
    double lowest = 1e9;
    double highest = 0.0;
    for (size_t start = 24000; start + 1920 <= out.left.size(); start += 480)
    {
        const double f = frequencyOf(out.left, start, start + 1920);
        lowest = std::min(lowest, f);
        highest = std::max(highest, f);
    }
    EXPECT_NEAR(cents(highest, 130.8128), 120.0, 15.0);
    EXPECT_NEAR(cents(lowest, 130.8128), -120.0, 15.0);
}

TEST(MorphexsynthVoice, TheFilterEnvelopeSweepsTheCutoffByTheContour)
{
    auto swept = makeVoice();
    auto fixed = makeVoice();
    for (auto* voice : {&swept, &fixed})
    {
        tonePatch(*voice);
        voice->setWaveForm(0, 2);
        voice->setCutoff(50.0f);
        voice->setEnvelopeAttackFilter(0.0f);
        voice->setEnvelopeDecayFilter(400.0f);
        voice->setEnvelopeSustainLevelFilter(0.0f);
    }
    swept.setContourF(3.0f);
    swept.triggerVoice(45, 100, 45);
    fixed.triggerVoice(45, 100, 45);
    const auto a = render(swept, 48000);
    const auto b = render(fixed, 48000);
    const std::vector<float> startSwept(a.left.begin() + 480, a.left.begin() + 480 + 4096);
    const std::vector<float> startFixed(b.left.begin() + 480, b.left.begin() + 480 + 4096);
    EXPECT_GT(highBandEnergyDb(startSwept, 1500.0, c_rate), highBandEnergyDb(startFixed, 1500.0, c_rate) + 15.0)
        << "bright at the start";
    EXPECT_NEAR(highEnergyDb(a.left, 1500.0), highEnergyDb(b.left, 1500.0), 1.5) << "the same once settled";
}

TEST(MorphexsynthVoice, KeyFollowRaisesTheCutoffWithTheNote)
{
    auto following = makeVoice();
    auto fixed = makeVoice();
    for (auto* voice : {&following, &fixed})
    {
        tonePatch(*voice);
        voice->setWaveForm(0, 2);
        voice->setCutoff(60.0f);
    }
    following.setKeyFollow(1.0f);
    following.triggerVoice(84, 100, 84); // two octaves above middle C
    fixed.triggerVoice(84, 100, 84);
    const double followDb = highEnergyDb(render(following, 48000).left, 5000.0);
    const double fixedDb = highEnergyDb(render(fixed, 48000).left, 5000.0);
    EXPECT_GT(followDb, fixedDb + 10.0);
}

TEST(MorphexsynthVoice, PulseWidthChangesTheSpectrumOfAnOscillator)
{
    auto plain = makeVoice();
    auto modulated = makeVoice();
    for (auto* voice : {&plain, &modulated})
    {
        tonePatch(*voice);
        voice->setWaveForm(0, 2);
    }
    modulated.setPwmOscillator(0, 1.0f);
    plain.triggerVoice(45, 100, 45);
    modulated.triggerVoice(45, 100, 45);
    const auto a = render(plain, 48000);
    const auto b = render(modulated, 48000);
    EXPECT_NE(a.left, b.left);
    const auto spectrumPlain = hannMagnitudeSpectrum(std::vector<float>(a.left.end() - 16384, a.left.end()));
    const auto spectrumModulated = hannMagnitudeSpectrum(std::vector<float>(b.left.end() - 16384, b.left.end()));
    const auto bin = static_cast<size_t>(220.0 / (static_cast<double>(c_rate) / 16384.0));
    EXPECT_GT(spectrumPlain[bin - 3] + spectrumPlain[bin] + spectrumPlain[bin + 3], 0.0);
    EXPECT_LT(spectrumModulated[bin] / spectrumPlain[bin], 0.9)
        << "the soft pulse width removes part of the 2nd harmonic";
}

TEST(MorphexsynthVoice, ADistortionPresetFlattensThePeaks)
{
    auto clean = makeVoice();
    auto distorted = makeVoice();
    for (auto* voice : {&clean, &distorted})
    {
        tonePatch(*voice);
        voice->setLevelOscillator(0, 1.0f);
    }
    distorted.setPresetWaveTable(7); // classic fuzz: a sign function
    clean.triggerVoice(48, 127, 48);
    distorted.triggerVoice(48, 127, 48);
    const auto a = render(clean, 48000);
    const auto b = render(distorted, 48000);
    const double crestClean = static_cast<double>(peakIn(a.left, 24000, 48000)) / rms(a.left, 24000);
    const double crestDistorted = static_cast<double>(peakIn(b.left, 24000, 48000)) / rms(b.left, 24000);
    EXPECT_LT(crestDistorted, 1.1) << "a fuzz makes a square wave of the tone";
    EXPECT_GT(crestClean, 1.5);
    EXPECT_LE(peakIn(b.left, 0, b.left.size()), 1.01f);
}

TEST(MorphexsynthVoice, AFilterTypeOtherThanTheFourPoleLowpassGetsTheExtraLowpass)
{
    auto highpass = makeVoice();
    auto highpassOpen = makeVoice();
    for (auto* voice : {&highpass, &highpassOpen})
    {
        tonePatch(*voice);
        voice->setWaveForm(0, 4);
        voice->setLevelOscillator(0, 0.3f);
        voice->setCutoff(70.0f);
        voice->setFilterType("2 pole hp");
    }
    highpass.setLowpassCutoff(500.0f);
    highpassOpen.setLowpassCutoff(20000.0f);
    highpass.triggerVoice(48, 127, 48);
    highpassOpen.triggerVoice(48, 127, 48);
    const auto a = render(highpass, 48000);
    const auto b = render(highpassOpen, 48000);
    EXPECT_LT(bandDb(a.left, 3000.0, 8000.0), bandDb(b.left, 3000.0, 8000.0) - 15.0)
        << "one pole at 500 Hz: -19 dB between 3 and 8 kHz";
    EXPECT_NEAR(bandDb(a.left, 20.0, 150.0), bandDb(b.left, 20.0, 150.0), 1.0) << "the low end is the same";
}

TEST(MorphexsynthVoice, UnknownFilterTypesAndOutOfRangeIndicesAreIgnored)
{
    auto voice = makeVoice();
    tonePatch(voice);
    voice.setFilterType("no such filter");
    voice.setFilterPreset(9999);
    voice.setWaveForm(7, 1);
    voice.setLevelOscillator(9, 1.0f);
    voice.setPwmOscillator(5, 0.5f);
    voice.setDetune(4, 1.0f);
    voice.setPitchFactor(3, 2.0f);
    voice.setCtrlDimension(10, CtrlDimension::X);
    voice.setCtrlTarget(99, CtrlTarget::Pan);
    voice.triggerVoice(60, 100, 60);
    const auto out = render(voice, 4800);
    for (const float x : out.left)
    {
        ASSERT_TRUE(std::isfinite(x));
    }
    EXPECT_GT(peakIn(out.left, 100, 4800), 0.05f);
}

TEST(MorphexsynthVoice, NoiseAndRandomDetuneFollowTheSeed)
{
    auto make = [](const uint64_t seed)
    {
        auto voice = makeVoice(seed);
        tonePatch(voice);
        voice.setWaveForm(0, 4);
        voice.setLevelOscillator(0, 0.3f);
        voice.setLevelOscillator(1, 0.5f);
        voice.setRandomDetune(0.3f);
        voice.triggerVoice(60, 100, 60);
        return render(voice, 6000).left;
    };
    EXPECT_EQ(make(1u), make(1u));
    EXPECT_NE(make(1u), make(2u));
}

TEST(MorphexsynthVoice, RandomDetuneSpreadsThePitchOfNotesWithTheGivenDeviation)
{
    std::vector<double> errors;
    for (uint64_t seed = 1; seed <= 40; ++seed)
    {
        auto voice = makeVoice(seed);
        tonePatch(voice);
        voice.setRandomDetune(0.5f); // semitones
        voice.triggerVoice(60, 100, 60);
        const auto out = render(voice, 24000);
        errors.push_back(cents(frequencyOf(out.left, 6000, 24000), 261.6256));
    }
    double sumSquares = 0.0;
    for (const double e : errors)
    {
        sumSquares += e * e;
    }
    const double deviation = std::sqrt(sumSquares / static_cast<double>(errors.size()));
    EXPECT_NEAR(deviation, 50.0, 15.0) << "a standard deviation of 0.5 semitone";
}

TEST(MorphexsynthVoice, StressedWithRandomPatchesNotesAndControllersItStaysFiniteAndBounded)
{
    Random random(77u);
    for (int run = 0; run < 12; ++run)
    {
        auto voice = makeVoice(static_cast<uint64_t>(run) + 1);
        for (size_t osc = 0; osc < 3; ++osc)
        {
            voice.setWaveForm(osc, static_cast<size_t>(random.uniform() * 5.0f));
            voice.setLevelOscillator(osc, random.uniform() * 2.0f - 0.5f);
            voice.setPwmOscillator(osc, random.uniform() * 2.0f - 1.0f);
            voice.setDetune(osc, (random.uniform() - 0.5f) * 24.0f);
            voice.setPitchFactor(osc, 0.25f + random.uniform() * 4.0f);
        }
        voice.setCutoff(random.uniform() * 140.0f);
        voice.setResonance(random.uniform() * 2.5f);
        voice.setFilterPreset(static_cast<size_t>(random.uniform() * static_cast<float>(c_poleMixingPresetCount)));
        voice.setEnvelopeAttack(random.uniform() * 50.0f);
        voice.setEnvelopeDecay(random.uniform() * 200.0f);
        voice.setEnvelopeSustainLevel(random.uniform());
        voice.setEnvelopeRelease(random.uniform() * 300.0f);
        voice.setContourF((random.uniform() - 0.5f) * 8.0f);
        voice.setLfoWaveForm(static_cast<LfoType>(static_cast<int>(random.uniform() * 8.0f)));
        voice.setLfoPitchFactor(random.uniform() * 30.0f);
        voice.setLfoOscModulationDepth(random.uniform());
        voice.setLfoFilterModulationDepth(random.uniform() * 3.0f);
        voice.setGlide(random.uniform() * 500.0f);
        voice.setPresetWaveTable(static_cast<size_t>(random.uniform() * 32.0f));
        for (size_t slot = 0; slot < c_mpeSlotCount; ++slot)
        {
            voice.setCtrlDimension(slot, static_cast<CtrlDimension>(static_cast<int>(random.uniform() * 7.0f)));
            voice.setCtrlTarget(slot, static_cast<CtrlTarget>(static_cast<int>(random.uniform() * 17.0f)));
            voice.setCtrlCurve(slot, static_cast<CtrlCurve>(static_cast<int>(random.uniform() * 5.0f)));
            voice.setCtrlType(slot, random.uniform() < 0.5f ? CtrlValueType::Abs : CtrlValueType::BiPolar);
            voice.setCtrlDepth(slot, (random.uniform() - 0.5f) * 4.0f);
        }
        int lastNote = 60;
        for (int i = 0; i < 60; ++i)
        {
            if (i % 6 == 0)
            {
                const int note = 20 + static_cast<int>(random.uniform() * 90.0f);
                voice.triggerVoice(note, 1 + static_cast<int>(random.uniform() * 126.0f), lastNote);
                lastNote = note;
            }
            if (i % 6 == 4)
            {
                voice.stopVoice();
            }
            voice.mpeX(static_cast<int>(random.uniform() * 16383.0f));
            voice.mpeY(static_cast<int>(random.uniform() * 16383.0f));
            voice.mpeZ(static_cast<int>(random.uniform() * 16383.0f));
            const auto out = render(voice, 700, 1 + static_cast<size_t>(random.uniform() * 300.0f));
            for (size_t k = 0; k < out.left.size(); ++k)
            {
                ASSERT_TRUE(std::isfinite(out.left[k]) && std::isfinite(out.right[k])) << "run " << run;
                ASSERT_LE(std::abs(out.left[k]), 8.0f) << "run " << run;
                ASSERT_LE(std::abs(out.right[k]), 8.0f) << "run " << run;
            }
        }
    }
}


TEST(MorphexsynthVoice, AMorphAtPositionZeroSoundsExactlyAsNoMorphAndAMorphedNoteDiffers)
{
    const auto play = [](const bool configured, const float position)
    {
        auto voice = makeVoice(3u);
        tonePatch(voice);
        voice.setWaveForm(0, 2);
        voice.setPwmOscillator(0, 0.4f);
        if (configured)
        {
            voice.setWaveMorph(0, 3, 0, position);
        }
        voice.triggerVoice(57, 100, 57);
        return render(voice, 12000);
    };
    const auto plain = play(false, 0.0f);
    EXPECT_EQ(play(true, 0.0f).left, plain.left) << "morph 0 with the other slots set";
    const auto morphed = play(true, 1.0f);
    EXPECT_NE(morphed.left, plain.left);
    EXPECT_TRUE(std::isfinite(rms(morphed.left)));
    // the note starts in the morph it was configured with, not on its way to it (the move made while the voice was
    // silent is finished at the trigger): the same as an oscillator whose first waveform is the third slot
    auto reference = makeVoice(3u);
    tonePatch(reference);
    reference.setWaveForm(0, 0);
    reference.setPwmOscillator(0, 0.4f);
    reference.triggerVoice(57, 100, 57);
    const auto triangle = render(reference, 12000);
    EXPECT_EQ(morphed.left, triangle.left) << "position 1 reads the third slot (Triangle) alone from the first sample";
}
}
