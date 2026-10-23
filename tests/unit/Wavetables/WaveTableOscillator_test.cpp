#include <cmath>
#include <gtest/gtest.h>
#include <iostream>
#include <limits>
#include <vector>

#include "dsp/Analysis/ZeroCrossingFrequency.h"
#include "dsp/Numbers/Random.h"
#include "dsp/Wavetables/WaveTableOscillator.h"
#include "tests/unit/TestHelpers.h"

namespace jp::test
{

namespace
{
constexpr float c_sampleRate{48000.0f};

const WaveTableBank& bank()
{
    static const WaveTableBank sharedBank({BasicWave::Sine, BasicWave::Triangle, BasicWave::Saw, BasicWave::SharkFin,
                                           BasicWave::Square, BasicWave::White});
    return sharedBank;
}

std::vector<float> render(WaveTableOscillator& oscillator, const size_t length, const size_t blockSize = 128)
{
    std::vector<float> out(length);
    for (size_t start = 0; start < length; start += blockSize)
    {
        oscillator.processBlock(out.data() + start, std::min(blockSize, length - start));
    }
    return out;
}

double rmsOf(const std::vector<float>& signal)
{
    double sum = 0.0;
    for (const float x : signal)
    {
        sum += static_cast<double>(x) * static_cast<double>(x);
    }
    return std::sqrt(sum / static_cast<double>(signal.size()));
}

/// The frequency nearest to f that has a whole number of cycles in 32768 samples.
double coherentFrequency(const double f)
{
    const double binHz = static_cast<double>(c_sampleRate) / 32768.0;
    return std::round(f / binHz) * binHz;
}

/// Level of harmonic k of f0 (Hann window, amplitude of the strongest bin within 2 bins), relative to the
/// fundamental, in dB.
double harmonicDb(const std::vector<double>& spectrum, const double f0, const size_t k)
{
    const double binHz = static_cast<double>(c_sampleRate) / (2.0 * static_cast<double>(spectrum.size() - 1));
    const auto centre = static_cast<size_t>(std::lround(f0 * static_cast<double>(k) / binHz));
    double strongest = 0.0;
    for (size_t bin = centre > 2 ? centre - 2 : 0; bin <= std::min(centre + 2, spectrum.size() - 1); ++bin)
    {
        strongest = std::max(strongest, spectrum[bin]);
    }
    const auto fundamentalCentre = static_cast<size_t>(std::lround(f0 / binHz));
    double fundamental = 0.0;
    for (size_t bin = fundamentalCentre - 2; bin <= fundamentalCentre + 2; ++bin)
    {
        fundamental = std::max(fundamental, spectrum[bin]);
    }
    return toDb(strongest / fundamental);
}

/// Power outside +-2 bins of the harmonics of f0 (and outside DC), relative to the total, in dB: what is left
/// when the harmonics are taken out is aliasing and interpolation images. With f0 on a bin centre (coherentFrequency)
/// the Hann window puts each harmonic into three bins, so there is no leakage to speak of.
double inharmonicDb(const std::vector<double>& spectrum, const double f0)
{
    const double binHz = static_cast<double>(c_sampleRate) / (2.0 * static_cast<double>(spectrum.size() - 1));
    double total = 0.0;
    double other = 0.0;
    for (size_t bin = 1; bin < spectrum.size(); ++bin)
    {
        const double power = spectrum[bin] * spectrum[bin];
        total += power;
        const double harmonic = std::round(static_cast<double>(bin) * binHz / f0);
        const bool near = harmonic >= 1.0 && std::abs(static_cast<double>(bin) * binHz - harmonic * f0) <= 2.0 * binHz;
        other += near ? 0.0 : power;
    }
    return 10.0 * std::log10(other / total);
}
}

TEST(WaveTableOscillator, AWaveformThatTheBankLacksIsIgnoredAndAnEmptyBankIsSilent)
{
    const WaveTableBank onlySaw({BasicWave::Saw});
    WaveTableOscillator oscillator(c_sampleRate, onlySaw);
    EXPECT_FALSE(oscillator.hasWave()) << "the default waveform is a sine, which the bank does not have";
    oscillator.setFrequency(440.0f);
    std::vector<float> out(64, 1.0f);
    oscillator.processBlock(out.data(), out.size());
    for (const float x : out)
    {
        EXPECT_EQ(x, 0.0f);
    }
    oscillator.setWave(BasicWave::Saw);
    EXPECT_TRUE(oscillator.hasWave());
    oscillator.setWave(BasicWave::Square);
    oscillator.processBlock(out.data(), out.size());
    EXPECT_NE(out[40], 0.0f) << "the saw is still selected";
}

TEST(WaveTableOscillator, ThePitchIsExactAcrossTheRange)
{
    double worstCents = 0.0;
    for (const float frequency : {20.0f, 27.5f, 110.0f, 261.63f, 440.0f, 1000.0f, 4186.0f, 8000.0f, 12000.0f})
    {
        WaveTableOscillator oscillator(c_sampleRate, bank());
        oscillator.setWave(BasicWave::Triangle);
        oscillator.setFrequency(frequency);
        const auto signal = render(oscillator, static_cast<size_t>(c_sampleRate) * 6);
        ZeroCrossingFrequency crossings(c_sampleRate, 0.05f);
        for (const float x : signal)
        {
            crossings.add(x);
        }
        const double cents = 1200.0 * std::log2(crossings.frequency() / static_cast<double>(frequency));
        worstCents = std::max(worstCents, std::abs(cents));
        EXPECT_NEAR(cents, 0.0, 0.02) << frequency << " Hz";
    }
    std::cout << "[ pitch    ] worst error " << worstCents << " cent over 20 Hz to 12 kHz\n";
}

TEST(WaveTableOscillator, TonalWavesHaveTheRmsOfTheirTable)
{
    for (const auto& [wave, tolerance] : {std::pair{BasicWave::Sine, 0.001}, std::pair{BasicWave::Triangle, 0.005},
                                          std::pair{BasicWave::Saw, 0.02}, std::pair{BasicWave::Square, 0.02}})
    {
        WaveTableOscillator oscillator(c_sampleRate, bank());
        oscillator.setWave(wave);
        oscillator.setFrequency(100.0f);
        const auto signal = render(oscillator, static_cast<size_t>(c_sampleRate));
        std::cout << "[ rms      ] wave " << static_cast<int>(wave) << " at 100 Hz: " << rmsOf(signal) << "\n";
        EXPECT_NEAR(rmsOf(signal), 0.5, tolerance) << static_cast<int>(wave);
    }
}

TEST(WaveTableOscillator, ASawHasItsHarmonicsAtOneOverK)
{
    WaveTableOscillator oscillator(c_sampleRate, bank());
    oscillator.setWave(BasicWave::Saw);
    oscillator.setFrequency(750.0f); // 16 cycles in 1024 samples: coherent
    const auto spectrum = hannMagnitudeSpectrum(render(oscillator, 16384));
    for (const size_t k : {2u, 3u, 5u, 10u})
    {
        EXPECT_NEAR(harmonicDb(spectrum, 750.0, k), -20.0 * std::log10(static_cast<double>(k)), 0.3) << k;
    }
}

TEST(WaveTableOscillator, NothingAliasesAcrossTheRange)
{
    double worst = -300.0;
    double worstFrequency = 0.0;
    int worstWave = 0;
    for (const BasicWave wave : {BasicWave::Saw, BasicWave::Square, BasicWave::SharkFin})
    {
        for (const double nominal : {55.0, 331.0, 997.0, 2503.0, 5003.0, 7993.0, 11003.0})
        {
            const double f0 = coherentFrequency(nominal);
            WaveTableOscillator oscillator(c_sampleRate, bank());
            oscillator.setWave(wave);
            oscillator.setFrequency(static_cast<float>(f0));
            const double db = inharmonicDb(hannMagnitudeSpectrum(render(oscillator, 32768)), f0);
            if (db > worst)
            {
                worst = db;
                worstFrequency = f0;
                worstWave = static_cast<int>(wave);
            }
            EXPECT_LT(db, -60.0) << static_cast<int>(wave) << " at " << f0 << " Hz";
        }
    }
    std::cout << "[ aliasing ] worst inharmonic power " << worst << " dB re total (wave " << worstWave << " at "
              << worstFrequency << " Hz; saw, square, shark fin, 55 Hz to 11 kHz)\n";
}

TEST(WaveTableOscillator, ABlockCutAnywhereGivesTheSameSamples)
{
    for (const BasicWave wave : {BasicWave::Saw, BasicWave::White})
    {
        std::vector<float> reference;
        for (const size_t blockSize : {1u, 7u, 16u, 128u, 5000u})
        {
            WaveTableOscillator oscillator(c_sampleRate, bank(), 99u);
            oscillator.setWave(wave);
            oscillator.setFrequency(333.0f);
            oscillator.setPwmMode(WaveTableOscillator::PwmMode::Soft);
            oscillator.setPwm(0.8f);
            const auto out = render(oscillator, 10000, blockSize);
            if (reference.empty())
            {
                reference = out;
            }
            else
            {
                EXPECT_EQ(out, reference) << static_cast<int>(wave) << " block size " << blockSize;
            }
        }
    }
}

TEST(WaveTableOscillator, PulseWidthOffsetOneHalfLeavesOnlyOddHarmonicsOfASaw)
{
    WaveTableOscillator oscillator(c_sampleRate, bank());
    oscillator.setWave(BasicWave::Saw);
    oscillator.setFrequency(750.0f);
    oscillator.setPwmMode(WaveTableOscillator::PwmMode::Strong);
    oscillator.setPwm(0.5f); // the default offset: no ramp
    const auto spectrum = hannMagnitudeSpectrum(render(oscillator, 16384));
    for (const size_t k : {2u, 4u, 6u, 8u})
    {
        EXPECT_LT(harmonicDb(spectrum, 750.0, k), -70.0) << k;
    }
    for (const size_t k : {3u, 5u, 7u})
    {
        EXPECT_NEAR(harmonicDb(spectrum, 750.0, k), -20.0 * std::log10(static_cast<double>(k)), 0.3) << k;
    }
}

TEST(WaveTableOscillator, PulseWidthOffsetOneQuarterRemovesEveryFourthHarmonic)
{
    WaveTableOscillator oscillator(c_sampleRate, bank());
    oscillator.setWave(BasicWave::Saw);
    oscillator.setFrequency(750.0f);
    oscillator.setPwmMode(WaveTableOscillator::PwmMode::Strong);
    oscillator.setPwm(0.25f);
    std::vector<float> settle(1024);
    oscillator.processBlock(settle.data(), settle.size()); // the 10 ms ramp from 0.5
    const auto spectrum = hannMagnitudeSpectrum(render(oscillator, 16384));
    for (const size_t k : {4u, 8u, 12u})
    {
        EXPECT_LT(harmonicDb(spectrum, 750.0, k), -60.0) << k;
    }
    for (const size_t k : {1u, 2u, 3u, 5u})
    {
        EXPECT_GT(harmonicDb(spectrum, 750.0, k), -20.0 * std::log10(static_cast<double>(k)) - 7.0) << k;
    }
}

TEST(WaveTableOscillator, SoftAndStrongSubtractTheCopyWithWeightQuarterAndOne)
{
    for (const auto& [mode, weight] :
         {std::pair{WaveTableOscillator::PwmMode::Soft, 0.25f}, std::pair{WaveTableOscillator::PwmMode::Strong, 1.0f}})
    {
        WaveTableOscillator plain(c_sampleRate, bank());
        WaveTableOscillator shifted(c_sampleRate, bank());
        WaveTableOscillator modulated(c_sampleRate, bank());
        for (auto* oscillator : {&plain, &shifted, &modulated})
        {
            oscillator->setWave(BasicWave::Saw);
            oscillator->setFrequency(441.0f);
        }
        shifted.setPhase(0.5);
        modulated.setPwmMode(mode);
        const auto a = render(plain, 4000);
        const auto b = render(shifted, 4000);
        const auto c = render(modulated, 4000);
        for (size_t i = 0; i < a.size(); i += 13)
        {
            ASSERT_NEAR(c[i], a[i] - weight * b[i], 1e-5f) << i;
        }
    }
}

TEST(WaveTableOscillator, PulseWidthOffOrWithoutAModeChangesNothing)
{
    WaveTableOscillator plain(c_sampleRate, bank());
    WaveTableOscillator moved(c_sampleRate, bank());
    for (auto* oscillator : {&plain, &moved})
    {
        oscillator->setWave(BasicWave::Triangle);
        oscillator->setFrequency(300.0f);
    }
    moved.setPwm(0.9f);
    EXPECT_EQ(render(plain, 3000), render(moved, 3000)) << "the mode is off";
}

TEST(WaveTableOscillator, ThePulseWidthMovesLinearlyInTenMillisecondsAndLandsExactly)
{
    WaveTableOscillator oscillator(c_sampleRate, bank());
    oscillator.setWave(BasicWave::Saw);
    oscillator.setFrequency(200.0f);
    EXPECT_EQ(oscillator.getPwm(), 0.5);
    oscillator.setPwm(0.9f);
    std::vector<float> out(240);
    oscillator.processBlock(out.data(), out.size());
    EXPECT_NEAR(oscillator.getPwm(), 0.5 + (static_cast<double>(0.9f) - 0.5) * 0.5, 1e-9) << "half way after 5 ms";
    oscillator.processBlock(out.data(), 239);
    EXPECT_LT(oscillator.getPwm(), static_cast<double>(0.9f));
    oscillator.processBlock(out.data(), 1);
    EXPECT_EQ(oscillator.getPwm(), static_cast<double>(0.9f)) << "set exactly after 480 samples";
    oscillator.setPwm(0.9f);
    EXPECT_EQ(oscillator.getPwm(), static_cast<double>(0.9f)) << "the same value starts no ramp";
    oscillator.setPwm(5.0f);
    oscillator.processBlock(out.data(), 240);
    oscillator.processBlock(out.data(), 240);
    EXPECT_EQ(oscillator.getPwm(), 1.0) << "clamped to 1";
}

TEST(WaveTableOscillator, WhiteIsUniformNoiseInMinusTwoToTwo)
{
    WaveTableOscillator oscillator(c_sampleRate, bank(), 5u);
    oscillator.setWave(BasicWave::White);
    oscillator.setFrequency(440.0f);
    const auto signal = render(oscillator, 96000);
    double sum = 0.0;
    float peak = 0.0f;
    for (const float x : signal)
    {
        sum += static_cast<double>(x);
        peak = std::max(peak, std::abs(x));
    }
    EXPECT_NEAR(sum / static_cast<double>(signal.size()), 0.0, 0.02);
    EXPECT_NEAR(rmsOf(signal), 2.0 / std::sqrt(3.0), 0.01);
    EXPECT_LT(peak, 2.001f);
    EXPECT_GT(peak, 1.99f);
    const auto spectrum = hannMagnitudeSpectrum(std::vector<float>(signal.begin(), signal.begin() + 16384));
    double low = 0.0;
    double high = 0.0;
    for (size_t bin = 100; bin < 2000; ++bin)
    {
        low += spectrum[bin] * spectrum[bin];
    }
    for (size_t bin = 6000; bin < 7900; ++bin)
    {
        high += spectrum[bin] * spectrum[bin];
    }
    EXPECT_NEAR(10.0 * std::log10(low / high), 0.0, 0.7) << "flat";
}

TEST(WaveTableOscillator, TheNoiseFollowsTheSeed)
{
    auto noise = [](const uint64_t seed)
    {
        WaveTableOscillator oscillator(c_sampleRate, bank(), seed);
        oscillator.setWave(BasicWave::White);
        return render(oscillator, 2000);
    };
    EXPECT_EQ(noise(1u), noise(1u));
    EXPECT_NE(noise(1u), noise(2u));
}

TEST(WaveTableOscillator, ANegativeOrNanFrequencyStopsTheOscillator)
{
    for (const float frequency : {-100.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f})
    {
        WaveTableOscillator oscillator(c_sampleRate, bank());
        oscillator.setWave(BasicWave::Saw);
        oscillator.setPhase(0.25);
        oscillator.setFrequency(frequency);
        const auto out = render(oscillator, 64);
        for (const float x : out)
        {
            EXPECT_EQ(x, out.front());
        }
        EXPECT_EQ(oscillator.getPhase(), 0.25);
    }
}

TEST(WaveTableOscillator, FrequenciesAboveNyquistAreLimitedToIt)
{
    WaveTableOscillator oscillator(c_sampleRate, bank());
    oscillator.setWave(BasicWave::Saw);
    oscillator.setFrequency(1e9f);
    const auto out = render(oscillator, 256);
    for (const float x : out)
    {
        ASSERT_TRUE(std::isfinite(x));
        ASSERT_LT(std::abs(x), 2.0f);
    }
}

TEST(WaveTableOscillator, RandomFrequenciesPhasesAndPulseWidthsStayInsideTheTable)
{
    Random random(11u);
    WaveTableOscillator oscillator(c_sampleRate, bank());
    oscillator.setWave(BasicWave::Square);
    oscillator.setPwmMode(WaveTableOscillator::PwmMode::Strong);
    std::vector<float> out(200);
    for (int i = 0; i < 4000; ++i)
    {
        oscillator.setFrequency(random.uniform() * 30000.0f);
        oscillator.setPwm(random.uniform() * 1.2f - 0.1f);
        oscillator.setPhase(static_cast<double>(random.uniform()) * 3.0 - 1.0);
        oscillator.processBlock(out.data(), 1 + static_cast<size_t>(random.uniform() * 199.0f));
        ASSERT_TRUE(std::isfinite(out[0]));
        ASSERT_GE(oscillator.getPhase(), 0.0);
        ASSERT_LT(oscillator.getPhase(), 1.0);
    }
}

TEST(WaveTableOscillator, SetPhaseWrapsIntoTheCycle)
{
    WaveTableOscillator oscillator(c_sampleRate, bank());
    oscillator.setPhase(2.25);
    EXPECT_DOUBLE_EQ(oscillator.getPhase(), 0.25);
    oscillator.setPhase(-0.25);
    EXPECT_DOUBLE_EQ(oscillator.getPhase(), 0.75);
    oscillator.setPhase(-1e-20);
    EXPECT_LT(oscillator.getPhase(), 1.0);
}


// -- the morph over three slots ---------------------------------------------------------------------------------

namespace
{
WaveTableOscillator morphing(const BasicWave first, const BasicWave second, const BasicWave third,
                             const float frequency)
{
    WaveTableOscillator oscillator(c_sampleRate, bank());
    oscillator.setWaveset(0, first);
    oscillator.setWaveset(1, second);
    oscillator.setWaveset(2, third);
    oscillator.setFrequency(frequency);
    return oscillator;
}

WaveTableOscillator plain(const BasicWave wave, const float frequency)
{
    WaveTableOscillator oscillator(c_sampleRate, bank());
    oscillator.setWave(wave);
    oscillator.setFrequency(frequency);
    return oscillator;
}
}

TEST(WaveTableOscillatorMorph, PositionZeroIsBitIdenticalToAnOscillatorThatNeverMorphs)
{
    for (const BasicWave wave :
         {BasicWave::Triangle, BasicWave::Saw, BasicWave::Square, BasicWave::SharkFin, BasicWave::White})
    {
        auto morphed = morphing(wave, BasicWave::Square, BasicWave::Saw, 331.0f);
        auto reference = plain(wave, 331.0f);
        morphed.setMorph(0.0f);
        morphed.setPwmMode(WaveTableOscillator::PwmMode::Soft);
        reference.setPwmMode(WaveTableOscillator::PwmMode::Soft);
        morphed.setPwm(0.8f);
        reference.setPwm(0.8f);
        EXPECT_EQ(render(morphed, 6000), render(reference, 6000)) << static_cast<int>(wave);
    }
}

TEST(WaveTableOscillatorMorph, APositionThatIsASlotReadsThatSlotAlone)
{
    for (const auto& [position, wave] : {std::pair{0.5f, BasicWave::Square}, std::pair{1.0f, BasicWave::Saw}})
    {
        auto morphed = morphing(BasicWave::Triangle, BasicWave::Square, BasicWave::Saw, 997.0f);
        auto reference = plain(wave, 997.0f);
        morphed.setMorph(position);
        render(morphed, 480); // the ramp
        // the reference oscillator runs the same 480 samples, so the phases agree
        std::vector<float> skip(480);
        reference.processBlock(skip.data(), skip.size());
        EXPECT_EQ(morphed.getMorph(), static_cast<double>(position));
        EXPECT_EQ(render(morphed, 4096), render(reference, 4096)) << position;
    }
}

TEST(WaveTableOscillatorMorph, BetweenTwoSlotsTheOutputIsTheLinearMixOfBoth)
{
    auto morphed = morphing(BasicWave::Triangle, BasicWave::Square, BasicWave::Saw, 441.0f);
    auto first = plain(BasicWave::Triangle, 441.0f);
    auto second = plain(BasicWave::Square, 441.0f);
    auto third = plain(BasicWave::Saw, 441.0f);
    morphed.setMorph(0.25f);
    render(morphed, 480);
    render(first, 480);
    render(second, 480);
    render(third, 480);
    const auto mixed = render(morphed, 2048);
    const auto a = render(first, 2048);
    const auto b = render(second, 2048);
    render(third, 2048);
    for (size_t i = 0; i < mixed.size(); ++i)
    {
        ASSERT_NEAR(mixed[i], 0.5f * a[i] + 0.5f * b[i], 2e-6f) << i;
    }
    morphed.setMorph(0.75f);
    render(morphed, 480);
    render(second, 480);
    render(third, 480);
    const auto upper = render(morphed, 2048);
    const auto c = render(second, 2048);
    const auto d = render(third, 2048);
    for (size_t i = 0; i < upper.size(); ++i)
    {
        ASSERT_NEAR(upper[i], 0.5f * c[i] + 0.5f * d[i], 2e-6f) << i;
    }
}

TEST(WaveTableOscillatorMorph, ASlotThatWasNeverSetReadsAsTheFirst)
{
    WaveTableOscillator oscillator(c_sampleRate, bank());
    oscillator.setWave(BasicWave::Saw);
    oscillator.setFrequency(300.0f);
    auto reference = plain(BasicWave::Saw, 300.0f);
    oscillator.setMorph(0.3f);
    render(oscillator, 480);
    render(reference, 480);
    EXPECT_EQ(render(oscillator, 2048), render(reference, 2048));
}

TEST(WaveTableOscillatorMorph, ThePositionMovesLinearlyInTenMillisecondsAndLandsExactly)
{
    auto oscillator = morphing(BasicWave::Triangle, BasicWave::Square, BasicWave::Saw, 200.0f);
    std::vector<float> out(240);
    oscillator.setMorph(0.8f);
    oscillator.processBlock(out.data(), out.size());
    EXPECT_NEAR(oscillator.getMorph(), 0.8 * 0.5, 1e-6) << "half way after 5 ms";
    oscillator.processBlock(out.data(), 239);
    EXPECT_LT(oscillator.getMorph(), static_cast<double>(0.8f));
    oscillator.processBlock(out.data(), 1);
    EXPECT_EQ(oscillator.getMorph(), static_cast<double>(0.8f));
    oscillator.setMorph(7.0f);
    render(oscillator, 480);
    EXPECT_EQ(oscillator.getMorph(), 1.0) << "clamped to 1";
    oscillator.setMorph(-3.0f);
    render(oscillator, 480);
    EXPECT_EQ(oscillator.getMorph(), 0.0) << "clamped to 0";
}

TEST(WaveTableOscillatorMorph, AMorphedWaveDoesNotAliasMoreThanThePlainOnes)
{
    double worst = -300.0;
    for (const double nominal : {55.0, 331.0, 997.0, 2503.0, 5003.0, 7993.0, 11003.0})
    {
        const double f0 = coherentFrequency(nominal);
        for (const float position : {0.1f, 0.25f, 0.5f, 0.6f, 0.75f, 1.0f})
        {
            auto oscillator = morphing(BasicWave::Saw, BasicWave::Square, BasicWave::SharkFin, static_cast<float>(f0));
            oscillator.setMorph(position);
            render(oscillator, 480);
            const double db = inharmonicDb(hannMagnitudeSpectrum(render(oscillator, 32768)), f0);
            worst = std::max(worst, db);
            EXPECT_LT(db, -60.0) << position << " at " << f0 << " Hz";
        }
    }
    std::cout << "[ aliasing ] worst inharmonic power of a morph " << worst
              << " dB re total (saw, square, shark fin)\n";
}

TEST(WaveTableOscillatorMorph, ASweepHasNoClickAndTheBlockCutsDoNotMatter)
{
    auto first = morphing(BasicWave::Triangle, BasicWave::Square, BasicWave::Saw, 200.0f);
    auto second = morphing(BasicWave::Triangle, BasicWave::Square, BasicWave::Saw, 200.0f);
    first.setMorph(1.0f);
    second.setMorph(1.0f);
    EXPECT_EQ(render(first, 9600, 7), render(second, 9600, 1000));
    // no jump while the position moves: the largest step between neighbouring samples stays within that of the plain
    // waves (a switch between the waves would show as a step of their difference)
    double largestPlain = 0.0;
    for (const BasicWave wave : {BasicWave::Triangle, BasicWave::Square, BasicWave::Saw})
    {
        auto oscillator = plain(wave, 200.0f);
        const auto signal = render(oscillator, 9600);
        for (size_t i = 1; i < signal.size(); ++i)
        {
            largestPlain = std::max(largestPlain, static_cast<double>(std::abs(signal[i] - signal[i - 1])));
        }
    }
    auto oscillator = morphing(BasicWave::Triangle, BasicWave::Square, BasicWave::Saw, 200.0f);
    oscillator.setMorph(1.0f);
    const auto swept = render(oscillator, 9600);
    double largestSwept = 0.0;
    for (size_t i = 1; i < swept.size(); ++i)
    {
        largestSwept = std::max(largestSwept, static_cast<double>(std::abs(swept[i] - swept[i - 1])));
    }
    EXPECT_LE(largestSwept, largestPlain * 1.05);
}

TEST(WaveTableOscillatorMorph, ThePitchStaysWhereItIs)
{
    for (const float position : {0.0f, 0.2f, 0.5f, 0.8f, 1.0f})
    {
        auto oscillator = morphing(BasicWave::Triangle, BasicWave::Sine, BasicWave::Triangle, 440.0f);
        oscillator.setMorph(position);
        render(oscillator, 480); // the ramp
        const auto signal = render(oscillator, static_cast<size_t>(c_sampleRate) * 4);
        ZeroCrossingFrequency crossings(c_sampleRate, 0.05f);
        for (const float x : signal)
        {
            crossings.add(x);
        }
        EXPECT_NEAR(1200.0 * std::log2(crossings.frequency() / 440.0), 0.0, 0.02) << position;
    }
}

TEST(WaveTableOscillatorMorph, ANoiseSlotAddsItsNoiseInProportionToItsWeight)
{
    std::vector<double> levels;
    for (const float position : {0.0f, 0.25f, 0.5f})
    {
        auto oscillator = morphing(BasicWave::Sine, BasicWave::White, BasicWave::Sine, 440.0f);
        oscillator.setMorph(position);
        render(oscillator, 480);
        levels.push_back(rmsOf(render(oscillator, 48000)));
    }
    EXPECT_NEAR(levels[0], 0.5, 0.01) << "the sine alone";
    EXPECT_NEAR(levels[1], std::sqrt(0.25 * 0.25 + 0.25 * 1.3333), 0.03) << "half sine, half noise";
    EXPECT_NEAR(levels[2], std::sqrt(1.3333), 0.03) << "the noise alone";
}

TEST(WaveTableOscillatorMorph, RandomMorphingStaysFinite)
{
    Random random(11u);
    auto oscillator = morphing(BasicWave::White, BasicWave::Saw, BasicWave::Square, 1000.0f);
    oscillator.setPwmMode(WaveTableOscillator::PwmMode::Strong);
    std::vector<float> out(200);
    for (int i = 0; i < 2000; ++i)
    {
        oscillator.setMorph(random.uniform() * 1.4f - 0.2f);
        oscillator.setFrequency(20.0f + random.uniform() * 12000.0f);
        oscillator.setPwm(random.uniform());
        oscillator.processBlock(out.data(), 1 + static_cast<size_t>(random.uniform() * 199.0f));
        for (const float x : out)
        {
            ASSERT_TRUE(std::isfinite(x));
        }
    }
}
}
