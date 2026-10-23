// Expected values for the eigenghost device test (tools/device_tests/test_eigenghost.py): the
// wet level of white noise relative to the input, per decay setting, measured on
// jp::EigenghostPedal itself with the device test's settings (dry muted, wet 0 dB, no pitch
// shifting, the rest at the defaults) and its timing (settle, then one window). The noise is
// siggen's (uniform, 2 u - 1); the ratio does not depend on its level. Prints JSON on stdout,
// so the expectation is not written a second time in Python.
//
// Usage: eigenghost_expected SAMPLE_RATE SETTLE_SECONDS WINDOW_SECONDS DECAY_SECONDS...

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "dsp/Numbers/Random.h"
#include "plugins/eigenghost/EigenghostPedal.h"

namespace
{
double wetLevelDb(const float sampleRate, const double settle, const double window, const float decay)
{
    jp::EigenghostPedal pedal;
    pedal.setSampleRate(sampleRate);
    pedal.setDry(jp::EigenghostPedal::c_muteDb);
    pedal.setWet(0.0f);
    pedal.setShimmer(0.0f);
    pedal.setDecay(decay);
    pedal.reset();
    jp::Random random(5);
    const auto settleSamples = static_cast<size_t>(settle * sampleRate);
    const auto total = settleSamples + static_cast<size_t>(window * sampleRate);
    double in2 = 0.0;
    double out2 = 0.0;
    float left = 0.0f;
    float right = 0.0f;
    for (size_t i = 0; i < total; ++i)
    {
        const float x = 2.0f * random.uniform() - 1.0f;
        const float* in[1] = {&x};
        float* out[2] = {&left, &right};
        pedal.process(in, out, 1);
        if (i >= settleSamples)
        {
            in2 += static_cast<double>(x) * x;
            out2 += static_cast<double>(left) * left;
        }
    }
    return 10.0 * std::log10(out2 / in2);
}
}

int main(int argc, char** argv)
{
    if (argc < 5)
    {
        std::fprintf(stderr, "usage: eigenghost_expected SAMPLE_RATE SETTLE_SECONDS WINDOW_SECONDS DECAY_SECONDS...\n");
        return 1;
    }
    const auto sampleRate = static_cast<float>(std::atof(argv[1]));
    const double settle = std::atof(argv[2]);
    const double window = std::atof(argv[3]);
    std::printf("{\"sampleRate\": %.17g, \"settle\": %.17g, \"window\": %.17g, \"wetLevelDb\": {", sampleRate, settle,
                window);
    for (int i = 4; i < argc; ++i)
    {
        const auto decay = static_cast<float>(std::atof(argv[i]));
        std::printf("%s\"%s\": %.17g", i == 4 ? "" : ", ", argv[i], wetLevelDb(sampleRate, settle, window, decay));
    }
    std::printf("}}\n");
    return 0;
}
