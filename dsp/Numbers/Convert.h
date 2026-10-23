#pragma once

#include <cmath>

namespace jp
{

/// Frequency of a MIDI note number (69 is A at the tuning, 440 Hz by default); fractional notes are cents.
[[nodiscard]] inline float noteToFrequency(const float note, const float tuning = 440.0f) noexcept
{
    return tuning * std::exp2((note - 69.0f) / 12.0f);
}

/// MIDI note number (fractional: cents) of a frequency, the inverse of noteToFrequency(); the frequency must be above
/// 0.
[[nodiscard]] inline float frequencyToNote(const float frequency, const float tuning = 440.0f) noexcept
{
    return 69.0f + 12.0f * std::log2(frequency / tuning);
}

/// Frequency ratio of an interval in semitones, 2^(semitones / 12); negative intervals are below 1.
[[nodiscard]] inline float noteIntervalToRatio(const float semitones) noexcept
{
    return std::exp2(semitones / 12.0f);
}

/// Decibels to an amplitude ratio, 10^(dB / 20).
[[nodiscard]] inline float dbToGain(const float decibels) noexcept
{
    return std::pow(10.0f, decibels / 20.0f);
}

}
