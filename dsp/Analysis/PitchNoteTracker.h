#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "dsp/Numbers/Convert.h"

namespace jp
{

/// One pitch reading per hop, as a detector delivers it: hz <= 0 means unvoiced; confidence is 1 - CMNDF of YIN
/// (a periodicity measure, not a probability); level is a linear RMS; stamp is carried through to the event.
struct PitchObservation
{
    float hz{0.0f};
    float confidence{0.0f};
    float level{0.0f};
    uint64_t stamp{0};
};

/// An accepted note, or the end of one (released): pitch is the mean over the hops that made it stable, in fractional
/// MIDI notes, note the nearest integer, hz the frequency of pitch. A release carries the note that ended.
struct PitchNote
{
    float hz{0.0f};
    float pitch{0.0f};
    int note{0};
    float confidence{0.0f};
    float level{0.0f};
    uint64_t stamp{0};
    bool released{false};
};

/// Thresholds of PitchNoteTracker; the values are what the tests and the charts of documentation/YinPitch/ were made
/// with.
struct PitchNoteTrackerSettings
{
    float minLevelDb{-50.0f};         ///< RMS below this (dBFS) is unvoiced
    float minConfidence{0.85f};       ///< confidence below this is unvoiced
    size_t stableHops{3};             ///< readings within the tolerance that make a note
    float toleranceCents{50.0f};      ///< the spread of readings that still count as one pitch
    float hysteresisSemitones{0.65f}; ///< distance from the playing note beyond which a reading is a new candidate
    size_t releaseHops{3};            ///< unvoiced readings that end a note
    float attackRiseDb{6.0f};         ///< rise over the lowest level since the onset that counts as a new attack
};

/**
 * @brief Turns a stream of pitch readings (one per hop) into note events: a note when a pitch has been stable, a
 * release when the voice has gone, a note again when the same pitch is struck anew.
 *
 * A reading is voiced when its level and confidence pass the thresholds; a pitch becomes a note when stableHops voiced
 * readings in a row stay within toleranceCents of their running mean, so one stray octave reading or a click does
 * not make a note, and the note starts that many hops before the event (the event is that much late). While a note
 * plays, readings within hysteresisSemitones of it belong to it (vibrato, a pitch that sits between two notes does
 * not flicker); a reading farther away is a candidate for the next note and needs the same stability. A single
 * unvoiced reading does not end a note, releaseHops in a row do; silence never produces a 0 Hz note. The same note is
 * struck again only after a release, or while it plays when the level rises by attackRiseDb over the lowest level
 * seen since the onset (or the last strike), which a decaying string never does by itself.
 *
 * Plain logic on one reading at a time, no allocation; time is counted in hops and the stamp of the reading that
 * completes an event is passed through to it.
 */
class PitchNoteTracker
{
  public:
    explicit PitchNoteTracker(const PitchNoteTrackerSettings& settings = {}) noexcept
        : m_settings(settings)
        , m_minLevel(dbToGain(settings.minLevelDb))
        , m_attackRatio(dbToGain(settings.attackRiseDb))
    {
    }

    void reset() noexcept
    {
        m_playing = false;
        m_note = 0;
        m_candidateCount = 0;
        m_candidateSum = 0.0f;
        m_unvoiced = 0;
        m_lowestLevel = 0.0f;
    }

    [[nodiscard]] bool isPlaying() const noexcept
    {
        return m_playing;
    }

    /// The note that plays, meaningful while isPlaying().
    [[nodiscard]] int getNote() const noexcept
    {
        return m_note;
    }

    /// Takes the next reading; returns the event it completes, if any.
    [[nodiscard]] std::optional<PitchNote> update(const PitchObservation& reading) noexcept
    {
        const bool voiced =
            reading.hz > 0.0f && reading.confidence >= m_settings.minConfidence && reading.level >= m_minLevel;
        if (!voiced)
        {
            return unvoiced(reading);
        }
        m_unvoiced = 0;
        const float pitch = frequencyToNote(reading.hz);
        if (m_playing && std::abs(pitch - static_cast<float>(m_note)) <= m_settings.hysteresisSemitones)
        {
            m_candidateCount = 0;
            return playingNote(reading);
        }
        return candidate(reading, pitch);
    }

  private:
    [[nodiscard]] std::optional<PitchNote> unvoiced(const PitchObservation& reading) noexcept
    {
        m_candidateCount = 0;
        if (!m_playing || ++m_unvoiced < m_settings.releaseHops)
        {
            return std::nullopt;
        }
        m_playing = false;
        PitchNote event = noteOf(static_cast<float>(m_note), reading);
        event.released = true;
        return event;
    }

    /// A reading that belongs to the playing note: a strike again when the level has risen enough.
    [[nodiscard]] std::optional<PitchNote> playingNote(const PitchObservation& reading) noexcept
    {
        if (reading.level >= m_lowestLevel * m_attackRatio)
        {
            m_lowestLevel = reading.level;
            PitchNote event = noteOf(frequencyToNote(reading.hz), reading);
            event.note = m_note;
            return event;
        }
        m_lowestLevel = std::min(m_lowestLevel, reading.level);
        return std::nullopt;
    }

    [[nodiscard]] std::optional<PitchNote> candidate(const PitchObservation& reading, const float pitch) noexcept
    {
        const float mean = m_candidateCount > 0 ? m_candidateSum / static_cast<float>(m_candidateCount) : pitch;
        if (m_candidateCount > 0 && std::abs(pitch - mean) * 100.0f > m_settings.toleranceCents)
        {
            m_candidateCount = 0;
        }
        m_candidateSum = m_candidateCount > 0 ? m_candidateSum + pitch : pitch;
        m_candidateCount = m_candidateCount > 0 ? m_candidateCount + 1 : 1;
        if (m_candidateCount < m_settings.stableHops)
        {
            return std::nullopt;
        }
        const float accepted = m_candidateSum / static_cast<float>(m_candidateCount);
        m_candidateCount = 0;
        m_playing = true;
        m_note = static_cast<int>(std::lround(accepted));
        m_lowestLevel = reading.level;
        return noteOf(accepted, reading);
    }

    [[nodiscard]] static PitchNote noteOf(const float pitch, const PitchObservation& reading) noexcept
    {
        PitchNote event;
        event.pitch = pitch;
        event.note = static_cast<int>(std::lround(pitch));
        event.hz = noteToFrequency(pitch);
        event.confidence = reading.confidence;
        event.level = reading.level;
        event.stamp = reading.stamp;
        return event;
    }

    PitchNoteTrackerSettings m_settings;
    float m_minLevel;
    float m_attackRatio;
    bool m_playing{false};
    int m_note{0};
    size_t m_candidateCount{0};
    float m_candidateSum{0.0f};
    size_t m_unvoiced{0};
    float m_lowestLevel{0.0f};
};

}
