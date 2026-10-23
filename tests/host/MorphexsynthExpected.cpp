// Expected values for the morphexsynth device test (tools/device_tests/test_morphexsynth.py), measured on
// jp::MorphexsynthPedal itself with the meter's analysis (SignalStats, ZeroCrossingFrequency with the meter's
// hysteresis), so the expectation is not written a second time in Python. Prints JSON on stdout.
//
//   morphexsynth_expected tone SAMPLE_RATE SETTLE WINDOW [key=value...]
//     The notes of the settings are struck at sample 0; after SETTLE seconds the left output is analysed over
//     WINDOW seconds (peak and RMS in dB, zero-crossing frequency), the right output's RMS in dB as well.
//     Keys: the pedal's ports by symbol (patch, vol, cutoff, resonance, attack, release, reverb, chorus, phaser,
//     bendRange, glide, pressure, mpeMaster, mpeLower, mpeUpper) and
//       notes=NOTE:CHANNEL:VELOCITY[,...]   the notes (channels 1 to 16)
//       bend=CHANNEL:VALUE14[,...]          pitch bends sent before the notes (centre 8192)

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "dsp/Analysis/SignalStats.h"
#include "dsp/Analysis/ZeroCrossingFrequency.h"
#include "plugins/morphexsynth/MorphexsynthPedal.h"

namespace
{
constexpr float c_zeroCrossingHysteresis{1e-3f};
constexpr float c_floorDb{-240.0f};

float toDb(const double x)
{
    return x > 0.0 ? static_cast<float>(std::max(20.0 * std::log10(x), static_cast<double>(c_floorDb))) : c_floorDb;
}

std::vector<std::string> split(const std::string& text, const char separator)
{
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= text.size())
    {
        const size_t end = text.find(separator, start);
        parts.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }
    return parts;
}

void midi(jp::MorphexsynthPedal& pedal, const uint8_t status, const uint8_t d1, const uint8_t d2)
{
    const std::array<uint8_t, 3> message{status, d1, d2};
    pedal.midi(message.data(), 3);
}

bool apply(jp::MorphexsynthPedal& pedal, const std::string& setting)
{
    const size_t equals = setting.find('=');
    if (equals == std::string::npos)
    {
        return false;
    }
    const std::string symbol = setting.substr(0, equals);
    const std::string text = setting.substr(equals + 1);
    const auto value = static_cast<float>(std::atof(text.c_str()));
    if (symbol == "patch")
    {
        pedal.setPatch(value);
    }
    else if (symbol == "vol")
    {
        pedal.setVol(value);
    }
    else if (symbol == "cutoff")
    {
        pedal.setCutoff(value);
    }
    else if (symbol == "resonance")
    {
        pedal.setResonance(value);
    }
    else if (symbol == "attack")
    {
        pedal.setAttack(value);
    }
    else if (symbol == "release")
    {
        pedal.setRelease(value);
    }
    else if (symbol == "reverb")
    {
        pedal.setReverb(value);
    }
    else if (symbol == "chorus")
    {
        pedal.setChorus(value);
    }
    else if (symbol == "phaser")
    {
        pedal.setPhaser(value);
    }
    else if (symbol == "bendRange")
    {
        pedal.setBendRange(value);
    }
    else if (symbol == "glide")
    {
        pedal.setGlide(value);
    }
    else if (symbol == "pressure")
    {
        pedal.setPressure(value);
    }
    else if (symbol == "mpeMaster")
    {
        pedal.setMpeMaster(value);
    }
    else if (symbol == "mpeLower")
    {
        pedal.setMpeLower(value);
    }
    else if (symbol == "mpeUpper")
    {
        pedal.setMpeUpper(value);
    }
    else if (symbol == "notes")
    {
        for (const std::string& note : split(text, ','))
        {
            const auto fields = split(note, ':');
            if (fields.size() != 3)
            {
                return false;
            }
            midi(pedal, static_cast<uint8_t>(0x90 | (std::atoi(fields[1].c_str()) - 1)),
                 static_cast<uint8_t>(std::atoi(fields[0].c_str())),
                 static_cast<uint8_t>(std::atoi(fields[2].c_str())));
        }
    }
    else if (symbol == "bend")
    {
        for (const std::string& bend : split(text, ','))
        {
            const auto fields = split(bend, ':');
            if (fields.size() != 2)
            {
                return false;
            }
            const int value = std::atoi(fields[1].c_str());
            midi(pedal, static_cast<uint8_t>(0xE0 | (std::atoi(fields[0].c_str()) - 1)),
                 static_cast<uint8_t>(value & 0x7F), static_cast<uint8_t>(value >> 7));
        }
    }
    else
    {
        return false;
    }
    return true;
}
}

int main(int argc, char** argv)
{
    if (argc < 5 || std::strcmp(argv[1], "tone") != 0)
    {
        std::fprintf(stderr, "usage: morphexsynth_expected tone SAMPLE_RATE SETTLE WINDOW [key=value...]\n");
        return 2;
    }
    const double sampleRate = std::atof(argv[2]);
    const auto settle = static_cast<size_t>(std::atof(argv[3]) * sampleRate);
    const auto window = static_cast<size_t>(std::atof(argv[4]) * sampleRate);
    auto pedal = std::make_unique<jp::MorphexsynthPedal>();
    // the factory patches are the scripts of the source tree (the same files as the bundle on the device)
    pedal->setScriptDirectories(std::string(JP_SOURCE_DIR) + "/plugins/morphexsynth/scripts", "");
    pedal->setSampleRate(static_cast<float>(sampleRate));
    // ports first (in the order given, except the notes and bends), then the MIDI, as the device test does
    std::vector<std::string> midiSettings;
    for (int i = 5; i < argc; ++i)
    {
        const std::string setting = argv[i];
        if (setting.rfind("notes=", 0) == 0 || setting.rfind("bend=", 0) == 0)
        {
            midiSettings.push_back(setting);
        }
        else if (!apply(*pedal, setting))
        {
            std::fprintf(stderr, "bad setting %s\n", argv[i]);
            return 2;
        }
    }
    pedal->reset();
    for (const std::string& setting : midiSettings)
    {
        if (setting.rfind("bend=", 0) == 0)
        {
            apply(*pedal, setting);
        }
    }
    for (const std::string& setting : midiSettings)
    {
        if (setting.rfind("notes=", 0) == 0)
        {
            apply(*pedal, setting);
        }
    }
    std::vector<float> left(settle + window);
    std::vector<float> right(settle + window);
    for (size_t pos = 0; pos < left.size(); pos += 128)
    {
        const size_t n = std::min<size_t>(128, left.size() - pos);
        std::array<float*, 2> outputs{left.data() + pos, right.data() + pos};
        pedal->process(nullptr, outputs.data(), n);
    }
    jp::SignalStats stats;
    jp::SignalStats rightStats;
    jp::ZeroCrossingFrequency crossings(sampleRate, c_zeroCrossingHysteresis);
    for (size_t i = settle; i < left.size(); ++i)
    {
        stats.add(left[i]);
        rightStats.add(right[i]);
        crossings.add(left[i]);
    }
    std::printf("{\"peak\": %.4f, \"rms\": %.4f, \"rightRms\": %.4f, \"frequency\": %.4f}\n", toDb(stats.peak()),
                toDb(stats.rms()), toDb(rightStats.rms()), crossings.frequency());
    return 0;
}
