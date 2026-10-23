// Charts of jp::EigenghostPedal (plugins/eigenghost/EigenghostPedal.h) for
// documentation/Eigenghost: impulse responses and echo density per diffusion, energy decay
// per decay setting, decay time per octave band per damping setting, the partials of a
// shimmer tail, a size change by fade against a hard change, the line lengths per bulge, and
// the modulation (pitch deviation of the vibrato, a tail spectrum with and without).
// Writes documentation/Plot/PyConPlot.py text files and summary.txt into the directory given
// as argument.

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "dsp/Analysis/ZeroCrossingFrequency.h"
#include "dsp/BlockProcessors/BlockProcVibrato.h"
#include "dsp/Reverbs/FdnTank.h"
#include "plugins/eigenghost/EigenghostPedal.h"
#include "tests/unit/TestHelpers.h"

namespace
{
using jp::EigenghostPedal;
constexpr double c_pi{3.14159265358979323846};
constexpr float c_fs{48000.0f};

struct Stereo
{
    std::vector<float> left;
    std::vector<float> right;
};

template <typename Settings>
std::unique_ptr<EigenghostPedal> makePedal(Settings&& settings)
{
    auto pedal = std::make_unique<EigenghostPedal>();
    pedal->setSampleRate(c_fs);
    settings(*pedal);
    pedal->reset();
    return pedal;
}

Stereo run(EigenghostPedal& pedal, const std::vector<float>& input)
{
    Stereo out{std::vector<float>(input.size()), std::vector<float>(input.size())};
    for (size_t pos = 0; pos < input.size(); pos += 128)
    {
        const size_t n = std::min<size_t>(128, input.size() - pos);
        const float* in[1] = {input.data() + pos};
        float* outs[2] = {out.left.data() + pos, out.right.data() + pos};
        pedal.process(in, outs, n);
    }
    return out;
}

std::vector<float> impulse(const size_t length)
{
    std::vector<float> x(length, 0.0f);
    x[0] = 1.0f;
    return x;
}

double toDb(const double x)
{
    return 20.0 * std::log10(std::max(x, 1e-12));
}

/// Energy decay curve in dB (Schroeder backward integration).
std::vector<double> energyDecay(const std::vector<float>& x)
{
    std::vector<double> edc(x.size());
    double sum = 0.0;
    for (size_t i = x.size(); i-- > 0;)
    {
        sum += static_cast<double>(x[i]) * x[i];
        edc[i] = sum;
    }
    const double total = sum;
    for (auto& v : edc)
    {
        v = 10.0 * std::log10(std::max(v / total, 1e-30));
    }
    return edc;
}

/// T60 from a least-squares fit of the EDC between -5 dB and lowDb.
double fitT60(const std::vector<double>& edc, const double lowDb = -35.0)
{
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0, count = 0.0;
    for (size_t i = 0; i < edc.size(); ++i)
    {
        if (edc[i] <= -5.0 && edc[i] >= lowDb)
        {
            const double t = static_cast<double>(i) / c_fs;
            sx += t;
            sy += edc[i];
            sxx += t * t;
            sxy += t * edc[i];
            count += 1.0;
        }
    }
    return -60.0 * (count * sxx - sx * sx) / (count * sxy - sx * sy);
}

/// RBJ constant-peak bandpass, one octave wide, for band decay times.
class OctaveBand
{
  public:
    explicit OctaveBand(const double centre)
    {
        const double w = 2.0 * c_pi * centre / c_fs;
        const double alpha = std::sin(w) * std::sinh(std::log(2.0) / 2.0 * w / std::sin(w));
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

/// Normalised echo density (Abel and Huang): share of samples beyond one standard deviation
/// in a sliding window, divided by the Gaussian share erfc(1 / sqrt(2)); 1 means as dense as
/// noise.
double echoDensity(const std::vector<float>& x, const size_t centre, const size_t halfWindow)
{
    const size_t from = centre > halfWindow ? centre - halfWindow : 0;
    const size_t to = std::min(centre + halfWindow, x.size());
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
    {
        sum += static_cast<double>(x[i]) * x[i];
    }
    const double sigma = std::sqrt(sum / static_cast<double>(to - from));
    if (sigma <= 0.0)
    {
        return 0.0;
    }
    size_t beyond = 0;
    for (size_t i = from; i < to; ++i)
    {
        beyond += std::fabs(x[i]) > sigma ? 1u : 0u;
    }
    return static_cast<double>(beyond) / static_cast<double>(to - from) / std::erfc(1.0 / std::sqrt(2.0));
}

/// Amplitude of one frequency in a window (Hann single-bin DFT, sine amplitude A gives A).
double toneLevel(const std::vector<float>& x, const size_t from, const size_t length, const double hz)
{
    std::complex<double> sum{0.0, 0.0};
    double windowSum = 0.0;
    for (size_t i = 0; i < length; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos(2.0 * c_pi * static_cast<double>(i) / static_cast<double>(length));
        sum += w * static_cast<double>(x[from + i]) *
               std::polar(1.0, -2.0 * c_pi * hz * static_cast<double>(from + i) / c_fs);
        windowSum += w;
    }
    return 2.0 * std::abs(sum) / windowSum;
}

/// Energy above 5 kHz relative to all energy in a window, dB (one-pole-pair highpass split).
double highBandShareDb(const std::vector<float>& x, const size_t from, const size_t length)
{
    // fourth-order highpass at 5 kHz as two RBJ highpass biquads (Q 0.54, 1.31)
    struct Biquad
    {
        double b0, b1, b2, a1, a2, x1{0}, x2{0}, y1{0}, y2{0};
        double process(const double in)
        {
            const double y = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = in;
            y2 = y1;
            y1 = y;
            return y;
        }
    };
    const auto highpass = [](const double q)
    {
        const double w = 2.0 * c_pi * 5000.0 / c_fs;
        const double alpha = std::sin(w) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        const double c = std::cos(w);
        return Biquad{(1.0 + c) / 2.0 / a0, -(1.0 + c) / a0, (1.0 + c) / 2.0 / a0, -2.0 * c / a0, (1.0 - alpha) / a0};
    };
    Biquad first = highpass(0.5412);
    Biquad second = highpass(1.3066);
    double high = 0.0;
    double total = 0.0;
    const size_t settle = 256;
    for (size_t i = from - settle; i < from + length; ++i)
    {
        const double y = second.process(first.process(x[i]));
        if (i >= from)
        {
            high += y * y;
            total += static_cast<double>(x[i]) * x[i];
        }
    }
    return 10.0 * std::log10(std::max(high, 1e-30) / std::max(total, 1e-30));
}

void writeImpulseAndDensity(const std::string& dir, std::ofstream& summary)
{
    std::ofstream ir(dir + "/eg_impulse.txt");
    std::ofstream ned(dir + "/eg_density.txt");
    ned << "@New plot: title=\"Normalised echo density (1 = as dense as noise)\" labelx=\"time (ms)\" "
           "labely=\"echo density\" miny=0 maxy=1.2\n";
    {
        const auto pedal = makePedal([](EigenghostPedal&) {});
        size_t total = 0;
        summary << "Diffuser stages at the default 1.3 m (samples):";
        for (size_t i = 0; i < EigenghostPedal::c_diffuserStages; ++i)
        {
            summary << " " << pedal->getDiffuserStageSize(i);
            total += pedal->getDiffuserStageSize(i);
        }
        summary << ", sum " << total << " (" << 1000.0 * static_cast<double>(total) / c_fs
                << " ms: the pre-delay at diffusion 0)\n";
    }
    summary << "Echo density (Abel-Huang, 20 ms window), size 12 m, decay 3 s:\n";
    for (const float diffusion : {0.0f, 60.0f, 100.0f})
    {
        const auto pedal = makePedal(
            [diffusion](EigenghostPedal& p)
            {
                p.setDry(EigenghostPedal::c_muteDb);
                p.setWet(0.0f);
                p.setShimmer(0.0f);
                p.setDiffusion(diffusion);
            });
        const auto out = run(*pedal, impulse(48000));
        ir << "@New plot: title=\"Impulse response, diffusion " << diffusion
           << " %\" labelx=\"time (ms)\" labely=\"left\" miny=-0.3 maxy=0.3\n";
        ir << "#diffusion " << diffusion << " %\n";
        for (size_t i = 0; i < 9600; ++i)
        {
            ir << 1000.0 * static_cast<double>(i) / c_fs << " " << out.left[i] << "\n";
        }
        ned << "#diffusion " << diffusion << " %\n";
        double reached = -1.0;
        for (size_t centre = 480; centre < 24000; centre += 120)
        {
            const double d = echoDensity(out.left, centre, 480);
            ned << 1000.0 * static_cast<double>(centre) / c_fs << " " << d << "\n";
            if (reached < 0.0 && d >= 0.9)
            {
                reached = 1000.0 * static_cast<double>(centre) / c_fs;
            }
        }
        summary << "  diffusion " << std::setw(3) << diffusion << " %: echo density reaches 0.9 at " << reached
                << " ms\n";
    }
}

void writeDecay(const std::string& dir, std::ofstream& summary)
{
    std::ofstream out(dir + "/eg_decay.txt");
    out << "@New plot: title=\"Energy decay curves (left, damping 20 kHz, low cut 10 Hz, no pitch shifting)\" "
           "labelx=\"time (s)\" labely=\"dB\" miny=-80 maxy=0\n";
    summary << "Decay time from the energy decay curve (-5 to -35 dB):\n";
    for (const float decay : {0.5f, 1.0f, 3.0f, 10.0f})
    {
        const auto pedal = makePedal(
            [decay](EigenghostPedal& p)
            {
                p.setDry(EigenghostPedal::c_muteDb);
                p.setWet(0.0f);
                p.setShimmer(0.0f);
                p.setDecay(decay);
                p.setDamping(20000.0f);
                p.setLowCut(10.0f);
            });
        const auto response = run(*pedal, impulse(static_cast<size_t>(c_fs * std::max(2.0f, decay * 1.5f))));
        const auto edc = energyDecay(response.left);
        out << "#decay " << decay << " s\n";
        for (size_t i = 0; i < edc.size() && i < static_cast<size_t>(c_fs * 8.0f); i += 240)
        {
            out << static_cast<double>(i) / c_fs << " " << edc[i] << "\n";
        }
        summary << "  decay " << std::setw(4) << decay << " s: measured " << fitT60(edc) << " s\n";
    }
}

void writeDamping(const std::string& dir, std::ofstream& summary)
{
    std::ofstream out(dir + "/eg_damping.txt");
    out << "@New plot: title=\"Decay time per octave band (decay 3 s, lowpass in lines 0 and 1)\" "
           "labelx=\"band centre (Hz)\" labely=\"T60 (s)\" logx=true hzticks=true miny=0 maxy=3.5\n";
    const std::array<double, 7> centres{125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0};
    summary << "Band T60 (s) at decay 3 s, bands 125 Hz .. 8 kHz:\n";
    for (const float damping : {20000.0f, 6000.0f, 3000.0f, 1000.0f, 300.0f})
    {
        const auto pedal = makePedal(
            [damping](EigenghostPedal& p)
            {
                p.setDry(EigenghostPedal::c_muteDb);
                p.setWet(0.0f);
                p.setShimmer(0.0f);
                p.setDecay(3.0f);
                p.setDamping(damping);
                p.setLowCut(10.0f);
            });
        const auto response = run(*pedal, impulse(static_cast<size_t>(c_fs * 4.0f)));
        out << "#damping " << damping << " Hz\n";
        summary << "  damping " << std::setw(5) << damping << " Hz:";
        for (const double centre : centres)
        {
            OctaveBand band(centre);
            std::vector<float> filtered(response.left.size());
            for (size_t i = 0; i < filtered.size(); ++i)
            {
                filtered[i] = static_cast<float>(band.process(response.left[i]));
            }
            const double t60 = fitT60(energyDecay(filtered), -25.0);
            out << centre << " " << t60 << "\n";
            summary << " " << std::fixed << std::setprecision(2) << t60;
        }
        summary << std::defaultfloat << std::setprecision(6) << "\n";
    }
}

void writeShimmer(const std::string& dir, std::ofstream& summary)
{
    std::ofstream out(dir + "/eg_shimmer.txt");
    const size_t length = static_cast<size_t>(c_fs * 6.0f);
    std::vector<float> input(length, 0.0f);
    for (size_t i = 0; i < static_cast<size_t>(c_fs * 0.5f); ++i)
    {
        input[i] = static_cast<float>(0.5 * std::sin(2.0 * c_pi * 237.0 * static_cast<double>(i) / c_fs));
    }
    struct Case
    {
        const char* title;
        float pitch1;
        float pitch2;
        float shimmer;
    };
    const std::array<Case, 3> cases{Case{"no pitch shifting", 12.0f, 12.0f, 0.0f},
                                    Case{"pitch 1 and 2 +12, amount 60 %", 12.0f, 12.0f, 60.0f},
                                    Case{"pitch 1 +7, pitch 2 +12, amount 60 %", 7.0f, 12.0f, 60.0f}};
    const std::array<double, 5> semitones{0.0, 7.0, 12.0, 19.0, 24.0};
    summary << "Shimmer: 237 Hz for 0.5 s, decay 6 s, level of each partial 2 s later (dB, left):\n";
    for (const auto& c : cases)
    {
        const auto pedal = makePedal(
            [&c](EigenghostPedal& p)
            {
                p.setDry(EigenghostPedal::c_muteDb);
                p.setWet(0.0f);
                p.setDecay(6.0f);
                p.setDamping(8000.0f);
                p.setPitch1(c.pitch1);
                p.setPitch2(c.pitch2);
                p.setShimmer(c.shimmer);
            });
        const auto response = run(*pedal, input);
        out << "@New plot: title=\"" << c.title << "\" labelx=\"time (s)\" labely=\"level (dB)\" miny=-100 maxy=0\n";
        summary << "  " << c.title << ":";
        for (const double st : semitones)
        {
            const double hz = 237.0 * std::pow(2.0, st / 12.0);
            out << "#+" << st << " st (" << std::lround(hz) << " Hz)\n";
            constexpr size_t window = 8192;
            for (size_t from = 0; from + window < length; from += 2048)
            {
                out << static_cast<double>(from + window / 2) / c_fs << " "
                    << toDb(toneLevel(response.left, from, window, hz)) << "\n";
            }
            summary << " +" << st << ": "
                    << std::lround(toDb(toneLevel(response.left, static_cast<size_t>(c_fs * 2.5f), window, hz)));
        }
        summary << "\n";
    }
}

void writeSizeFade(const std::string& dir, std::ofstream& summary)
{
    // the tank alone with a sine: the same size change by fade (setter) and hard (setDirectSize)
    using Tank = jp::FdnTank<16, 16>;
    std::ofstream wave(dir + "/eg_size_wave.txt");
    std::ofstream band(dir + "/eg_size_band.txt");
    band << "@New plot: title=\"Energy above 5 kHz per 10 ms, size 12 -> 20 m at 1.0 s\" labelx=\"time (s)\" "
            "labely=\"dB re total\" miny=-140 maxy=0\n";
    summary << "Size change 12 -> 20 m on a 237 Hz sine (tank alone), energy above 5 kHz in the 10 ms after it:\n";
    for (const bool hard : {false, true})
    {
        Tank tank(c_fs);
        tank.setMinSize(12.0f);
        tank.setMaxSize(36.0f);
        tank.setSpreadBulge(EigenghostPedal::c_defaultBulge);
        tank.setDecay(3000.0f);
        Tank target(c_fs);
        target.setMinSize(20.0f);
        target.setMaxSize(60.0f);
        target.setSpreadBulge(EigenghostPedal::c_defaultBulge);
        {
            // lengths apply with the first block
            std::array<float, 16> silence{};
            std::array<float, 16> dummyL{};
            std::array<float, 16> dummyR{};
            target.processBlockSplit(silence.data(), dummyL.data(), dummyR.data());
        }
        const size_t length = static_cast<size_t>(c_fs * 1.5f);
        std::vector<float> out;
        std::array<float, 16> in{};
        std::array<float, 16> l{};
        std::array<float, 16> r{};
        for (size_t b = 0; b * 16 < length; ++b)
        {
            if (b * 16 == static_cast<size_t>(c_fs))
            {
                if (hard)
                {
                    for (size_t o = 0; o < 16; ++o)
                    {
                        tank.setDirectSize(o, target.getLineLength(o));
                    }
                }
                else
                {
                    tank.setMinSize(20.0f);
                    tank.setMaxSize(60.0f);
                }
            }
            for (size_t i = 0; i < 16; ++i)
            {
                in[i] =
                    static_cast<float>(0.05 * std::sin(2.0 * c_pi * 237.0 * static_cast<double>(b * 16 + i) / c_fs));
            }
            tank.processBlockSplit(in.data(), l.data(), r.data());
            out.insert(out.end(), l.begin(), l.end());
        }
        const char* name = hard ? "hard change" : "fade (50 ms)";
        wave << "@New plot: title=\"Left output around the size change, " << name
             << "\" labelx=\"time (s)\" labely=\"left\"\n#" << name << "\n";
        for (size_t i = static_cast<size_t>(c_fs * 0.98f); i < static_cast<size_t>(c_fs * 1.08f); ++i)
        {
            wave << static_cast<double>(i) / c_fs << " " << out[i] << "\n";
        }
        band << "#" << name << "\n";
        for (size_t from = static_cast<size_t>(c_fs * 0.8f); from + 480 < out.size(); from += 240)
        {
            band << static_cast<double>(from + 240) / c_fs << " " << highBandShareDb(out, from, 480) << "\n";
        }
        summary << "  " << name << ": " << highBandShareDb(out, static_cast<size_t>(c_fs), 480) << " dB\n";
    }
}

void writeLengths(const std::string& dir, std::ofstream& summary)
{
    std::ofstream out(dir + "/eg_lengths.txt");
    out << "@New plot: title=\"Line lengths, size 12 m, spread 3\" labelx=\"line\" labely=\"length (ms)\"\n";
    summary << "Line lengths in ms (size 12 m, spread 3), shortest .. longest:\n";
    for (const float bulge : {-1.0f, -0.4f, 0.0f, 0.6f, 1.0f}) // 0.6 is the default
    {
        const auto pedal = makePedal([bulge](EigenghostPedal& p) { p.setBulge(bulge); });
        std::vector<float> silence(16, 0.0f);
        std::ignore = run(*pedal, silence);
        out << "#bulge " << bulge << "\n";
        const auto& tank = pedal->getTank();
        for (size_t o = 0; o < EigenghostPedal::c_order; ++o)
        {
            out << o << " " << 1000.0 * static_cast<double>(tank.getLineLength(o)) / c_fs << "\n";
        }
        summary << "  bulge " << std::setw(4) << bulge << ": "
                << 1000.0 * static_cast<double>(tank.getLineLength(0)) / c_fs << " .. "
                << 1000.0 * static_cast<double>(tank.getLineLength(EigenghostPedal::c_order - 1)) / c_fs
                << ", median line "
                << 1000.0 * static_cast<double>(tank.getLineLength(EigenghostPedal::c_order / 2)) / c_fs << "\n";
    }
}

void writeModulation(const std::string& dir, std::ofstream& summary)
{
    // the vibrato alone: a 1 kHz tone, frequency per 50 ms window by zero crossings
    {
        std::ofstream out(dir + "/eg_mod_pitch.txt");
        out << "@New plot: title=\"Pitch deviation of a modulated line (0.5 Hz), 1 kHz tone\" labelx=\"time (s)\" "
               "labely=\"cents\" miny=-25 maxy=25\n";
        for (const float cents : {4.0f, 20.0f})
        {
            jp::BlockVibrato<EigenghostPedal::c_blockSize> vibrato(c_fs, 0.004f);
            vibrato.setRate(EigenghostPedal::c_modulationHz);
            vibrato.setDepthCents(cents);
            vibrato.reset();
            const size_t length = static_cast<size_t>(c_fs * 4.5f);
            std::vector<float> signal(length);
            std::array<float, EigenghostPedal::c_blockSize> block{};
            for (size_t b = 0; b + block.size() <= length; b += block.size())
            {
                for (size_t i = 0; i < block.size(); ++i)
                {
                    block[i] = static_cast<float>(std::sin(2.0 * c_pi * 1000.0 * static_cast<double>(b + i) / c_fs));
                }
                vibrato.process(block);
                std::copy(block.begin(), block.end(), signal.begin() + static_cast<long>(b));
            }
            out << "#depth " << cents << " cents\n";
            constexpr size_t window = 2400;
            for (size_t from = 4800; from + window <= length; from += window / 4)
            {
                jp::ZeroCrossingFrequency counter(c_fs, 0.01f);
                counter.add(signal.data() + from, window);
                out << static_cast<double>(from + window / 2) / c_fs << " "
                    << 1200.0 * std::log2(counter.frequency() / 1000.0) << "\n";
            }
        }
    }
    // the reverb: a noise burst, then the spectrum of the late tail; metallic ringing shows as
    // single modes standing out of the tail, which the modulation smears
    std::ofstream out(dir + "/eg_mod_tail.txt");
    out << "@New plot: title=\"Late tail of a noise burst (decay 3 s, 1.5 to 2.9 s), per mod depth\" "
           "labelx=\"frequency (Hz)\" labely=\"dB\" miny=-60 maxy=0\n";
    summary << "Modulation: late tail of a noise burst (decay 3 s), strongest bin over the mean power 500 Hz to 4 kHz:";
    const size_t from = static_cast<size_t>(c_fs * 1.5f);
    constexpr size_t window = 65536;
    std::vector<float> input(from + window, 0.0f);
    jp::Random random(3);
    for (size_t i = 0; i < static_cast<size_t>(c_fs * 0.2f); ++i)
    {
        input[i] = 0.5f * (2.0f * random.uniform() - 1.0f);
    }
    double reference = 0.0;
    for (const float cents : {0.0f, 4.0f, 20.0f})
    {
        const auto pedal = makePedal(
            [cents](EigenghostPedal& p)
            {
                p.setDry(EigenghostPedal::c_muteDb);
                p.setWet(0.0f);
                p.setShimmer(0.0f);
                p.setModDepth(cents);
            });
        const auto response = run(*pedal, input);
        const std::vector<float> tail(response.left.begin() + static_cast<long>(from), response.left.end());
        const auto magnitude = jp::test::hannMagnitudeSpectrum(tail);
        const double binHz = static_cast<double>(c_fs) / static_cast<double>(window);
        double peak = 0.0;
        double sum = 0.0;
        size_t count = 0;
        for (size_t k = 0; k < magnitude.size(); ++k)
        {
            const double hz = static_cast<double>(k) * binHz;
            if (hz >= 500.0 && hz <= 4000.0)
            {
                const double power = magnitude[k] * magnitude[k];
                peak = std::max(peak, power);
                sum += power;
                ++count;
            }
        }
        if (reference == 0.0)
        {
            reference = std::sqrt(peak);
        }
        out << "#mod depth " << cents << " cents\n";
        for (size_t k = 0; k < magnitude.size(); ++k)
        {
            const double hz = static_cast<double>(k) * binHz;
            if (hz >= 900.0 && hz <= 1100.0)
            {
                out << hz << " " << std::max(toDb(magnitude[k] / reference), -120.0) << "\n";
            }
        }
        summary << " " << cents << " cents: " << std::fixed << std::setprecision(1)
                << 10.0 * std::log10(peak / (sum / static_cast<double>(count))) << " dB" << std::defaultfloat
                << std::setprecision(6) << ";";
    }
    summary << "\n";
}
}

int main(const int argc, const char* const* argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: EigenghostExplore <output directory>\n";
        return 1;
    }
    const std::string dir = argv[1];
    std::ofstream summary(dir + "/summary.txt");
    summary << "EigenghostExplore (fs 48 kHz, defaults unless stated; dry muted, wet 0 dB)\n";
    writeImpulseAndDensity(dir, summary);
    writeDecay(dir, summary);
    writeDamping(dir, summary);
    writeShimmer(dir, summary);
    writeSizeFade(dir, summary);
    writeLengths(dir, summary);
    writeModulation(dir, summary);
    return 0;
}
