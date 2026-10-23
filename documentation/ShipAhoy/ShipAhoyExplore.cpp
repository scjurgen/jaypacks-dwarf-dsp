// Charts of jp::ShipAhoyEngine (plugins/shipahoy/ShipAhoyEngine.h) for documentation/ShipAhoy:
// the "Ship Ahoy" preset at work (frequency steps, the resonance peak riding on them, band
// energies of noise and of a sawtooth through it), hop trajectories and distributions per
// random type, glide shapes, the mix law and the additive drift. Writes documentation/Plot/PyConPlot.py text
// files into the directory given as argument.

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "dsp/Filters/PoleMixingFilter.h"
#include "dsp/Numbers/PanLaw.h"
#include "dsp/Numbers/Random.h"
#include "plugins/shipahoy/ShipAhoyEngine.h"
#include "tests/unit/ShipAhoyClockedEngine.h"

namespace
{
// the engine with the hop clock of the knob mode before the plugin took its steps from the tempo clock
using ShipAhoyEngine = jp::test::ClockedEngine;
constexpr double c_pi{3.14159265358979323846};
constexpr float c_fs{48000.0f};
constexpr std::array<const char*, ShipAhoyEngine::c_randomTypeCount> c_randomNames{"white", "toggle", "violet", "brown",
                                                                                   "gauss"};

/// RBJ constant-peak bandpass (0 dB at the centre), one octave wide: a band meter for the charts.
class OctaveBand
{
  public:
    explicit OctaveBand(const double centre)
    {
        const double w = 2.0 * c_pi * centre / c_fs;
        const double alpha = std::sin(w) * std::sinh(std::log(2.0) / 2.0 * 1.0 * w / std::sin(w));
        const double a0 = 1.0 + alpha;
        m_b0 = alpha / a0;
        m_b2 = -alpha / a0;
        m_a1 = -2.0 * std::cos(w) / a0;
        m_a2 = (1.0 - alpha) / a0;
    }

    double process(const double x)
    {
        const double y = m_b0 * x + m_b2 * m_x2 - m_a1 * m_y1 - m_a2 * m_y2;
        m_x2 = m_x1;
        m_x1 = x;
        m_y2 = m_y1;
        m_y1 = y;
        return y;
    }

