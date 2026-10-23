// Views of jp::OrnsteinUhlenbeck (dsp/Generators/OrnsteinUhlenbeck.h) for documentation/OrnsteinUhlenbeck:
// timelines at four sigma "speeds", empirical autocorrelation against exp(-theta tau), the
// stationary distribution against the theoretical Gaussian, the exact transition against an
// Euler-Maruyama step at large theta dt, and the shipahoy "brown" hop walk.
// Transformed from AbacDsp documentation/OrnsteinUhlenbeck/OrnsteinUhlenbeckTimeline.cpp
// (see docs/references.md). Writes documentation/Plot/PyConPlot.py text files into the
// directory given as the only argument.

#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "dsp/Generators/OrnsteinUhlenbeck.h"
#include "dsp/Numbers/Random.h"
#include "tests/unit/ShipAhoyClockedEngine.h"

namespace
{
constexpr float c_pi{3.14159265358979323846f};

// dt = 1 / c_stepRate: fine enough for smooth timelines, not an audio rate.
constexpr float c_stepRate{2000.0f};
constexpr size_t c_numSteps{20000};
// theta = 20 sigma + 1, so these also span about 4x in reversion rate.
constexpr std::array<float, 4> c_sigmas{0.02f, 0.05f, 0.1f, 0.2f};
constexpr size_t c_burnInSteps{c_numSteps / 5};
constexpr float c_maxLagSeconds{3.0f};
constexpr size_t c_lagStride{10};
constexpr int c_autocorrRuns{20};
constexpr size_t c_distributionSteps{10'000'000};
constexpr int c_bins{61};
constexpr float c_binRangeDeviations{5.0f};

/// Deviations from mu (setSigma() also sets mu = sigma), so all runs are centred on zero.
std::vector<float> simulate(const float sigma, const uint64_t seed, const size_t steps = c_numSteps)
{
    jp::OrnsteinUhlenbeck process(c_stepRate, seed);
    process.setSigma(sigma);
    std::vector<float> deviations(steps);
    for (auto& d : deviations)
    {
        d = process.step() - process.getMu();
    }
    return deviations;
}

void writeAutocorrelation(std::ofstream& out, const std::vector<float>& values, const size_t burnIn,
                          const size_t maxLag, const size_t stride, const float xScale)
{
    const size_t n = values.size() - burnIn;
    double mean = 0.0;
    for (size_t i = burnIn; i < values.size(); ++i)
    {
        mean += values[i];
    }
    mean /= static_cast<double>(n);
    double variance = 0.0;
    for (size_t i = burnIn; i < values.size(); ++i)
    {
        variance += (values[i] - mean) * (values[i] - mean);
    }
    for (size_t lag = 0; lag <= maxLag; lag += stride)
    {
        double covariance = 0.0;
        for (size_t i = burnIn; i + lag < values.size(); ++i)
        {
            covariance += (values[i] - mean) * (values[i + lag] - mean);
        }
        out << static_cast<float>(lag) * xScale << " " << covariance / variance << "\n";
    }
}

void writeGaussian(std::ofstream& out, const float mean, const float deviation, const float from, const float to)
{
    constexpr int points{200};
    for (int i = 0; i <= points; ++i)
    {
        const float x = from + (to - from) * static_cast<float>(i) / points;
        const float z = (x - mean) / deviation;
        out << x << " " << std::exp(-0.5f * z * z) / (deviation * std::sqrt(2.0f * c_pi)) << "\n";
    }
}

void writeDistribution(std::ofstream& out, const float sigma, const uint64_t seed)
{
    jp::OrnsteinUhlenbeck process(c_stepRate, seed);
    process.setSigma(sigma);
    process.setValue(process.getMu());
    const float deviation = process.getStationaryDeviation();
    const float range = c_binRangeDeviations * deviation;
    const float binWidth = 2.0f * range / c_bins;
    std::array<size_t, c_bins> counts{};
    size_t counted = 0;
    for (size_t i = 0; i < c_distributionSteps; ++i)
    {
        const auto bin = static_cast<int>((process.step() - process.getMu() + range) / binWidth);
        if (bin >= 0 && bin < c_bins)
        {
            ++counts[static_cast<size_t>(bin)];
            ++counted;
        }
    }
    out << "#empirical\n";
    for (int b = 0; b < c_bins; ++b)
    {
        const float centre = -range + (static_cast<float>(b) + 0.5f) * binWidth;
        out << centre << " " << static_cast<float>(counts[static_cast<size_t>(b)]) / (counted * binWidth) << "\n";
    }
    out << "#theoretical Gaussian\n";
    writeGaussian(out, 0.0f, deviation, -range, range);
}

/// Exact transition against Euler-Maruyama driven by the same Gaussian sequence.
void writeStepMethods(std::ofstream& out)
{
    constexpr float sigma{0.7f}; // theta = 15
    constexpr float theta{sigma * 20.0f + 1.0f};
    constexpr int steps{60};
    for (const float stepRate : {150.0f, 10.0f, 7.0f})
    {
        const float dt = 1.0f / stepRate;
        jp::OrnsteinUhlenbeck exact(stepRate, 5u);
        exact.setSigma(sigma);
        exact.setValue(exact.getMu() + 0.05f);
        // the same noise: the exact step uses one gaussian() per step from the same seed
        jp::Random random(5u);
        const float noiseScale = sigma / jp::OrnsteinUhlenbeck::c_noiseDivisor;
        float euler = exact.getValue();
        out << "@New plot: title=\"theta dt = " << theta * dt << " (sigma 0.7, " << stepRate
            << " steps/s)\" symlogy=true linthreshy=0.1\n";
        std::vector<float> exactValues{exact.getValue() - exact.getMu()};
        std::vector<float> eulerValues{euler - exact.getMu()};
        for (int i = 1; i <= steps; ++i)
        {
            const float noise = random.gaussian();
            euler += theta * (exact.getMu() - euler) * dt + noiseScale * std::sqrt(dt) * noise;
            eulerValues.push_back(euler - exact.getMu());
            exactValues.push_back(exact.step() - exact.getMu());
        }
        out << "#exact transition\n";
        for (size_t i = 0; i < exactValues.size(); ++i)
        {
            out << i << " " << exactValues[i] << "\n";
        }
        out << "#Euler-Maruyama\n";
        for (size_t i = 0; i < eulerValues.size(); ++i)
        {
            out << i << " " << eulerValues[i] << "\n";
        }
    }
}

/// shipahoy "brown": hop targets of the pedal (one OU step per hop) over time, their
/// distribution and their autocorrelation per hop.
void writeShipAhoyBrown(std::ofstream& out, std::ofstream& summary)
{
    // a low sample rate keeps hops cheap; the hop logic does not depend on it
    jp::test::ClockedEngine pedal(2024u);
    pedal.setSampleRate(700.0f);
    pedal.setSpeed(7.0f);
    pedal.setRandomType(static_cast<float>(jp::test::ClockedEngine::RandomType::Brown));
    pedal.reset();
    constexpr size_t hops{200000};
    std::vector<float> targets;
    targets.reserve(hops);
    uint64_t hopCount = 0;
    float in = 0.0f;
    float out1 = 0.0f;
    while (targets.size() < hops)
    {
        pedal.process(&in, &out1, 1);
        if (pedal.getHopCount() != hopCount)
        {
            hopCount = pedal.getHopCount();
            targets.push_back(pedal.getTarget());
        }
    }
    out << "@New plot: title=\"brown hop targets at 7 Hz (first 30 s)\" labelx=\"time (s)\" labely=\"position\" "
           "miny=0 maxy=1\n#target\n";
    for (size_t i = 0; i < 210; ++i)
    {
        // hold each value for its hop period, so the plot shows the steps
        out << static_cast<float>(i) / 7.0f << " " << targets[i] << "\n";
        out << static_cast<float>(i + 1) / 7.0f << " " << targets[i] << "\n";
    }
    constexpr int bins{50};
    std::array<size_t, bins> counts{};
    size_t clamped = 0;
    double mean = 0.0;
    for (const float t : targets)
    {
        counts[static_cast<size_t>(std::min(bins - 1, static_cast<int>(t * bins)))] += 1;
        clamped += (t <= 0.0f || t >= 1.0f) ? 1 : 0;
        mean += t;
    }
    mean /= static_cast<double>(targets.size());
    double variance = 0.0;
    double lagOne = 0.0;
    for (size_t i = 0; i < targets.size(); ++i)
    {
        variance += (targets[i] - mean) * (targets[i] - mean);
        if (i > 0)
        {
            lagOne += (targets[i] - mean) * (targets[i - 1] - mean);
        }
    }
    const double clampedPercent = 100.0 * static_cast<double>(clamped) / static_cast<double>(targets.size());
    out << "@New plot: title=\"distribution of " << hops << " hop targets (" << clampedPercent
        << " % clamped)\" labelx=\"position\" labely=\"density\"\n#empirical\n";
    for (int b = 0; b < bins; ++b)
    {
        out << (static_cast<float>(b) + 0.5f) / bins << " "
            << static_cast<float>(counts[static_cast<size_t>(b)]) * bins / static_cast<float>(targets.size()) << "\n";
    }
    out << "#Gaussian 0.5 +- 0.2 (before the clamp)\n";
    writeGaussian(out, 0.5f, jp::test::ClockedEngine::c_brownDeviation, 0.0f, 1.0f);
    out << "@New plot: title=\"autocorrelation per hop\" labelx=\"lag (hops)\" labely=\"ACF\" miny=-0.1 maxy=1.05\n";
    out << "#empirical\n";
    writeAutocorrelation(out, targets, 0, 40, 1, 1.0f);
    out << "#theoretical exp(-0.1 k)\n";
    for (int k = 0; k <= 40; ++k)
    {
        out << k << " " << std::exp(-0.1f * static_cast<float>(k)) << "\n";
    }
    summary << "brown hops: mean " << mean << ", deviation " << std::sqrt(variance / targets.size())
            << ", lag-one correlation " << lagOne / variance << ", clamped " << clampedPercent << " %\n";
}
}

