// Views of jp::WaveTableBank and jp::WaveTableOscillator (dsp/Wavetables/) for documentation/Wavetables:
// the one-cycle shapes, the harmonics kept by the mipmap levels, the non-harmonic power of a saw with the
// level chosen by the oscillator against one table for all pitches, the pitch-bend headroom of a level,
// and the spectra under pulse-width modulation. Idea from AbacDsp documentation/Wavetables (see
// docs/references.md). Writes documentation/Plot/PyConPlot.py text files and summary.txt into the
// directory given as the only argument.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "dsp/Wavetables/WaveTableBank.h"
#include "dsp/Wavetables/WaveTableOscillator.h"
#include "tests/unit/TestHelpers.h"

namespace
{
constexpr float c_sampleRate{48000.0f};
constexpr size_t c_fftSize{32768};
constexpr double c_binHz{static_cast<double>(c_sampleRate) / static_cast<double>(c_fftSize)};

/// The frequency nearest to f with a whole number of cycles in c_fftSize samples.
double coherent(const double f)
{
    return std::round(f / c_binHz) * c_binHz;
}

/// Amplitude of harmonic k of one table cycle.
double harmonicAmplitude(const jp::WaveTable& table, const size_t k)
{
    constexpr double twoPi{6.283185307179586};
    std::complex<double> sum{};
    for (size_t n = 0; n < jp::c_waveTableSize; ++n)
    {
        const double angle =
            -twoPi * static_cast<double>((k * n) % jp::c_waveTableSize) / static_cast<double>(jp::c_waveTableSize);
        sum += static_cast<double>(table.data[n]) * std::complex<double>(std::cos(angle), std::sin(angle));
    }
    return 2.0 * std::abs(sum) / static_cast<double>(jp::c_waveTableSize);
}

double toDb(const double amplitude)
{
    return 20.0 * std::log10(amplitude + 1e-12);
}

/// What the oscillator does, with a level that is not chosen by the pitch.
std::vector<float> renderWithLevel(const jp::WaveTableSet& set, const size_t level, const double frequency,
                                   const size_t length)
{
    const float* data = set.tables[level].data.data();
    const double increment = frequency / static_cast<double>(c_sampleRate);
    std::vector<float> out(length);
    double phase = 0.0;
    for (float& sample : out)
    {
        const double position = phase * static_cast<double>(jp::c_waveTableSize);
        const auto index = static_cast<size_t>(position);
        const auto fraction = static_cast<float>(position - static_cast<double>(index));
        sample = data[index] + (data[index + 1] - data[index]) * fraction;
        phase += increment;
        phase -= phase >= 1.0 ? 1.0 : 0.0;
    }
    return out;
}

std::vector<float> renderOscillator(const jp::WaveTableBank& bank, const jp::BasicWave wave, const double frequency,
                                    const size_t length)
{
    jp::WaveTableOscillator oscillator(c_sampleRate, bank);
    oscillator.setWave(wave);
    oscillator.setFrequency(static_cast<float>(frequency));
    std::vector<float> out(length);
    oscillator.processBlock(out.data(), length);
    return out;
}

/// Power outside +-2 bins of the harmonics of f0, relative to the total, in dB.
double inharmonicDb(const std::vector<float>& signal, const double f0)
{
    const auto spectrum = jp::test::hannMagnitudeSpectrum(signal);
    double total = 0.0;
    double other = 0.0;
    for (size_t bin = 1; bin < spectrum.size(); ++bin)
    {
        const double power = spectrum[bin] * spectrum[bin];
        total += power;
        const double harmonic = std::round(static_cast<double>(bin) * c_binHz / f0);
        const bool near =
            harmonic >= 1.0 && std::abs(static_cast<double>(bin) * c_binHz - harmonic * f0) <= 2.0 * c_binHz;
        other += near ? 0.0 : power;
    }
    return 10.0 * std::log10(other / total);
}

void writeShapes(std::ofstream& out, const jp::WaveTableBank& bank)
{
    for (const auto& [wave, name] :
         {std::pair{jp::BasicWave::Triangle, "Triangle"}, std::pair{jp::BasicWave::SharkFin, "SharkFin"},
          std::pair{jp::BasicWave::Saw, "Saw"}, std::pair{jp::BasicWave::Square, "Square"}})
    {
        const auto& table = bank.find(wave)->tables[0];
        out << "@New plot: title=\"" << name << ", level 0 (1024 harmonics)\" miny=-1.1 maxy=1.1\n#one cycle\n";
        for (size_t n = 0; n <= jp::c_waveTableSize; n += 4)
        {
            out << static_cast<double>(n) / static_cast<double>(jp::c_waveTableSize) << " " << table.data[n] << "\n";
        }
    }
}

void writeLevels(std::ofstream& out, const jp::WaveTableBank& bank, std::ofstream& summary)
{
    const auto& set = *bank.find(jp::BasicWave::Saw);
    const double fundamental = harmonicAmplitude(set.tables[0], 1);
    out << "@New plot: title=\"saw: harmonic level per mipmap level\" labelx=\"harmonic\" labely=\"dB re fundamental\" "
           "miny=-110 maxy=5\n";
    for (const size_t level : {size_t{0}, size_t{3}, size_t{6}, size_t{9}, size_t{12}, size_t{15}})
    {
        const auto harmonics = static_cast<size_t>(std::lround(0.6 / static_cast<double>(set.tables[level].topFreq)));
        out << "#level " << level << " (" << harmonics << " harmonics, serves up to "
            << std::lround(static_cast<double>(set.tables[level].topFreq) / 2.0 * static_cast<double>(c_sampleRate))
            << " Hz)\n";
        for (size_t k = 1; k <= std::min<size_t>(harmonics + 12, 1000); k += (k < 40 ? 1 : 8))
        {
            out << k << " " << std::max(toDb(harmonicAmplitude(set.tables[level], k) / fundamental), -140.0) << "\n";
        }
    }
    summary << "saw level 0, harmonic 2 / 10 / 100 / 1000 (dB re fundamental): ";
    for (const size_t k : {size_t{2}, size_t{10}, size_t{100}, size_t{1000}})
    {
        summary << toDb(harmonicAmplitude(set.tables[0], k) / fundamental) << " ";
    }
    summary << "\n";
}

void writeAliasing(std::ofstream& out, const jp::WaveTableBank& bank, std::ofstream& summary)
{
    const auto& set = *bank.find(jp::BasicWave::Saw);
    out << "@New plot: title=\"saw: power that is not a harmonic, re total\" labelx=\"fundamental (Hz)\" "
           "labely=\"dB\" miny=-100 maxy=0\n#level chosen by the oscillator\n";
    double worstChosen = -300.0;
    double worstSingle = -300.0;
    std::vector<std::pair<double, double>> single;
    for (int i = 0; i <= 48; ++i)
    {
        const double f0 = coherent(40.0 * std::pow(12000.0 / 40.0, i / 48.0));
        const double chosen = inharmonicDb(renderOscillator(bank, jp::BasicWave::Saw, f0, c_fftSize), f0);
        out << f0 << " " << chosen << "\n";
        worstChosen = std::max(worstChosen, chosen);
        single.emplace_back(f0, inharmonicDb(renderWithLevel(set, 0, f0, c_fftSize), f0));
        worstSingle = std::max(worstSingle, single.back().second);
    }
    out << "#one table with all 1024 harmonics for every pitch\n";
    for (const auto& [f0, db] : single)
    {
        out << f0 << " " << db << "\n";
    }
    summary << "saw, 40 Hz to 12 kHz, worst non-harmonic power: " << worstChosen << " dB with the chosen level, "
            << worstSingle << " dB with the first level only\n";
}

void writeBend(std::ofstream& out, const jp::WaveTableBank& bank, std::ofstream& summary)
{
    const auto& set = *bank.find(jp::BasicWave::Saw);
    // the top of the range of the 12-harmonic level: twice the increment is just below its topFreq 0.05
    const double top = 0.999 * 0.025 * static_cast<double>(c_sampleRate);
    const size_t level = set.levelFor(static_cast<float>(2.0 * top / static_cast<double>(c_sampleRate)));
    out << "@New plot: title=\"saw: a level kept while the pitch rises from " << std::lround(top) << " Hz (level "
        << level << ", 12 harmonics)\" labelx=\"pitch ratio\" labely=\"non-harmonic power re total (dB)\" miny=-100 "
        << "maxy=0\n#level kept\n";
    double onset = 0.0;
    for (int i = 0; i <= 60; ++i)
    {
        const double ratio = 1.0 + 1.6 * i / 60.0;
        const double f = coherent(top * ratio);
        const double db = inharmonicDb(renderWithLevel(set, level, f, c_fftSize), f);
        out << f / top << " " << db << "\n";
        if (onset == 0.0 && db > -60.0)
        {
            onset = f / top;
        }
    }
    summary << "level " << level << " kept from " << top << " Hz: non-harmonic power passes -60 dB at pitch ratio "
            << onset << " (design: Nyquist / (12 harmonics * " << top << " Hz) = " << 24000.0 / (12.0 * top) << ")\n";
}

void writePwm(std::ofstream& out, const jp::WaveTableBank& bank)
{
    constexpr double f0{750.0};
    out << "@New plot: title=\"saw at 750 Hz, harmonic level\" labelx=\"harmonic\" "
           "labely=\"dB re the plain saw's fundamental\" miny=-80 maxy=5\n";
    const std::vector<std::pair<std::string, float>> cases{
        {"no pulse width modulation", -1.0f}, {"offset 1/2 (strong)", 0.5f}, {"offset 1/4 (strong)", 0.25f}};
    double reference = 0.0;
    for (const auto& [name, offset] : cases)
    {
        jp::WaveTableOscillator oscillator(c_sampleRate, bank);
        oscillator.setWave(jp::BasicWave::Saw);
        oscillator.setFrequency(static_cast<float>(f0));
        if (offset >= 0.0f)
        {
            oscillator.setPwmMode(jp::WaveTableOscillator::PwmMode::Strong);
            oscillator.setPwm(offset);
        }
        std::vector<float> settle(2048);
        oscillator.processBlock(settle.data(), settle.size());
        std::vector<float> signal(64000);
        oscillator.processBlock(signal.data(), signal.size());
        out << "#" << name << "\n";
        for (size_t k = 1; k <= 16; ++k)
        {
            const double amplitude =
                std::abs(jp::test::toneAmplitude(signal, f0 * static_cast<double>(k), c_sampleRate));
            if (reference == 0.0)
            {
                reference = amplitude;
            }
            out << k << " " << toDb(amplitude / reference + 1e-5) << "\n";
        }
    }
}
}

int main(const int argc, const char* const* argv)
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    const jp::WaveTableBank bank(
        {jp::BasicWave::Triangle, jp::BasicWave::SharkFin, jp::BasicWave::Saw, jp::BasicWave::Square});
    std::ofstream summary(dir + "/summary.txt");
    summary << "bank of 4 waveforms: " << bank.memoryBytes() << " bytes\n";
    {
        std::ofstream out(dir + "/wt_shapes.txt");
        writeShapes(out, bank);
    }
    {
        std::ofstream out(dir + "/wt_levels.txt");
        writeLevels(out, bank, summary);
    }
    {
        std::ofstream out(dir + "/wt_aliasing.txt");
        writeAliasing(out, bank, summary);
    }
    {
        std::ofstream out(dir + "/wt_bend.txt");
        writeBend(out, bank, summary);
    }
    {
        std::ofstream out(dir + "/wt_pwm.txt");
        writePwm(out, bank);
    }
    std::cout << "wrote the Wavetables data into " << dir << "\n";
    return 0;
}
