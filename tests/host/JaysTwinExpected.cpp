// Expected values for the jaystwin device test (tools/device_tests/test_jaystwin.py), measured
// on jp::JaysTwinPedal itself with the device test's signals and the meter's analysis, so the
// expectation is not written a second time in Python. Prints JSON on stdout.
//
//   jaystwin_expected transfer SAMPLE_RATE SETTLE WINDOW LEVEL_DB [symbol=value...]
//     White noise like siggen's (level (2 u - 1)) through the pedal: the meter's band gains
//     (TransferBand, Hann blocks of 16384 samples, H1) at its default band frequencies, and the
//     designed linear response there (makeup x tone stack x bright x small-signal drive gain x
//     cabinet x level x trim) for comparison.
//   jaystwin_expected level SAMPLE_RATE SETTLE WINDOW FREQUENCY LEVEL_DB [symbol=value...]
//     A sine like siggen's (level sin(2 pi f n / fs)) through the pedal: RMS of the output
//     minus RMS of the input in dB over the window.

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "dsp/Analysis/GoertzelBand.h"
#include "dsp/Numbers/Random.h"
#include "plugins/jaystwin/JaysTwinPedal.h"
#include "plugins/meter/MeterPedal.h"
#include "plugins/siggen/SigGenPedal.h"

namespace
{
constexpr std::array<double, 8> c_bandHz{63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0};

bool apply(jp::JaysTwinPedal& pedal, const char* setting)
{
    const char* equals = std::strchr(setting, '=');
    if (equals == nullptr)
    {
        return false;
    }
    const std::string symbol(setting, equals);
    const auto value = static_cast<float>(std::atof(equals + 1));
    if (symbol == "bass")
    {
        pedal.setBass(value);
    }
    else if (symbol == "mid")
    {
        pedal.setMid(value);
    }
    else if (symbol == "treble")
    {
        pedal.setTreble(value);
    }
    else if (symbol == "gain")
    {
        pedal.setGain(value);
    }
    else if (symbol == "level")
    {
        pedal.setLevel(value);
    }
    else if (symbol == "bright")
    {
        pedal.setBright(value);
    }
    else
    {
        return false;
    }
    return true;
}

double designedDb(const jp::JaysTwinPedal& pedal, const double f, const double sampleRate, const float levelDb)
{
    const double chain = std::abs(pedal.getToneStack().response(f) * pedal.getBright().response(f) *
                                  pedal.getCabinet().response(f, sampleRate));
    return 20.0 * std::log10(chain) + static_cast<double>(jp::JaysTwinPedal::c_toneStackMakeupDb) +
           20.0 * std::log10(static_cast<double>(pedal.getDrive().getSmallSignalGain())) +
           static_cast<double>(levelDb + jp::JaysTwinPedal::c_outputDb);
}

int usage()
{
    std::fprintf(stderr,
                 "usage: jaystwin_expected transfer SAMPLE_RATE SETTLE WINDOW LEVEL_DB [symbol=value...]\n"
                 "       jaystwin_expected level SAMPLE_RATE SETTLE WINDOW FREQUENCY LEVEL_DB [symbol=value...]\n");
    return 1;
}
}

int main(int argc, char** argv)
{
    if (argc < 6)
    {
        return usage();
    }
    const std::string mode = argv[1];
    const auto sampleRate = static_cast<float>(std::atof(argv[2]));
    const double settle = std::atof(argv[3]);
    const double window = std::atof(argv[4]);
    const bool transfer = mode == "transfer";
    if (!transfer && (mode != "level" || argc < 7))
    {
        return usage();
    }
    const double frequency = transfer ? 0.0 : std::atof(argv[5]);
    const double levelDb = std::atof(argv[transfer ? 5 : 6]);
    jp::JaysTwinPedal pedal;
    pedal.setSampleRate(sampleRate);
    float levelSetting = jp::JaysTwinPedal::c_defaultLevel;
    for (int i = transfer ? 6 : 7; i < argc; ++i)
    {
        if (!apply(pedal, argv[i]))
        {
            return usage();
        }
        if (std::strncmp(argv[i], "level=", 6) == 0)
        {
            levelSetting = static_cast<float>(std::atof(argv[i] + 6));
        }
    }
    pedal.reset();
    const double amplitude = std::pow(10.0, levelDb / 20.0);
    jp::Random random(jp::SigGenPedal::c_seed);
    std::array<jp::TransferBand, c_bandHz.size()> bands{};
    for (size_t k = 0; k < bands.size(); ++k)
    {
        bands[k].setup(c_bandHz[k], static_cast<double>(sampleRate), jp::MeterPedal::c_bandBlockSize);
    }
    const auto settleSamples = static_cast<size_t>(settle * static_cast<double>(sampleRate));
    const auto total = settleSamples + static_cast<size_t>(window * static_cast<double>(sampleRate));
    double in2 = 0.0;
    double out2 = 0.0;
    constexpr double twoPi{6.283185307179586476925286766559};
    for (size_t i = 0; i < total; ++i)
    {
        const double raw = transfer ? 2.0 * static_cast<double>(random.uniform()) - 1.0
                                    : std::sin(twoPi * frequency * static_cast<double>(i) / sampleRate);
        const auto x = static_cast<float>(amplitude * raw);
        float y = 0.0f;
        pedal.process(&x, &y, 1);
        if (i >= settleSamples)
        {
            in2 += static_cast<double>(x) * x;
            out2 += static_cast<double>(y) * y;
            for (auto& band : bands)
            {
                band.add(x, y);
            }
        }
    }
    if (transfer)
    {
        std::printf("{\"bandHz\": [");
        for (size_t k = 0; k < c_bandHz.size(); ++k)
        {
            std::printf("%s%.17g", k == 0 ? "" : ", ", c_bandHz[k]);
        }
        std::printf("], \"bandGainDb\": [");
        for (size_t k = 0; k < bands.size(); ++k)
        {
            std::printf("%s%.17g", k == 0 ? "" : ", ", 20.0 * std::log10(bands[k].gain()));
        }
        std::printf("], \"designedDb\": [");
        for (size_t k = 0; k < c_bandHz.size(); ++k)
        {
            std::printf("%s%.17g", k == 0 ? "" : ", ",
                        designedDb(pedal, c_bandHz[k], static_cast<double>(sampleRate), levelSetting));
        }
        std::printf("]}\n");
    }
    else
    {
        std::printf("{\"levelDb\": %.17g}\n", 10.0 * std::log10(out2 / in2));
    }
    return 0;
}
