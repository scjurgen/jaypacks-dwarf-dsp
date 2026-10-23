#pragma once

#include <array>
#include <cstddef>

#include "dsp/Filters/BilinearTransform.h"
#include "dsp/Filters/Biquad.h"
#include "dsp/Filters/BiquadCascade.h"

namespace jp
{

/// Jensen C12K, 8 ohm, on an IEC baffle: seven cookbook sections fitted at 48 kHz by
/// documentation/JaysTwin/fit_cabinet.py to the curve of the Jensen specification sheet
/// (documentation/JaysTwin/c12k_8ohm.csv): 0.68 dB rms over 30 Hz to 8 kHz, at most 2.4 dB off
/// up to 6 kHz, unity gain at the 500 Hz to 2 kHz level. Two resonance highpasses, the dip at
/// 410 Hz, the narrow presence peak at 2.3 kHz, the cut at 6.8 kHz and two lowpasses for the
/// fall above 4 kHz. Designed at another rate the sections keep their centre frequencies (the
/// designs prewarp) and differ only near Nyquist.
inline constexpr std::array<BiquadDesign, 7> c_jensenC12KSections{{
    {BiquadFilterType::HighPass, 124.9f, 1.552f, 0.0f},
    {BiquadFilterType::HighPass, 66.9f, 0.500f, 0.0f},
    {BiquadFilterType::Peak, 410.4f, 2.481f, -4.35f},
    {BiquadFilterType::Peak, 2296.9f, 6.000f, 3.63f},
    {BiquadFilterType::Peak, 6789.4f, 4.000f, -11.32f},
    {BiquadFilterType::LowPass, 3167.0f, 1.175f, 0.0f},
    {BiquadFilterType::LowPass, 8056.6f, 1.500f, 0.0f},
}};

/// The open back of the Twin's cabinet as a first-order highpass at this frequency. An
/// assumption, not a measurement: the baffle curve has no rear wave, and an open back loses
/// low end where the rear wave reaches the front in antiphase. To be replaced by a measurement.
inline constexpr double c_twinOpenBackHighpassHz{80.0};

/// Number of sections of the Twin cabinet: the speaker and the open back.
inline constexpr size_t c_twinCabinetSections{c_jensenC12KSections.size() + 1};

/**
 * Sets a cascade to the Fender Twin Reverb's 2x12 open-back cabinet with Jensen C12K speakers:
 * the fitted speaker sections followed by the open-back highpass. The two speakers are taken as
 * one (same response, no interference between them). Call outside the audio path.
 */
inline void designTwinCabinet(BiquadCascade<c_twinCabinetSections>& cascade, const float sampleRate)
{
    for (size_t i = 0; i < c_jensenC12KSections.size(); ++i)
    {
        cascade.setSection(i, designBiquad(c_jensenC12KSections[i], sampleRate));
    }
    cascade.setSection(c_jensenC12KSections.size(),
                       firstOrderHighpassSection(c_twinOpenBackHighpassHz, static_cast<double>(sampleRate)));
}

}