  private:
    double m_b0{0}, m_b2{0}, m_a1{0}, m_a2{0};
    double m_x1{0}, m_x2{0}, m_y1{0}, m_y2{0};
};

ShipAhoyEngine shipAhoyPreset(const uint64_t seed)
{
    ShipAhoyEngine pedal(seed); // the defaults are the "Ship Ahoy" preset
    pedal.setSampleRate(c_fs);
    pedal.reset();
    return pedal;
}

void writeBandEnergies(std::ofstream& out, const std::string& title, const bool sawtooth)
{
    ShipAhoyEngine pedal = shipAhoyPreset(7u);
    std::array<OctaveBand, 5> bands{OctaveBand(250), OctaveBand(500), OctaveBand(1000), OctaveBand(2000),
                                    OctaveBand(4000)};
    constexpr std::array<const char*, 5> names{"250 Hz", "500 Hz", "1 kHz", "2 kHz", "4 kHz"};
    constexpr int window{480}; // 10 ms
    const auto length = static_cast<int>(2.0f * c_fs);
    std::vector<std::vector<std::pair<double, double>>> series(bands.size());
    std::array<double, 5> energy{};
    jp::Random random(11u);
    for (int i = 0; i < length; ++i)
    {
        const float x = sawtooth ? 0.3f * static_cast<float>(2.0 * std::fmod(110.0 * i / c_fs, 1.0) - 1.0)
                                 : 0.3f * (2.0f * random.uniform() - 1.0f);
        float y = 0.0f;
        pedal.process(&x, &y, 1);
        for (size_t b = 0; b < bands.size(); ++b)
        {
            const double v = bands[b].process(y);
            energy[b] += v * v;
        }
        if ((i + 1) % window == 0)
        {
            for (size_t b = 0; b < bands.size(); ++b)
            {
                const double db = 10.0 * std::log10(std::max(energy[b] / window, 1e-12));
                series[b].emplace_back((i + 1) / c_fs, db);
                energy[b] = 0.0;
            }
        }
    }
    out << "@New plot: title=\"" << title << "\" labelx=\"time (s)\" labely=\"band level (dB)\" miny=-70\n";
    for (size_t b = 0; b < bands.size(); ++b)
    {
        out << "#" << names[b] << "\n";
        for (const auto& [t, db] : series[b])
        {
            out << t << " " << db << "\n";
        }
    }
}

void writePreset(std::ofstream& out, std::ofstream& summary)
{
    ShipAhoyEngine pedal = shipAhoyPreset(7u);
    out << "@New plot: title=\"Ship Ahoy preset: set frequency (7 Hz hops, range 3 octaves around 1 kHz, glide "
           "5 %)\" labelx=\"time (s)\" labely=\"frequency (Hz)\" logy=true\n#frequency\n";
    std::vector<float> hopFrequencies;
    uint64_t hops = 0;
    const float zero = 0.0f;
    float sink = 0.0f;
    for (int i = 0; i < static_cast<int>(2.0f * c_fs); ++i)
    {
        pedal.process(&zero, &sink, 1);
        if (i % 48 == 0)
        {
            out << i / c_fs << " " << pedal.getPeakFrequency() << "\n";
        }
        if (pedal.getHopCount() != hops)
        {
            hops = pedal.getHopCount();
            hopFrequencies.push_back(1000.0f * std::exp2(3.0f * (pedal.getTarget() - 0.5f)));
        }
    }
    summary << "Ship Ahoy preset: " << hops << " hops in 2 s\n";
    // the resonance peak riding on the first hops: linear response of the pedal's filter
    const size_t lp12 = ShipAhoyEngine::c_filterTypePresets[1];
    const auto& preset = jp::c_poleMixingPresets[lp12];
    out << "@New plot: title=\"response at the first five hop frequencies (lp 12 dB, resonance 85 %)\" logx=true "
           "hzticks=true labelx=\"frequency (Hz)\" labely=\"magnitude (dB)\" miny=-40 maxy=24 ystep=12\n";
    for (size_t h = 0; h < 5 && h < hopFrequencies.size(); ++h)
    {
        const jp::PoleMixingTuning tuning = jp::poleMixingTuningForPeak(hopFrequencies[h], c_fs);
        const double resonance = 0.85 * tuning.oscillationThreshold / preset.resonanceFeedback;
        out << "#hop " << h + 1 << ": " << static_cast<int>(hopFrequencies[h]) << " Hz\n";
        for (double f = 50.0; f < 16000.0; f *= std::pow(2.0, 1.0 / 48.0))
        {
            out << f << " "
                << 20.0 * std::log10(std::abs(jp::poleMixingResponse(preset, tuning.pole, resonance, f, c_fs))) << "\n";
        }
    }
    writeBandEnergies(out, "white noise through the preset: octave band levels per 10 ms", false);
    writeBandEnergies(out, "110 Hz sawtooth through the preset: octave band levels per 10 ms", true);
}

void writeRandomTypes(std::ofstream& timelines, std::ofstream& histograms, std::ofstream& summary)
{
    histograms << "@New plot: title=\"distribution of hop targets per random type (100000 hops)\" "
                  "labelx=\"position in the range\" labely=\"density\"\n";
    for (size_t type = 0; type < ShipAhoyEngine::c_randomTypeCount; ++type)
    {
        ShipAhoyEngine pedal(100u + type);
        pedal.setSampleRate(c_fs);
        pedal.setRandomType(static_cast<float>(type));
        pedal.reset();
        timelines << "@New plot: title=\"" << c_randomNames[type] << "\" logy=true miny=300 maxy=3400\n#frequency\n";
        const float zero = 0.0f;
        float sink = 0.0f;
        for (int i = 0; i < static_cast<int>(4.0f * c_fs); ++i)
        {
            pedal.process(&zero, &sink, 1);
            if (i % 48 == 0)
            {
                timelines << i / c_fs << " " << pedal.getPeakFrequency() << "\n";
            }
        }
        // distribution: hop targets at a low sample rate (the hop logic does not depend on it)
        ShipAhoyEngine fast(200u + type);
        fast.setSampleRate(1000.0f);
        fast.setSpeed(16.0f);
        fast.setRandomType(static_cast<float>(type));
        fast.reset();
        constexpr int bins{50};
        std::array<double, bins> counts{};
        constexpr int hopsWanted{100000};
        int hops = 0;
        uint64_t hopCount = 0;
        int atEdge = 0;
        while (hops < hopsWanted)
        {
            fast.process(&zero, &sink, 1);
            if (fast.getHopCount() != hopCount)
            {
                hopCount = fast.getHopCount();
                const float t = fast.getTarget();
                counts[static_cast<size_t>(std::min(bins - 1, static_cast<int>(t * bins)))] += 1.0;
                atEdge += (t <= 0.0f || t >= 1.0f) ? 1 : 0;
                ++hops;
            }
        }
        histograms << "#" << c_randomNames[type] << "\n";
        for (int b = 0; b < bins; ++b)
        {
            histograms << (b + 0.5) / bins << " " << counts[static_cast<size_t>(b)] * bins / hopsWanted << "\n";
        }
        summary << "random " << c_randomNames[type] << ": " << 100.0 * atEdge / hopsWanted
                << " % of hops at the range edge\n";
    }
}

void writeGlide(std::ofstream& out)
{
    out << "@New plot: title=\"position over three hops at 4 Hz for glide up / glide down settings\" "
           "labelx=\"time (s)\" labely=\"position\" miny=0 maxy=1\n";
    for (const auto& [up, down] : std::array<std::pair<float, float>, 4>{{{5, 5}, {50, 10}, {10, 50}, {100, 100}}})
    {
        ShipAhoyEngine pedal(31u);
        pedal.setSampleRate(c_fs);
        pedal.setSpeed(4.0f);
        pedal.setGlideUp(up);
        pedal.setGlideDown(down);
        pedal.reset();
        out << "#up " << up << " %, down " << down << " %\n";
        const float zero = 0.0f;
        float sink = 0.0f;
        for (int i = 0; i < static_cast<int>(0.75f * c_fs); ++i)
        {
            pedal.process(&zero, &sink, 1);
            if (i % 24 == 0)
            {
                out << i / c_fs << " " << pedal.getPosition() << "\n";
            }
        }
    }
}

void writeMix(std::ofstream& out)
{
    out << "@New plot: title=\"mix law (constant power)\" labelx=\"mix (%)\" labely=\"gain\" miny=0 maxy=1.5\n";
    std::vector<std::pair<int, jp::DryWet>> points;
    for (int mix = 0; mix <= 100; mix += 1)
    {
        points.emplace_back(mix, jp::constantPowerMix(mix / 50.0f - 1.0f));
    }
    out << "#dry\n";
    for (const auto& [mix, g] : points)
    {
        out << mix << " " << g.dry << "\n";
    }
    out << "#wet\n";
    for (const auto& [mix, g] : points)
    {
        out << mix << " " << g.wet << "\n";
    }
    out << "#dry^2 + wet^2 (power, uncorrelated signals)\n";
    for (const auto& [mix, g] : points)
    {
        out << mix << " " << g.dry * g.dry + g.wet * g.wet << "\n";
    }
    out << "#dry + wet (amplitude, identical signals)\n";
    for (const auto& [mix, g] : points)
    {
        out << mix << " " << g.dry + g.wet << "\n";
    }
}

/// The engine with a fixed filter (the engine alone: no script moves it) and the drift on, for the charts of the drift.
jp::ShipAhoyEngine driftEngine(const uint64_t seed, const float depth, const float rate)
{
    jp::ShipAhoyEngine engine(seed);
    engine.setSampleRate(c_fs);
    engine.setResonance(0.0f);
    engine.setModDepth(depth);
    engine.setModRate(rate);
    engine.reset();
    return engine;
}

std::vector<float> driftValues(jp::ShipAhoyEngine& engine, const size_t samples)
{
    std::vector<float> values(samples);
    const float zero = 0.0f;
    float sink = 0.0f;
    for (auto& value : values)
    {
        engine.process(&zero, &sink, 1);
        value = engine.getModulation();
    }
    return values;
}

void writeDrift(std::ofstream& timelines, std::ofstream& histogram, std::ofstream& correlation, std::ofstream& summary)
{
    constexpr float depth{3.0f};
    timelines << "@New plot: title=\"drift in semitones, depth 3, three rates\" labelx=\"time (s)\" "
                 "labely=\"semitones\"\n";
    for (const float rate : {0.25f, 1.0f, 4.0f})
    {
        jp::ShipAhoyEngine engine = driftEngine(7u, depth, rate);
        const auto values = driftValues(engine, static_cast<size_t>(8.0f * c_fs));
        timelines << "#rate " << rate << " Hz\n";
        for (size_t i = 0; i < values.size(); i += 48)
        {
            timelines << i / c_fs << " " << values[i] << "\n";
        }
    }

    histogram << "@New plot: title=\"distribution of the drift, depth 3, rate 5 Hz (60 s) against a Gaussian\" "
                 "labelx=\"semitones\" labely=\"density\"\n";
    jp::ShipAhoyEngine engine = driftEngine(8u, depth, 5.0f);
    const auto values = driftValues(engine, static_cast<size_t>(60.0f * c_fs));
    double sum = 0.0;
    double sumSquares = 0.0;
    constexpr int bins{60};
    constexpr double span{18.0};
    std::array<double, bins> counts{};
    for (const float x : values)
    {
        sum += x;
        sumSquares += static_cast<double>(x) * x;
        const double position = (x + span / 2.0) / span * bins;
        if (position >= 0.0 && position < bins)
        {
            counts[static_cast<size_t>(position)] += 1.0;
        }
    }
    const double count = static_cast<double>(values.size());
    const double mean = sum / count;
    const double deviation = std::sqrt(sumSquares / count - mean * mean);
    histogram << "#measured\n";
    for (int b = 0; b < bins; ++b)
    {
        histogram << ((b + 0.5) / bins - 0.5) * span << " " << counts[static_cast<size_t>(b)] * bins / (count * span)
                  << "\n";
    }
    histogram << "#Gaussian, standard deviation 3\n";
    for (int b = 0; b < bins; ++b)
    {
        const double x = ((b + 0.5) / bins - 0.5) * span;
        histogram << x << " " << std::exp(-x * x / (2.0 * depth * depth)) / (depth * std::sqrt(2.0 * c_pi)) << "\n";
    }
    summary << "drift depth 3, rate 5 Hz: mean " << mean << " st, standard deviation " << deviation << " st\n";

    correlation << "@New plot: title=\"autocorrelation of the drift against exp(-2 pi fc t)\" labelx=\"lag (s)\" "
                   "labely=\"correlation\"\n";
    for (const float rate : {0.5f, 2.0f, 8.0f})
    {
        jp::ShipAhoyEngine longRun = driftEngine(9u, depth, rate);
        const auto drift = driftValues(longRun, static_cast<size_t>(120.0f * c_fs));
        const size_t maxLag = static_cast<size_t>(c_fs / (2.0f * static_cast<float>(c_pi) * rate) * 3.0f);
        std::vector<double> measured;
        for (size_t lag = 0; lag <= maxLag; lag += std::max<size_t>(1, maxLag / 60))
        {
            double product = 0.0;
            double power = 0.0;
            for (size_t i = 0; i + lag < drift.size(); ++i)
            {
                product += static_cast<double>(drift[i]) * drift[i + lag];
                power += static_cast<double>(drift[i]) * drift[i];
            }
            measured.push_back(product / power);
        }
        correlation << "#measured, " << rate << " Hz\n";
        const size_t stride = std::max<size_t>(1, maxLag / 60);
        for (size_t k = 0; k < measured.size(); ++k)
        {
            correlation << static_cast<double>(k * stride) / c_fs << " " << measured[k] << "\n";
        }
        correlation << "#theory, " << rate << " Hz\n";
        for (size_t k = 0; k < measured.size(); ++k)
        {
            correlation << static_cast<double>(k * stride) / c_fs << " "
                        << std::exp(-2.0 * c_pi * rate * static_cast<double>(k * stride) / c_fs) << "\n";
        }
        const size_t lagOne = static_cast<size_t>(c_fs / (2.0f * static_cast<float>(c_pi) * rate));
        double product = 0.0;
        double power = 0.0;
        for (size_t i = 0; i + lagOne < drift.size(); ++i)
        {
            product += static_cast<double>(drift[i]) * drift[i + lagOne];
            power += static_cast<double>(drift[i]) * drift[i];
        }
        summary << "drift rate " << rate << " Hz: correlation at 1 / (2 pi fc) = " << product / power
                << " (theory 0.368)\n";
    }
}
}

int main(int argc, char* argv[])
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    std::ofstream preset(dir + "/sa_preset.txt");
    std::ofstream timelines(dir + "/sa_random_types.txt");
    std::ofstream histograms(dir + "/sa_random_distribution.txt");
    std::ofstream glide(dir + "/sa_glide.txt");
    std::ofstream mix(dir + "/sa_mix.txt");
    std::ofstream driftTime(dir + "/sa_drift_time.txt");
    std::ofstream driftDistribution(dir + "/sa_drift_distribution.txt");
    std::ofstream driftCorrelation(dir + "/sa_drift_correlation.txt");
    std::ofstream summary(dir + "/summary.txt");
    if (!driftTime || !driftDistribution || !driftCorrelation || !preset || !timelines || !histograms || !glide ||
        !mix || !summary)
    {
        std::cerr << "ShipAhoyExplore: cannot write to " << dir << "\n";
        return 1;
    }
    writePreset(preset, summary);
    writeRandomTypes(timelines, histograms, summary);
    writeGlide(glide);
    writeMix(mix);
    writeDrift(driftTime, driftDistribution, driftCorrelation, summary);
    std::cout << "ShipAhoyExplore: wrote 8 plot files and summary.txt to " << dir << "\n";
    return 0;
}
