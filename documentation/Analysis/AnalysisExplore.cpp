// Accuracy of the analysis blocks in dsp/Analysis/ for documentation/Analysis: the
// zero-crossing frequency estimate over frequency and window length, the Hann Goertzel band
// around a tone (main lobe, scallop, side lobes), ToneFit against a single Goertzel block of the
// same length, and the TransferBand block length on a steep highpass (the step 8 finding).
// Writes documentation/Plot/PyConPlot.py text files and summary.txt into the directory given
// as the only argument.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "dsp/Analysis/GoertzelBand.h"
#include "dsp/Analysis/ToneFit.h"
#include "dsp/Analysis/ZeroCrossingFrequency.h"
#include "dsp/Filters/OnePoleLowpass.h"
#include "dsp/Numbers/Random.h"

namespace
{
constexpr double c_pi{3.14159265358979323846};
constexpr double c_fs{48000.0};

double toDb(const double x)
{
    return 20.0 * std::log10(std::max(x, 1e-15));
}

std::vector<float> sine(const double frequency, const double amplitude, const size_t length, const double phase = 0.3)
{
    std::vector<float> out(length);
    for (size_t i = 0; i < length; ++i)
    {
        out[i] =
            static_cast<float>(amplitude * std::sin(2.0 * c_pi * frequency * static_cast<double>(i) / c_fs + phase));
    }
    return out;
}

/// Log-spaced frequencies from `from` to `to`, `perOctave` points per octave.
std::vector<double> logSpaced(const double from, const double to, const int perOctave)
{
    std::vector<double> values;
    for (double f = from; f <= to * 1.0001; f *= std::pow(2.0, 1.0 / perOctave))
    {
        values.push_back(f);
    }
    return values;
}

void writeFrequencyError(std::ofstream& out, std::ofstream& summary)
{
    // relative error of ZeroCrossingFrequency for a clean sine and for one with white noise
    // 60 dB below it, over 0.1 s and 1 s windows
    out << "@New plot: title=\"zero-crossing frequency, clean sine\" logx=true hzticks=true labely=\"error (ppm)\" "
           "miny=-50 maxy=50\n";
    for (const double seconds : {0.1, 1.0})
    {
        out << "#window " << seconds << " s\n";
        double worst = 0.0;
        for (const double f : logSpaced(20.0, 20000.0, 6))
        {
            const auto x = sine(f, 0.5, static_cast<size_t>(seconds * c_fs));
            jp::ZeroCrossingFrequency counter(c_fs, 1e-3f);
            counter.add(x.data(), x.size());
            const double ppm = counter.frequency() > 0.0 ? (counter.frequency() / f - 1.0) * 1e6 : 0.0;
            worst = std::max(worst, std::abs(ppm));
            out << f << " " << ppm << "\n";
        }
        summary << "zero crossings, clean sine, window " << seconds << " s: worst error " << worst
                << " ppm (20 Hz to 20 kHz)\n";
    }
    out << "@New plot: title=\"zero-crossing frequency, sine plus noise at -60 dB\" logx=true hzticks=true "
           "labely=\"error (ppm)\" miny=-200 maxy=200\n";
    for (const double seconds : {0.1, 1.0})
    {
        out << "#window " << seconds << " s\n";
        double worst = 0.0;
        for (const double f : logSpaced(20.0, 20000.0, 6))
        {
            auto x = sine(f, 0.5, static_cast<size_t>(seconds * c_fs));
            jp::Random random(9u);
            for (auto& v : x)
            {
                v += 0.5f * 1e-3f * (2.0f * random.uniform() - 1.0f);
            }
            jp::ZeroCrossingFrequency counter(c_fs, 1e-2f);
            counter.add(x.data(), x.size());
            const double ppm = counter.frequency() > 0.0 ? (counter.frequency() / f - 1.0) * 1e6 : 0.0;
            worst = std::max(worst, std::abs(ppm));
            out << f << " " << ppm << "\n";
        }
        summary << "zero crossings, noise at -60 dB, window " << seconds << " s: worst error " << worst << " ppm\n";
    }
}

void writeGoertzelLeakage(std::ofstream& out, std::ofstream& summary)
{
    // reading of a band at 1 kHz plus an offset, for a sine of amplitude 1 at exactly the bin
    // centre nearest 1 kHz; block 16384 as in the meter
    constexpr uint32_t block{16384};
    const double bin = c_fs / block;
    const double tone = std::round(1000.0 / bin) * bin;
    const auto x = sine(tone, 1.0, block * 4);
    out << "@New plot: title=\"Hann Goertzel band around a tone (block 16384)\" labelx=\"band offset (bins)\" "
           "labely=\"reading (dB)\" miny=-120 maxy=5\n#reading\n";
    double scallop = 0.0;
    double sidelobe = -400.0;
    for (int k = -800; k <= 800; ++k)
    {
        const double offset = k / 100.0;
        jp::GoertzelBand band(tone + offset * bin, c_fs, block);
        for (const float v : x)
        {
            band.add(v);
        }
        const double reading = toDb(band.amplitude());
        out << offset << " " << reading << "\n";
        if (k == 50)
        {
            scallop = reading;
        }
        if (std::abs(offset) > 2.2 && std::abs(offset) < 3.8)
        {
            sidelobe = std::max(sidelobe, reading);
        }
    }
    summary << "Goertzel (Hann, 16384): half a bin off reads " << scallop << " dB; highest side lobe " << sidelobe
            << " dB\n";
}

void writeToneFitAgainstGoertzel(std::ofstream& out, std::ofstream& summary)
{
    // amplitude error of a sine at a frequency that is not on a bin centre, over the window
    // length: ToneFit fits the sine exactly, a single Goertzel block of the same length reads
    // the windowed DTFT (scallop and leakage of the window's own edges)
    const double f = 1234.5;
    out << "@New plot: title=\"amplitude error of a 1234.5 Hz sine over the window length\" logx=true "
           "labelx=\"window (samples)\" labely=\"|error| (dB re amplitude)\" miny=-200 maxy=0\n";
    out << "#ToneFit\n";
    std::vector<size_t> lengths;
    for (double n = 64.0; n <= 200000.0; n *= std::pow(2.0, 0.25))
    {
        lengths.push_back(static_cast<size_t>(n));
    }
    double worstFit = -400.0;
    for (const size_t n : lengths)
    {
        const auto x = sine(f, 0.5, n);
        jp::ToneFit fit(f, c_fs);
        fit.add(x.data(), x.size());
        const double error = toDb(std::abs(fit.result().amplitude / 0.5 - 1.0));
        worstFit = std::max(worstFit, error);
        out << n << " " << error << "\n";
    }
    out << "#Goertzel, one block\n";
    for (const size_t n : lengths)
    {
        const auto x = sine(f, 0.5, n);
        jp::GoertzelBand band(f, c_fs, static_cast<uint32_t>(n));
        for (const float v : x)
        {
            band.add(v);
        }
        out << n << " " << toDb(std::abs(band.amplitude() / 0.5 - 1.0)) << "\n";
    }
    summary << "ToneFit amplitude error, 64 to 200000 samples: at most " << worstFit << " dB re amplitude\n";
}

void writeTransferBlockLength(std::ofstream& out, std::ofstream& summary)
{
    // four one-pole highpass stages at 1 kHz (x - lowpass), measured from 16 s of white noise
    // with TransferBand at several block lengths, against the exact response
    const float pole = jp::onePoleLowpassPole(1000.0f, static_cast<float>(c_fs));
    std::array<jp::OnePoleLowpass, 4> stages;
    for (auto& stage : stages)
    {
        stage.setPole(pole);
    }
    jp::Random random(21u);
    const size_t length = static_cast<size_t>(16.0 * c_fs);
    std::vector<float> input(length);
    std::vector<float> output(length);
    for (size_t i = 0; i < length; ++i)
    {
        input[i] = 0.25f * (2.0f * random.uniform() - 1.0f);
        float y = input[i];
        for (auto& stage : stages)
        {
            y = y - stage.process(y);
        }
        output[i] = y;
    }
    const std::vector<double> probes = logSpaced(31.5, 16000.0, 3);
    const std::array<uint32_t, 4> blocks{4096u, 8192u, 16384u, 32768u};
    std::array<std::vector<double>, 4> deviations;
    std::array<std::vector<double>, 4> coherences;
    for (size_t b = 0; b < blocks.size(); ++b)
    {
        std::vector<jp::TransferBand> bands;
        for (const double f : probes)
        {
            bands.emplace_back(f, c_fs, blocks[b]);
        }
        for (size_t i = 0; i < length; ++i)
        {
            for (auto& band : bands)
            {
                band.add(input[i], output[i]);
            }
        }
        double worst = 0.0;
        double lowestCoherence = 1.0;
        for (size_t k = 0; k < probes.size(); ++k)
        {
            const std::complex<double> lowpass = jp::onePoleLowpassResponse(pole, probes[k], c_fs);
            const double exact = toDb(std::pow(std::abs(1.0 - lowpass), 4.0));
            deviations[b].push_back(toDb(bands[k].gain()) - exact);
            coherences[b].push_back(bands[k].coherence());
            if (exact > -60.0)
            {
                worst = std::max(worst, std::abs(deviations[b].back()));
                lowestCoherence = std::min(lowestCoherence, bands[k].coherence());
            }
        }
        summary << "TransferBand, 4-pole highpass at 1 kHz, block " << blocks[b] << ": worst deviation " << worst
                << " dB, lowest coherence " << lowestCoherence << " where the response is above -60 dB\n";
    }
    out << "@New plot: title=\"TransferBand on a 4-pole highpass at 1 kHz: measured - exact\" logx=true "
           "hzticks=true labely=\"deviation (dB)\" miny=-1 maxy=1\n";
    for (size_t b = 0; b < blocks.size(); ++b)
    {
        out << "#block " << blocks[b] << "\n";
        for (size_t k = 0; k < probes.size(); ++k)
        {
            out << probes[k] << " " << deviations[b][k] << "\n";
        }
    }
    out << "@New plot: title=\"coherence\" logx=true hzticks=true labely=\"coherence\" miny=0.9 maxy=1.005\n";
    for (size_t b = 0; b < blocks.size(); ++b)
    {
        out << "#block " << blocks[b] << "\n";
        for (size_t k = 0; k < probes.size(); ++k)
        {
            out << probes[k] << " " << coherences[b][k] << "\n";
        }
    }
}
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: AnalysisExplore OUTPUT_DIR\n";
        return 1;
    }
    const std::string dir = argv[1];
    std::ofstream summary(dir + "/summary.txt");
    {
        std::ofstream out(dir + "/an_frequency.txt");
        writeFrequencyError(out, summary);
    }
    {
        std::ofstream out(dir + "/an_goertzel.txt");
        writeGoertzelLeakage(out, summary);
    }
    {
        std::ofstream out(dir + "/an_tonefit.txt");
        writeToneFitAgainstGoertzel(out, summary);
    }
    {
        std::ofstream out(dir + "/an_transfer_blocks.txt");
        writeTransferBlockLength(out, summary);
    }
    return 0;
}
