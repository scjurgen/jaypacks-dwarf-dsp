// Charts of jp::PoleMixingFilter (dsp/Filters/PoleMixingFilter.h) and its one-pole stage for
// documentation/Filters/PoleMixing: responses of the 15 shipahoy menu entries (theory and a
// measured check), phaser phase, the menu mapping fix, stage response across sample rates,
// oscillation threshold and peak tuning, resonance up to self-oscillation, frequency jumps
// against glides, and the +-1 stage clamp at high input levels. Method after AbacDsp
// documentation/Filters/PoleMixing (see docs/references.md); the code is new.
// Writes documentation/Plot/PyConPlot.py text files into the directory given as argument.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "dsp/Filters/OnePoleLowpass.h"
#include "dsp/Filters/PoleMixingFilter.h"
#include "plugins/shipahoy/ShipAhoyEngine.h"
#include "tests/unit/TestHelpers.h"

namespace
{
using jp::ShipAhoyEngine;
constexpr double c_pi{3.14159265358979323846};
constexpr float c_fs{48000.0f};
constexpr float c_cutoff{1000.0f};

/// Log-spaced frequencies from 20 Hz to 20 kHz.
std::vector<double> frequencies(const int perOctave = 24)
{
    std::vector<double> out;
    for (double f = 20.0; f <= 20000.0; f *= std::pow(2.0, 1.0 / perOctave))
    {
        out.push_back(f);
    }
    return out;
}

double responseDb(const size_t preset, const double pole, const double resonance, const double f,
                  const double fs = c_fs)
{
    return jp::test::toDb(std::abs(jp::poleMixingResponse(jp::c_poleMixingPresets[preset], pole, resonance, f, fs)));
}

void writeResponse(std::ofstream& out, const std::string& name, const size_t preset, const double pole,
                   const double resonance = 0.0)
{
    out << "#" << name << "\n";
    for (const double f : frequencies())
    {
        out << f << " " << std::max(responseDb(preset, pole, resonance, f), -80.0) << "\n";
    }
}

/// Largest |measured - theory| in dB over a third-octave grid (steady-state sine probe),
/// only where the theory is above -60 dB.
double measuredDeviationDb(const size_t preset)
{
    double worst = 0.0;
    const double pole = jp::onePoleLowpassPole(c_cutoff, c_fs);
    for (double f = 25.0; f < 16000.0; f *= std::pow(2.0, 1.0 / 3.0))
    {
        jp::PoleMixingFilter filter;
        filter.setCutoff(c_cutoff);
        filter.setPreset(preset);
        const double measured = jp::test::toDb(
            jp::test::measureSineGain([&filter](const float x) { return filter.process(x); }, f, c_fs, 0.01, 0.3, 0.5));
        const double theory = responseDb(preset, pole, 0.0, f);
        if (theory > -60.0)
        {
            worst = std::max(worst, std::abs(measured - theory));
        }
    }
    return worst;
}

void writeMenuResponses(std::ofstream& out, std::ofstream& summary)
{
    const double pole = jp::onePoleLowpassPole(c_cutoff, c_fs);
    const std::array<std::pair<const char*, std::vector<size_t>>, 4> groups{{
        {"lowpass", {0, 1, 2, 3}},
        {"bandpass and notch", {4, 5, 10, 11}},
        {"highpass", {6, 7, 8, 9}},
        {"phaser", {12, 13, 14}},
    }};
    double worst = 0.0;
    for (const auto& [title, entries] : groups)
    {
        double groupWorst = 0.0;
        for (const size_t entry : entries)
        {
            groupWorst = std::max(groupWorst, measuredDeviationDb(ShipAhoyEngine::c_filterTypePresets[entry]));
        }
        worst = std::max(worst, groupWorst);
        out << "@New plot: title=\"" << title << " (measured "
            << (groupWorst < 0.001 ? "< 0.001" : std::to_string(groupWorst))
            << " dB from the theory)\" logx=true hzticks=true miny=-60 maxy=6 ystep=12\n";
        for (const size_t entry : entries)
        {
            writeResponse(out, ShipAhoyEngine::c_filterTypeLabels[entry], ShipAhoyEngine::c_filterTypePresets[entry],
                          pole);
        }
    }
    summary << "menu responses: measured vs theory within " << worst << " dB (where the theory is above -60 dB)\n";
}

void writePhaserPhase(std::ofstream& out)
{
    const double pole = jp::onePoleLowpassPole(c_cutoff, c_fs);
    out << "@New plot: title=\"phase of the phaser entries (stage cutoff 1 kHz)\" logx=true hzticks=true\n";
    for (const size_t entry : {12u, 13u, 14u})
    {
        out << "#" << ShipAhoyEngine::c_filterTypeLabels[entry] << "\n";
        double previous = 0.0;
        double unwrap = 0.0;
        for (const double f : frequencies())
        {
            const double phase = std::arg(jp::poleMixingResponse(
                jp::c_poleMixingPresets[ShipAhoyEngine::c_filterTypePresets[entry]], pole, 0.0, f, c_fs));
            if (phase - previous > c_pi)
            {
                unwrap -= 2.0 * c_pi;
            }
            else if (phase - previous < -c_pi)
            {
                unwrap += 2.0 * c_pi;
            }
            previous = phase;
            out << f << " " << (phase + unwrap) * 180.0 / c_pi << "\n";
        }
    }
}

/// The old plugin mapped menu index i to table row i; the new one maps by name. The old menu
/// had a 16th entry "1 pole phaser" at index 12 (table row "* 1 pole phaser", removed as a copy of "1 pole hp").
void writeMenuMapping(std::ofstream& out, std::ofstream& summary)
{
    const double pole = jp::onePoleLowpassPole(c_cutoff, c_fs);
    std::vector<size_t> oldMenuRows(ShipAhoyEngine::c_filterTypePresets.begin(),
                                    ShipAhoyEngine::c_filterTypePresets.end());
    oldMenuRows.insert(oldMenuRows.begin() + 12, jp::c_poleMixingPresetNotFound);
    int wrong = 0;
    for (size_t i = 0; i < oldMenuRows.size(); ++i)
    {
        wrong += oldMenuRows[i] != i ? 1 : 0;
    }
    summary << "menu mapping: " << wrong << " of " << oldMenuRows.size()
            << " old menu entries played another table row\n";
    for (size_t i = 0; i < ShipAhoyEngine::c_filterTypeCount; ++i)
    {
        for (size_t k = i + 1; k < ShipAhoyEngine::c_filterTypeCount; ++k)
        {
            const auto& a = jp::c_poleMixingPresets[ShipAhoyEngine::c_filterTypePresets[i]];
            const auto& b = jp::c_poleMixingPresets[ShipAhoyEngine::c_filterTypePresets[k]];
            if (a.inputWeight == b.inputWeight && a.stageWeights == b.stageWeights &&
                a.resonanceFeedback == b.resonanceFeedback && a.resonanceInputGain == b.resonanceInputGain)
            {
                summary << "menu duplicate: '" << ShipAhoyEngine::c_filterTypeLabels[k] << "' (" << b.name
                        << ") sounds exactly like '" << ShipAhoyEngine::c_filterTypeLabels[i] << "' (" << a.name
                        << ")\n";
            }
        }
    }
    for (const size_t entry : {4u, 6u, 9u, 10u})
    {
        out << "@New plot: title=\"menu '" << ShipAhoyEngine::c_filterTypeLabels[entry]
            << "'\" logx=true hzticks=true miny=-60 maxy=6 ystep=12\n";
        writeResponse(out,
                      std::string("new: ") + jp::c_poleMixingPresets[ShipAhoyEngine::c_filterTypePresets[entry]].name,
                      ShipAhoyEngine::c_filterTypePresets[entry], pole);
        writeResponse(out, std::string("old: ") + jp::c_poleMixingPresets[entry].name, entry, pole);
    }
}

double minusThreeDbRatio(const double cutoffRatio)
{
    // |H|^2 = 1/2  <=>  cos(w) = (1 + p^2 - 2 (1 - p)^2) / (2 p)
    const double p = std::exp(-2.0 * c_pi * cutoffRatio);
    const double cosW = (1.0 + p * p - 2.0 * (1.0 - p) * (1.0 - p)) / (2.0 * p);
    return std::acos(std::clamp(cosW, -1.0, 1.0)) / (2.0 * c_pi) / cutoffRatio;
}

void writeStage(std::ofstream& out, std::ofstream& summary)
{
    out << "@New plot: title=\"one-pole stage at 1 kHz and 8 kHz\" logx=true hzticks=true miny=-30 maxy=1 "
           "labelx=\"frequency (Hz)\" labely=\"magnitude (dB)\"\n";
    for (const double fc : {1000.0, 8000.0})
    {
        for (const double fs : {44100.0, 48000.0, 96000.0})
        {
            const double pole = jp::onePoleLowpassPole(static_cast<float>(fc), static_cast<float>(fs));
            out << "#fc " << fc << ", fs " << fs << "\n";
            for (const double f : frequencies())
            {
                if (f < 0.5 * fs)
                {
                    out << f << " " << jp::test::toDb(std::abs(jp::onePoleLowpassResponse(pole, f, fs))) << "\n";
                }
            }
        }
        out << "#analog fc " << fc << "\n";
        for (const double f : frequencies())
        {
            out << f << " " << jp::test::toDb(1.0 / std::sqrt(1.0 + (f / fc) * (f / fc))) << "\n";
        }
    }
    out << "@New plot: title=\"-3 dB frequency / fc over fc / fs\" labelx=\"fc / fs\" labely=\"ratio\"\n#-3 dB / fc\n";
    for (double ratio = 0.001; ratio <= 0.3; ratio *= 1.05)
    {
        out << ratio << " " << minusThreeDbRatio(ratio) << "\n";
    }
    for (const double ratio : {1.0 / 16.0, 1.0 / 8.0, 1.0 / 4.0})
    {
        summary << "one-pole -3 dB at fc = fs * " << ratio << ": " << minusThreeDbRatio(ratio) << " * fc\n";
    }
}

/// AbacDsp FourStageFilter::warpCutoffForSampleRate() at 48 kHz.
double warp48(const double x)
{
    return 0.1409743683 + 0.9999793344 * x - 6.203395634e-05 * x * x + 2.855230937e-09 * x * x * x;
}

double stageCutoffForPeak(const double peak, const double fs = c_fs)
{
    const double pole = jp::poleMixingTuningForPeak(static_cast<float>(peak), static_cast<float>(fs)).pole;
    return -std::log(pole) * fs / (2.0 * c_pi);
}

void writeThreshold(std::ofstream& out, std::ofstream& summary)
{
    out << "@New plot: title=\"self-oscillation threshold r * a over the stage cutoff (fs 48 kHz)\" logx=true "
           "hzticks=true labelx=\"stage cutoff (Hz)\" labely=\"loop gain\" miny=0 maxy=20\n#threshold\n";
    for (double fc = 20.0; fc < 23000.0; fc *= 1.03)
    {
        out << fc << " " << jp::poleMixingOscillationThreshold(jp::onePoleLowpassPole(static_cast<float>(fc), c_fs))
            << "\n";
    }
    out << "#analog ladder: 4\n20 4\n23000 4\n";
    out << "@New plot: title=\"resonance peak and stage cutoff (fs 48 kHz)\" logx=true hzticks=true logy=true "
           "labelx=\"set frequency (Hz)\" labely=\"frequency (Hz)\" legendpos=\"upper left\"\n";
    out << "#peak when the stage cutoff is set (untuned)\n";
    for (double peak = 20.0; peak < 22000.0; peak *= 1.03)
    {
        out << stageCutoffForPeak(peak) << " " << peak << "\n";
    }
    out << "#stage cutoff for a peak at the set frequency (poleMixingTuningForPeak)\n";
    for (double peak = 20.0; peak < 22000.0; peak *= 1.03)
    {
        out << peak << " " << stageCutoffForPeak(peak) << "\n";
    }
    out << "#AbacDsp warpCutoffForSampleRate (48 kHz fit)\n";
    for (double peak = 20.0; peak < 12000.0; peak *= 1.03)
    {
        out << peak << " " << warp48(peak) << "\n";
    }
    out << "#y = x\n20 20\n20000 20000\n";
    for (const double peak : {1000.0, 3000.0, 6000.0, 8000.0, 12000.0})
    {
        summary << "tuning: peak " << peak << " Hz -> stage cutoff " << stageCutoffForPeak(peak) << " Hz, AbacDsp warp "
                << warp48(peak) << " Hz\n";
    }
}

double responsePeakRatio(const double target, const double fraction, const double fs)
{
    const auto& preset = jp::c_poleMixingPresets[jp::findPoleMixingPreset("4 pole lp")];
    const jp::PoleMixingTuning tuning = jp::poleMixingTuningForPeak(static_cast<float>(target), static_cast<float>(fs));
    const double resonance = fraction * tuning.oscillationThreshold / preset.resonanceFeedback;
    double best = 0.0;
    double bestF = 0.0;
    for (double f = 0.5 * target; f < 1.2 * target && f < 0.5 * fs; f *= 1.0002)
    {
        const double magnitude = std::abs(jp::poleMixingResponse(preset, tuning.pole, resonance, f, fs));
        if (magnitude > best)
        {
            best = magnitude;
            bestF = f;
        }
    }
    return bestF / target;
}

void writeResonance(std::ofstream& out, std::ofstream& summary)
{
    const size_t lp4 = jp::findPoleMixingPreset("4 pole lp");
    const float a = jp::c_poleMixingPresets[lp4].resonanceFeedback;
    const jp::PoleMixingTuning tuning = jp::poleMixingTuningForPeak(c_cutoff, c_fs);
    out << "@New plot: title=\"4 pole lp tuned to 1 kHz, resonance in % of the threshold\" logx=true hzticks=true "
           "miny=-60 maxy=40 ystep=12 labelx=\"frequency (Hz)\" labely=\"magnitude (dB)\"\n";
    for (const double fraction : {0.0, 0.5, 0.8, 0.95})
    {
        writeResponse(out, std::to_string(static_cast<int>(fraction * 100)) + " %", lp4, tuning.pole,
                      fraction * tuning.oscillationThreshold / a);
    }
    out << "@New plot: title=\"response peak / set frequency over the resonance (fs 48 kHz)\" "
           "labelx=\"resonance (fraction of the threshold)\" labely=\"ratio\" miny=0.9 maxy=1.01\n";
    for (const double target : {100.0, 1000.0, 3000.0, 8000.0})
    {
        out << "#" << target << " Hz\n";
        for (double fraction = 0.5; fraction < 0.995; fraction += 0.01)
        {
            out << fraction << " " << responsePeakRatio(target, fraction, c_fs) << "\n";
        }
    }
    for (const double fraction : {0.85, 0.95})
    {
        double lo = 1.0;
        double hi = 0.0;
        for (const double fs : {44100.0, 48000.0, 96000.0})
        {
            for (double target = 100.0; target <= fs / 6.0; target *= 1.5)
            {
                const double r = responsePeakRatio(target, fraction, fs);
                lo = std::min(lo, r);
                hi = std::max(hi, r);
            }
        }
        summary << "response peak at " << fraction * 100 << " % resonance: " << (1.0 - hi) * 100 << " to "
                << (1.0 - lo) * 100 << " % below the set frequency (100 Hz to fs/6, 44.1/48/96 kHz)\n";
    }
    out << "@New plot: title=\"impulse 1e-4 at 101 % of the threshold: growth, then held by the stage clamp\" "
           "labelx=\"time (s)\" labely=\"peak per 5 ms\"\n#4 pole lp at 1 kHz\n";
    jp::PoleMixingFilter filter;
    filter.setPreset(lp4);
    filter.setPole(tuning.pole);
    filter.setResonance(1.01f * tuning.oscillationThreshold / a);
    float peak = 0.0f;
    for (int i = 0; i < 48000 * 2; ++i)
    {
        peak = std::max(peak, std::abs(filter.process(i == 0 ? 1e-4f : 0.0f)));
        if ((i + 1) % 240 == 0)
        {
            out << (i + 1) / c_fs << " " << peak << "\n";
            peak = 0.0f;
        }
    }
}

/// 1.5 kHz probe through 4 pole lp at 80 %: the set frequency jumps 800 -> 3000 Hz at 50 ms,
/// once in one sample and as glides in octaves (as shipahoy does).
void writeGlide(std::ofstream& out)
{
    const size_t lp4 = jp::findPoleMixingPreset("4 pole lp");
    const float a = jp::c_poleMixingPresets[lp4].resonanceFeedback;
    out << "@New plot: title=\"1.5 kHz probe, 4 pole lp at 80 %, set frequency 800 -> 3000 Hz at 50 ms\" "
           "labelx=\"time (s)\" labely=\"peak per 1 ms\"\n";
    for (const float glideMs : {0.0f, 5.0f, 20.0f})
    {
        jp::PoleMixingFilter filter;
        filter.setPreset(lp4);
        out << "#" << (glideMs == 0.0f ? std::string("jump") : std::to_string(static_cast<int>(glideMs)) + " ms glide")
            << "\n";
        const auto start = static_cast<int>(0.05f * c_fs);
        const auto glideSamples = static_cast<int>(glideMs * 0.001f * c_fs);
        float peak = 0.0f;
        for (int i = 0; i < static_cast<int>(0.15f * c_fs); ++i)
        {
            float octaves = std::log2(800.0f);
            if (i >= start)
            {
                const float progress =
                    glideSamples == 0 ? 1.0f : std::min(1.0f, static_cast<float>(i - start) / glideSamples);
                octaves += progress * (std::log2(3000.0f) - std::log2(800.0f));
            }
            const jp::PoleMixingTuning tuning = jp::poleMixingTuningForPeak(std::exp2(octaves), c_fs);
            filter.setPole(tuning.pole);
            filter.setResonance(0.8f * tuning.oscillationThreshold / a);
            const float x = 0.1f * static_cast<float>(std::sin(2.0 * c_pi * 1500.0 * i / c_fs));
            peak = std::max(peak, std::abs(filter.process(x)));
            if ((i + 1) % 48 == 0)
            {
                out << (i + 1) / c_fs << " " << peak << "\n";
                peak = 0.0f;
            }
        }
    }
}

void writeClamp(std::ofstream& out, std::ofstream& summary)
{
    const size_t lp4 = jp::findPoleMixingPreset("4 pole lp");
    out << "@New plot: title=\"300 Hz sine through 4 pole lp (stage cutoff 5 kHz, no resonance)\" "
           "labelx=\"time (ms)\" labely=\"output\"\n";
    for (const float amplitude : {0.5f, 1.0f, 2.0f, 4.0f})
    {
        jp::PoleMixingFilter filter;
        filter.setPreset(lp4);
        filter.setCutoff(5000.0f);
        out << "#input amplitude " << amplitude << "\n";
        for (int i = 0; i < 4800 + 480; ++i)
        {
            const float y = filter.process(amplitude * static_cast<float>(std::sin(2.0 * c_pi * 300.0 * i / c_fs)));
            if (i >= 4800)
            {
                out << (i - 4800) / 48.0 << " " << y << "\n";
            }
        }
    }
    out << "@New plot: title=\"harmonic distortion of the clamp (300 Hz)\" logx=true labelx=\"input amplitude\" "
           "labely=\"THD (%)\"\n#4 pole lp, stage cutoff 5 kHz\n";
    for (const double amplitude : {0.25, 0.5, 0.75, 0.9, 1.0, 1.1, 1.25, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0})
    {
        jp::PoleMixingFilter filter;
        filter.setPreset(lp4);
        filter.setCutoff(5000.0f);
        std::vector<float> y;
        for (int i = 0; i < 48000; ++i)
        {
            const float v = filter.process(static_cast<float>(amplitude * std::sin(2.0 * c_pi * 300.0 * i / c_fs)));
            if (i >= 24000)
            {
                y.push_back(v);
            }
        }
        const double fundamental = std::abs(jp::test::toneAmplitude(y, 300.0, c_fs));
        double harmonics = 0.0;
        for (int h = 2; h <= 15; ++h)
        {
            harmonics += std::norm(jp::test::toneAmplitude(y, 300.0 * h, c_fs));
        }
        const double thd = 100.0 * std::sqrt(harmonics) / fundamental;
        out << amplitude << " " << thd << "\n";
        summary << "clamp THD at input amplitude " << amplitude << ": " << thd << " %\n";
    }
}
}

int main(int argc, char* argv[])
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    std::ofstream response(dir + "/pm_menu_response.txt");
    std::ofstream phase(dir + "/pm_phaser_phase.txt");
    std::ofstream mapping(dir + "/pm_menu_mapping.txt");
    std::ofstream stage(dir + "/pm_stage.txt");
    std::ofstream threshold(dir + "/pm_threshold.txt");
    std::ofstream resonance(dir + "/pm_resonance.txt");
    std::ofstream glide(dir + "/pm_glide.txt");
    std::ofstream clamp(dir + "/pm_clamp.txt");
    std::ofstream summary(dir + "/summary.txt");
    if (!response || !phase || !mapping || !stage || !threshold || !resonance || !glide || !clamp || !summary)
    {
        std::cerr << "PoleMixingExplore: cannot write to " << dir << "\n";
        return 1;
    }
    writeMenuResponses(response, summary);
    writePhaserPhase(phase);
    writeMenuMapping(mapping, summary);
    writeStage(stage, summary);
    writeThreshold(threshold, summary);
    writeResonance(resonance, summary);
    writeGlide(glide);
    writeClamp(clamp, summary);
    std::cout << "PoleMixingExplore: wrote 8 plot files and summary.txt to " << dir << "\n";
    return 0;
}