int main(int argc, char* argv[])
{
    const std::string dir = argc > 1 ? argv[1] : ".";
    std::ofstream timeline(dir + "/ou_timeline.txt");
    std::ofstream autocorr(dir + "/ou_autocorr.txt");
    std::ofstream distribution(dir + "/ou_distribution.txt");
    std::ofstream stepMethods(dir + "/ou_step_methods.txt");
    std::ofstream shipahoy(dir + "/ou_shipahoy.txt");
    std::ofstream summary(dir + "/summary.txt");
    if (!timeline || !autocorr || !distribution || !stepMethods || !shipahoy || !summary)
    {
        std::cerr << "OrnsteinUhlenbeckExplore: cannot write to " << dir << "\n";
        return 1;
    }
    for (size_t i = 0; i < c_sigmas.size(); ++i)
    {
        const float sigma = c_sigmas[i];
        const float theta = sigma * 20.0f + 1.0f;
        timeline << "@New plot: title=\"sigma=" << sigma << "\"\n#sigma=" << sigma << "\n";
        const auto deviations = simulate(sigma, 1000 + i);
        for (size_t k = 0; k < deviations.size(); ++k)
        {
            timeline << static_cast<float>(k) / c_stepRate << " " << deviations[k] << "\n";
        }
        autocorr << "@New plot: title=\"sigma=" << sigma << " (theta=" << theta << ")\"\n";
        const auto maxLag = static_cast<size_t>(c_maxLagSeconds * c_stepRate);
        for (int run = 0; run < c_autocorrRuns; ++run)
        {
            autocorr << "#empirical\n";
            writeAutocorrelation(autocorr, simulate(sigma, 3000 + i * 100 + run), c_burnInSteps, maxLag, c_lagStride,
                                 1.0f / c_stepRate);
        }
        autocorr << "#theoretical exp(-theta*tau)\n";
        for (size_t lag = 0; lag <= maxLag; lag += c_lagStride)
        {
            const float tau = static_cast<float>(lag) / c_stepRate;
            autocorr << tau << " " << std::exp(-theta * tau) << "\n";
        }
        distribution << "@New plot: title=\"sigma=" << sigma << " (theta=" << theta << ")\"\n";
        writeDistribution(distribution, sigma, 2000 + i);
    }
    writeStepMethods(stepMethods);
    writeShipAhoyBrown(shipahoy, summary);
    std::cout << "OrnsteinUhlenbeckExplore: wrote 5 plot files and summary.txt to " << dir << "\n";
    return 0;
}
