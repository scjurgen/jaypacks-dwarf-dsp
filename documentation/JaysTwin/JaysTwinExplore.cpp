// Charts of the Jays Twin DSP blocks for documentation/JaysTwin: the Fender AB763 tone stack
// (dsp/Filters/FenderToneStack.h) per knob, the bright cap per volume
// (dsp/Filters/VolumeBright.h), the cabinet against the Jensen C12K datasheet curve
// (dsp/Filters/TwinCabinet.h), the atan drive curves (dsp/NonLinear/AtanDrive.h) and the
// linear chain at the default settings. All responses are those of the designed digital
// sections at 48 kHz (the unit tests check them against the running filters and the circuit).
// Writes documentation/Plot/PyConPlot.py text files into the directory given as argument;
// reads ../c12k_8ohm.csv relative to it.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "dsp/NonLinear/AtanDrive.h"
#include "dsp/Filters/BiquadCascade.h"
#include "dsp/Filters/FenderToneStack.h"
#include "dsp/Filters/TwinCabinet.h"
#include "dsp/Filters/VolumeBright.h"

namespace
{
constexpr float c_fs{48000.0f};

double toDb(const double gain)
{
    return 20.0 * std::log10(std::max(std::abs(gain), 1e-12));
}

std::vector<double> frequencies(const int perOctave = 24)
{
    std::vector<double> out;
    for (double f = 20.0; f <= 20000.0; f *= std::pow(2.0, 1.0 / perOctave))
    {
        out.push_back(f);
    }
    return out;
}

template <typename Response>
void writeSeries(std::ofstream& out, const std::string& name, Response&& response, const double floorDb = -80.0)
{
    out << "#" << name << "\n";
    for (const double f : frequencies())
    {
        out << f << " " << std::max(toDb(std::abs(response(f))), floorDb) << "\n";
    }
}

std::string knobName(const double position)
{
    std::ostringstream s;
    s << position * 10.0;
    return s.str();
}

void writeToneStack(const std::string& dir, std::ofstream& summary)
{
    std::ofstream out(dir + "/jt_tonestack.txt");
    jp::FenderToneStack stack;
    stack.setSampleRate(c_fs);
    const std::vector<double> positions{0.0, 0.25, 0.5, 0.75, 1.0};
    const char* const header = "labelx=\"frequency (Hz)\" labely=\"dB\" logx=true hzticks=true miny=-50 maxy=0";
    out << "@New plot: title=\"Treble 0 to 10 (bass, mid 5)\" " << header << "\n";
    for (const double p : positions)
    {
        stack.setControls(0.5, 0.5, p);
        writeSeries(out, "treble " + knobName(p), [&](const double f) { return stack.response(f); });
    }
    out << "@New plot: title=\"Mid 0 to 10 (bass, treble 5)\" " << header << "\n";
    for (const double p : positions)
    {
        stack.setControls(0.5, p, 0.5);
        writeSeries(out, "mid " + knobName(p), [&](const double f) { return stack.response(f); });
    }
    out << "@New plot: title=\"Bass 0 to 10 (mid, treble 5)\" " << header << "\n";
    for (const double p : positions)
    {
        stack.setControls(p, 0.5, 0.5);
        writeSeries(out, "bass " + knobName(p), [&](const double f) { return stack.response(f); });
    }
    out << "@New plot: title=\"Settings\" " << header << "\n";
    const std::vector<std::pair<std::string, std::array<double, 3>>> settings{
        {"all 5", {0.5, 0.5, 0.5}},
        {"all 10", {1.0, 1.0, 1.0}},
        {"all 0", {0.0, 0.0, 0.0}},
        {"bass 3 mid 4 treble 6", {0.3, 0.4, 0.6}},
        {"mid 10, bass, treble 0", {0.0, 1.0, 0.0}}};
    for (const auto& [name, s] : settings)
    {
        stack.setControls(s[0], s[1], s[2]);
        writeSeries(out, name, [&](const double f) { return stack.response(f); });
    }
    stack.setControls(0.5, 0.5, 0.5);
    double peak = -1e9;
    double peakHz = 0.0;
    for (const double f : frequencies(48))
    {
        const double db = toDb(std::abs(stack.response(f)));
        if (db > peak)
        {
            peak = db;
            peakHz = f;
        }
    }
    summary << "tone stack, all at 5: 80 Hz " << toDb(std::abs(stack.response(80.0))) << " dB, 500 Hz "
            << toDb(std::abs(stack.response(500.0))) << " dB, 4 kHz " << toDb(std::abs(stack.response(4000.0)))
            << " dB, maximum " << peak << " dB at " << peakHz << " Hz\n";
}

void writeBright(const std::string& dir, std::ofstream& summary)
{
    std::ofstream out(dir + "/jt_bright.txt");
    jp::VolumeBright bright;
    bright.setSampleRate(c_fs);
    bright.setAmount(1.0f);
    out << "@New plot: title=\"Bright lift per volume (gain) position\" labelx=\"frequency (Hz)\" labely=\"dB\" "
           "logx=true hzticks=true miny=-1 maxy=30\n";
    for (const double p : {0.0, 0.2, 0.4, 0.6, 0.8, 1.0})
    {
        bright.setVolume(p);
        writeSeries(out, "volume " + knobName(p), [&](const double f) { return bright.response(f); });
        summary << "bright at volume " << p * 10.0 << ": 1 kHz " << toDb(std::abs(bright.response(1000.0)))
                << " dB, 3 kHz " << toDb(std::abs(bright.response(3000.0))) << " dB, 10 kHz "
                << toDb(std::abs(bright.response(10000.0))) << " dB\n";
    }
}

std::vector<std::pair<double, double>> readCurve(const std::string& path)
{
    std::ifstream file(path);
    std::vector<std::pair<double, double>> curve;
    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line[0] != '#')
        {
            std::istringstream fields(line);
            double f = 0.0;
            double db = 0.0;
            fields >> f >> db;
            curve.emplace_back(f, db);
        }
    }
    return curve;
}

