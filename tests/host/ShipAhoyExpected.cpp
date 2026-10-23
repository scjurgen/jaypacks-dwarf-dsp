// Expected values for the shipahoy device test (tools/device_tests/test_shipahoy.py): the gain
// of every filter type at the meter's band frequencies, from the same transfer function the
// Mac tests compare with (poleMixingResponse, no resonance, peak tuned to the centre
// frequency). Prints JSON on stdout, so the theory is not written a second time in Python.
//
// Usage: shipahoy_expected CENTRE_HZ SAMPLE_RATE BAND_HZ...

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "dsp/Filters/PoleMixingFilter.h"
#include "plugins/shipahoy/ShipAhoyEngine.h"

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr, "usage: shipahoy_expected CENTRE_HZ SAMPLE_RATE BAND_HZ...\n");
        return 1;
    }
    const double centre = std::atof(argv[1]);
    const double sampleRate = std::atof(argv[2]);
    std::vector<double> bands;
    for (int i = 3; i < argc; ++i)
    {
        bands.push_back(std::atof(argv[i]));
    }
    const double pole = jp::poleMixingTuningForPeak(centre, sampleRate).pole;
    std::printf("{\"centreHz\": %.17g, \"sampleRate\": %.17g, \"types\": [\n", centre, sampleRate);
    for (size_t type = 0; type < jp::ShipAhoyEngine::c_filterTypeCount; ++type)
    {
        const jp::PoleMixingPreset& preset = jp::c_poleMixingPresets[jp::ShipAhoyEngine::c_filterTypePresets[type]];
        std::printf("  {\"index\": %zu, \"label\": \"%s\", \"gainsDb\": [", type,
                    jp::ShipAhoyEngine::c_filterTypeLabels[type]);
        for (size_t k = 0; k < bands.size(); ++k)
        {
            const double gain = std::abs(jp::poleMixingResponse(preset, pole, 0.0, bands[k], sampleRate));
            std::printf("%s%.17g", k == 0 ? "" : ", ", 20.0 * std::log10(std::max(gain, 1e-12)));
        }
        std::printf("]}%s\n", type + 1 < jp::ShipAhoyEngine::c_filterTypeCount ? "," : "");
    }
    std::printf("]}\n");
    return 0;
}