std::complex<double> speakerResponse(const double f)
{
    std::complex<double> h{1.0, 0.0};
    for (const auto& d : jp::c_jensenC12KSections)
    {
        h *= jp::biquadSectionResponse(jp::designBiquad(d, c_fs), f, c_fs);
    }
    return h;
}

void writeCabinet(const std::string& dir, std::ofstream& summary)
{
    const auto curve = readCurve(dir + "/../c12k_8ohm.csv");
    if (curve.empty())
    {
        std::cerr << "c12k_8ohm.csv not found next to " << dir << "\n";
        return;
    }
    // the level that the fit found: the weighted mean of curve - fit (as fit_cabinet.py)
    double sumW = 0.0;
    double sumWE = 0.0;
    for (const auto& [f, db] : curve)
    {
        if (f >= 30.0 && f <= 8000.0)
        {
            const double w = f > 6000.0 ? 0.3 : 1.0;
            sumW += w;
            sumWE += w * (db - toDb(std::abs(speakerResponse(f))));
        }
    }
    const double level = sumWE / sumW;
    jp::BiquadCascade<jp::c_twinCabinetSections> cabinet;
    jp::designTwinCabinet(cabinet, c_fs);
    std::ofstream out(dir + "/jt_cabinet.txt");
    out << "@New plot: title=\"Jensen C12K 8 ohm: datasheet (IEC baffle, level " << std::fixed << std::setprecision(1)
        << level << std::defaultfloat << std::setprecision(6)
        << " dB SPL = 0 dB) and the biquad chain\" labelx=\"frequency (Hz)\" labely=\"dB\" logx=true hzticks=true "
           "miny=-60 maxy=10\n";
    out << "#C12K datasheet\n";
    for (const auto& [f, db] : curve)
    {
        out << f << " " << db - level << "\n";
    }
    writeSeries(out, "7 biquads (fit)", speakerResponse);
    writeSeries(out, "with open back (80 Hz first order)",
                [&](const double f) { return cabinet.response(f, static_cast<double>(c_fs)); });
    summary << "cabinet: level " << level << " dB SPL; with open back 100 Hz "
            << toDb(std::abs(cabinet.response(100.0, c_fs))) << " dB, 1 kHz "
            << toDb(std::abs(cabinet.response(1000.0, c_fs))) << " dB, 2.3 kHz "
            << toDb(std::abs(cabinet.response(2300.0, c_fs))) << " dB, 6 kHz "
            << toDb(std::abs(cabinet.response(6000.0, c_fs))) << " dB, 10 kHz "
            << toDb(std::abs(cabinet.response(10000.0, c_fs))) << " dB\n";
}

void writeDrive(const std::string& dir)
{
    std::ofstream out(dir + "/jt_drive.txt");
    out << "@New plot: title=\"atan(d x) / atan(d)\" labelx=\"input\" labely=\"output\" miny=-2 maxy=2\n";
    for (const float d : {0.01f, 1.0f, 3.0f, 10.0f, 30.0f, 63.0f})
    {
        jp::AtanDrive drive;
        drive.setDrive(d);
        out << "#d " << d << "\n";
        for (int i = -200; i <= 200; ++i)
        {
            const float x = 0.01f * static_cast<float>(i);
            out << x << " " << drive.process(x) << "\n";
        }
    }
}

void writeChain(const std::string& dir)
{
    jp::FenderToneStack stack;
    stack.setSampleRate(c_fs);
    stack.setControls(0.5, 0.5, 0.5);
    jp::VolumeBright bright;
    bright.setSampleRate(c_fs);
    bright.setVolume(0.3);
    bright.setAmount(1.0f);
    jp::BiquadCascade<jp::c_twinCabinetSections> cabinet;
    jp::designTwinCabinet(cabinet, c_fs);
    std::ofstream out(dir + "/jt_chain.txt");
    out << "@New plot: title=\"Linear chain, bass, mid, treble 5 (small signals)\" labelx=\"frequency (Hz)\" "
           "labely=\"dB\" logx=true hzticks=true miny=-70 maxy=10\n";
    writeSeries(out, "tone stack", [&](const double f) { return stack.response(f); });
    writeSeries(out, "cabinet", [&](const double f) { return cabinet.response(f, c_fs); });
    writeSeries(out, "tone stack x cabinet",
                [&](const double f) { return stack.response(f) * cabinet.response(f, c_fs); });
    writeSeries(out, "with bright at gain 3",
                [&](const double f) { return stack.response(f) * bright.response(f) * cabinet.response(f, c_fs); });
}
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: JaysTwinExplore <output directory>\n";
        return 1;
    }
    const std::string dir = argv[1];
    std::ofstream summary(dir + "/summary.txt");
    writeToneStack(dir, summary);
    writeBright(dir, summary);
    writeCabinet(dir, summary);
    writeDrive(dir);
    writeChain(dir);
    return 0;
}
