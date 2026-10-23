#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "dsp/Filters/OnePoleLowpass.h"
#include "dsp/Filters/PoleMixingBandpassFilter.h"
#include "dsp/Filters/PoleMixingFilter.h"
#include "dsp/Generators/AdsEnvelope.h"
#include "dsp/Generators/SynthLfo.h"
#include "dsp/NonLinear/WaveShaperTables.h"
#include "dsp/Numbers/Convert.h"
#include "dsp/Numbers/PanLaw.h"
#include "dsp/Numbers/Random.h"
#include "dsp/Parameters/FixedSmoothing.h"
#include "dsp/Synthesizer/MpeRouting.h"
#include "dsp/Synthesizer/PitchQuantize.h"
#include "dsp/Synthesizer/ValueConnector.h"
#include "dsp/Wavetables/WaveTableBank.h"
#include "dsp/Wavetables/WaveTableOscillator.h"

namespace jp
{

/// Sources of the voice's modulation targets (the inputs of the formulas in MorphexsynthConnectors).
enum class CtrlSource
{
    None,
    BaseValue,
    Key,
    KeyFollow,
    Velocity,
    VelocityFollow,
    ContourEnvFrequency,
    EnvelopeFilter,
    Dimension,
    Lfo,
    EnvelopePitch,
    EnvelopeGlide,
    Pitch
};

/**
 * @brief The voice's modulation targets: each sums its sources with a formula and smooths the result.
 *
 * Cutoff and pitch bend in semitones, resonance and levels as fractions. The cutoff has a fixed part that changes
 * at a note trigger (key and velocity following) and a variable part for what moves while the voice sounds.
 */
struct MorphexsynthConnectors
{
    // The cutoff and the resonance are smoothed over 16 control ticks (5.3 ms), not the 128 (43 ms) of the other
    // levels: the cutoff's target is set at every tick (envelope, LFO, controllers), so a ramp of N steps that
    // restarts at every tick is a one-pole approach with a time constant of N ticks (43 ms: 90 % after 100 ms,
    // which AbacDsp has and which makes a filter envelope and a slide of Y or Z lag).
    static constexpr size_t c_filterSteps{16};
    using FilterCutoffConnector =
        ValueConnectorWithPreset<c_filterSteps, CtrlSource, CtrlSource::BaseValue, CtrlSource::Key,
                                 CtrlSource::KeyFollow, CtrlSource::Velocity, CtrlSource::VelocityFollow,
                                 CtrlSource::ContourEnvFrequency, CtrlSource::EnvelopeFilter, CtrlSource::Dimension,
                                 CtrlSource::Lfo>;
    using LevelConnector = ValueConnector<128, CtrlSource, CtrlSource::BaseValue, CtrlSource::Dimension>;
    using ResonanceConnector = ValueConnector<c_filterSteps, CtrlSource, CtrlSource::BaseValue, CtrlSource::Dimension>;
    using OscFrequencyConnector = ValueConnector<128, CtrlSource, CtrlSource::BaseValue, CtrlSource::Pitch>;
    using PitchBendConnector = ValueConnector<32, CtrlSource, CtrlSource::BaseValue, CtrlSource::Dimension,
                                              CtrlSource::EnvelopePitch, CtrlSource::EnvelopeGlide, CtrlSource::Lfo>;

    FilterCutoffConnector filterCutoff{[](const FilterCutoffConnector::Values& v) { return v[1] * v[2] + v[3] * v[4]; },
                                       [](const float fixedPart, const FilterCutoffConnector::Values& v)
                                       { return fixedPart + v[0] + v[5] * v[6] + v[7] + v[8]; }};

    ResonanceConnector filterResonance{[](const ResonanceConnector::Values& v) { return v[0] * 1.5f + v[1]; }};
    LevelConnector sustainLevel{[](const LevelConnector::Values& v) { return std::clamp(v[0] + v[1], -1.0f, 1.0f); }};
    LevelConnector osc1Level{[](const LevelConnector::Values& v) { return std::clamp(v[0] + v[1], -1.0f, 1.0f); }};
    LevelConnector osc2Level{[](const LevelConnector::Values& v) { return std::clamp(v[0] + v[1], -1.0f, 1.0f); }};
    LevelConnector osc3Level{[](const LevelConnector::Values& v) { return std::clamp(v[0] + v[1], -1.0f, 1.0f); }};

    OscFrequencyConnector oscFrequency{[](const OscFrequencyConnector::Values& v)
                                       { return std::clamp(v[0] + v[1], 0.0f, 120.0f); }};

    PitchBendConnector pitchBend{[](const PitchBendConnector::Values& v)
                                 { return noteIntervalToRatio(v[0] + v[1] + v[2] + v[3] + v[4]); }};

    FixedSmoothing<128> panning{};
    FixedSmoothing<128> distortion{};
};

/**
 * @brief A subtractive synth voice: three wavetable oscillators through a resonant pole-mixing filter, amplitude,
 * filter and pitch envelopes, an LFO, a distortion stage and a 10-slot routing matrix for the MPE dimensions.
 *
 * Signal path per sample: the oscillators (each with its level, detune, pulse width), a small fixed dither, the
 * filter, the velocity gain, the amplitude envelope (clamped to +-4), a one-pole lowpass if the filter type is not
 * the 4-pole lowpass, the distortion (gain, table, 2 / gain) if a preset is chosen, and the pan law. Everything
 * that is not audio rate runs once per window of 16 samples (the control rate of 3 kHz at 48 kHz): the LFO, the
 * filter and pitch envelopes, the routing, the filter's target pole and resonance. The oscillators produce each
 * window one window ahead (their frequency follows the control update with a delay of 16 samples, and the first
 * 16 samples after a trigger are silent). The audio output does not depend on how processBlock() is cut: the
 * calls are split at the windows, and the filter's pole and resonance follow their targets by a one-pole smoother
 * of 3 ms per sample.
 *
 * Filter: PoleMixingBandpassFilter, sample-exact the filter of AbacDsp (Filter1Pole4StageSmooth, checked against it
 * on 20 combinations of preset and resonance with a moving cutoff, largest difference 0): the stages' corner and the
 * resonance come from the fitted warp and threshold (PoleMixingCorrections.h), resonance 1 is the self-oscillation
 * threshold and the dial value is multiplied by 1.5 on its way to the filter (dial 0.6 is 0.9 of the threshold),
 * limited to 2; pole and resonance follow their targets by a smoother of 3 ms per sample. The loop input is
 * saturated (x / sqrt(1 + x^2)) even without resonance, so a loud oscillator sum is compressed before the filter (a
 * triangle of peak 0.87 leaves at 0.66) and the level is linear only well below 1. Measured with a saw (level 0.5,
 * note 43) the voice has the same RMS as AbacDsp's to three digits at the four cutoffs tried (0.166 to 0.047). The
 * cutoff is limited to [10 Hz, 22 kHz] by the warp, and its fit is for 48 kHz. The cutoff and resonance connectors
 * smooth over 16 control ticks (5.3 ms) where AbacDsp has 128 (a time constant of 43 ms for the cutoff, whose target is
 * set at every tick): the comparison above holds for a cutoff that does not move.
 *
 * Quirks kept from the source: the shaper gain is 2 until a Distortion slot moves it (a note trigger sets the
 * connector to 0.125 but not the gain); the envelope dimensions are fed as 0 to 8191 of the 14-bit range, velocity
 * and note as 128 times their 7-bit value. Sources the patches do not use (random detune, the quantised pitch bend,
 * key following of oscillator 3) are kept as the setters of the source.
 *
 * waveShaperTables and the wavetable bank are large, read-only and shared by all voices of a plugin (they must
 * outlive the voices; the bank must hold Triangle, SharkFin, Saw, Square and White). No allocation after
 * construction, real-time safe.
 */
class MorphexsynthVoice
{
  public:
    static constexpr size_t c_granularity{16};
    static constexpr size_t c_numOscillators{3};
    static constexpr size_t c_numMpeSlots{c_mpeSlotCount};
    static constexpr float c_glideCurve{0.6f};
    static constexpr float c_filterSmoothingMs{3.0f};

    MorphexsynthVoice(const float sampleRate, const WaveTableBank& wavetables,
                      const WaveShaperTableStore& waveShaperTables, const uint64_t seed = Random::c_defaultSeed)
        : m_sampleRate(sampleRate)
        , m_oscillators{{Oscillator(sampleRate, wavetables, seed), Oscillator(sampleRate, wavetables, seed + 1),
                         Oscillator(sampleRate, wavetables, seed + 2)}}
        , m_lfo(sampleRate / static_cast<float>(c_granularity), seed + 3)
        , m_envAmplitude(sampleRate)
        , m_envFilter(sampleRate / static_cast<float>(c_granularity))
        , m_envPitch(sampleRate / static_cast<float>(c_granularity))
        , m_envGlide(sampleRate / static_cast<float>(c_granularity))
        , m_filter(sampleRate)
        , m_waveShaperTables(waveShaperTables)
        , m_random(seed + 4)
    {
        for (auto& osc : m_oscillators)
        {
            osc.oscillator.setWave(BasicWave::Triangle);
        }
        m_lp4Index = findPoleMixingPreset("4 pole lp");
        setFilterPreset(m_lp4Index);
        m_lowpassStage.setPole(onePoleLowpassPole(1000.0f, sampleRate));
        m_filter.setSmoothingMs(c_filterSmoothingMs);
        m_filter.setCutoffFrequency(5000.0f);
        m_filter.setResonance(0.0f);
        m_filter.reset();
        setEnvelopeAmplitudeShape();
        setEnvelopeFilterShape();
    }

    // --- oscillators ---

    /// 0 Triangle, 1 SharkFin, 2 Saw, 3 Square, 4 White; larger values select White.
    void setWaveForm(const size_t oscillatorIndex, const size_t index) noexcept
    {
        if (oscillatorIndex < c_numOscillators)
        {
            m_oscillators[oscillatorIndex].oscillator.setWave(waveOf(index));
        }
    }

    /// The morph of an oscillator: the waveforms of its second and third slot (numbered as for setWaveForm(), which
    /// sets the first) and the position over the three in 0 to 1 (0 the first, 0.5 the second, 1 the third; a move
    /// takes 10 ms). Position 0 sounds exactly as an oscillator that never morphs.
    void setWaveMorph(const size_t oscillatorIndex, const size_t second, const size_t third,
                      const float position) noexcept
    {
        if (oscillatorIndex < c_numOscillators)
        {
            auto& oscillator = m_oscillators[oscillatorIndex].oscillator;
            oscillator.setWaveset(1, waveOf(second));
            oscillator.setWaveset(2, waveOf(third));
            oscillator.setMorph(position);
        }
    }

    /// How oscillator 3 follows the note: 1 follows it fully (the default), 0 holds middle C, 0.5 follows half the
    /// interval.
    void setOsc3KeyFollow(const float octaves) noexcept
    {
        m_osc3KeyFollowFactor = octaves;
    }

    void setLevelOscillator(const size_t oscillatorIndex, const float value) noexcept
    {
        switch (oscillatorIndex)
        {
            case 0:
                m_connectors.osc1Level.set(CtrlSource::BaseValue, value);
                break;
            case 1:
                m_connectors.osc2Level.set(CtrlSource::BaseValue, value);
                break;
            case 2:
                m_connectors.osc3Level.set(CtrlSource::BaseValue, value);
                break;
            default:
                break;
        }
    }

    /// Pulse width of an oscillator in -1 to 1: above 0 the soft pulse-width modulation with offset 0.5 + value / 2,
    /// otherwise off.
    void setPwmOscillator(const size_t oscillatorIndex, const float value) noexcept
    {
        if (oscillatorIndex >= c_numOscillators)
        {
            return;
        }
        auto& oscillator = m_oscillators[oscillatorIndex].oscillator;
        oscillator.setPwmMode(value > 0.0f ? WaveTableOscillator::PwmMode::Soft : WaveTableOscillator::PwmMode::Off);
        oscillator.setPwm(0.5f + 0.5f * std::clamp(value, -1.0f, 1.0f));
    }

    /// Detune in semitones.
    void setDetune(const size_t oscillatorIndex, const float value) noexcept
    {
        if (oscillatorIndex < c_numOscillators)
        {
            m_oscillators[oscillatorIndex].detune = noteIntervalToRatio(value);
            updateOscillatorFrequency(oscillatorIndex);
        }
    }

    /// Frequency multiplier of an oscillator against the note (1 unison, 2 an octave up).
    void setPitchFactor(const size_t oscillatorIndex, const float value) noexcept
    {
        if (oscillatorIndex < c_numOscillators)
        {
            m_oscillators[oscillatorIndex].pitchFactor = value;
        }
    }

    // --- LFO ---

    void setLfoWaveForm(const LfoType type) noexcept
    {
        m_lfo.setWaveForm(type);
    }

    /// LFO speed in Hz at middle C; set at the next note trigger.
    void setLfoPitchFactor(const float value) noexcept
    {
        m_lfoBaseFrequency = value;
    }

    /// Pitch modulation depth in units of 12 semitones (applied to all oscillators).
    void setLfoOscModulationDepth(const float value) noexcept
    {
        m_lfoOscModulationDepth = value;
    }

    /// Cutoff modulation depth in octaves.
    void setLfoFilterModulationDepth(const float value) noexcept
    {
        m_lfoFilterModulationDepth = value;
    }

    /// How much the LFO speed follows the note: octaves of speed per octave of pitch (0 fixed).
    void setLfoKeyFollow(const float octaves) noexcept
    {
        m_lfoKeyFollowFactor = octaves;
    }

    // --- amplitude and filter envelopes (times in ms) ---

    void setEnvelopeAttack(const float t) noexcept
    {
        m_adsrAmplitude.attack = t;
        m_envAmplitude.setSegment(0, t, 1.0f, 1.0f);
    }

    void setEnvelopeDecay(const float t) noexcept
    {
        m_adsrAmplitude.decay = t;
        m_envAmplitude.setSegment(1, t, m_adsrAmplitude.sustainLevel, 1.0f);
    }

    void setEnvelopeSustainLevel(const float v) noexcept
    {
        m_adsrAmplitude.sustainLevel = v;
        m_connectors.sustainLevel.set(CtrlSource::BaseValue, v);
    }

    void setEnvelopeRelease(const float t) noexcept
    {
        m_adsrAmplitude.release = t;
        m_envAmplitude.setSegment(3, t, 0.0f, 1.0f);
    }

    void setEmergencyReleaseTime(const float t) noexcept
    {
        m_envAmplitude.emergencyRelease(t);
    }

    void setEnvelopeAttackFilter(const float t) noexcept
    {
        m_adsrFilter.attack = t;
        m_envFilter.setSegment(0, t, 1.0f, 1.0f);
    }

    void setEnvelopeDecayFilter(const float t) noexcept
    {
        m_adsrFilter.decay = t;
        m_envFilter.setSegment(1, t, m_adsrFilter.sustainLevel, 1.0f);
    }

    void setEnvelopeSustainLevelFilter(const float v) noexcept
    {
        m_adsrFilter.sustainLevel = v;
        m_envFilter.setSegment(1, m_adsrFilter.decay, v, 1.0f);
        m_envFilter.setSegmentHoldPreviousValue(2, 1000.0f);
        m_envFilter.modifyTargetIfActive(1);
        m_envFilter.quickModifyIfSegmentActive(2, 1000);
    }

    void setEnvelopeReleaseFilter(const float t) noexcept
    {
        m_adsrFilter.release = t;
        m_envFilter.setSegment(3, t, 0.0f, 1.0f);
    }

    // --- pitch envelope and glide ---

    void setPitchAttack(const float t) noexcept
    {
        m_pitchAttackTime = t;
        m_envPitch.setSegment(0, m_pitchAttackTime, m_pitchFactorAD, 1.0f);
        m_envPitch.noSustain();
    }

    void setPitchDecay(const float t) noexcept
    {
        m_envPitch.setSegment(1, t, 0.0f, 1.0f);
        m_envPitch.noSustain();
    }

    /// Depth of the pitch envelope in semitones.
    void setPitchEnvelopeDepth(const float t) noexcept
    {
        m_pitchFactorAD = t;
        m_envPitch.setSegment(0, m_pitchAttackTime, m_pitchFactorAD, 1.0f);
        m_envPitch.noSustain();
    }

    /// Portamento time per octave in ms (0 off).
    void setGlide(const float glideMsPerOctave) noexcept
    {
        m_glideTimePerOctave = glideMsPerOctave;
    }

    void setQuantizePitchbend(const float quantize) noexcept
    {
        m_quantizePitchbend = quantize;
    }

    /// Random detune per note: normal distribution with this standard deviation in semitones (0 off).
    void setRandomDetune(const float detuneSemitones) noexcept
    {
        m_randDetune = detuneSemitones;
    }

    /// Velocity range in 0 to 1: the gain is (v x f + 1 - f)^3 with v = velocity / 127 and f = 1 - cbrt(range), so 0
    /// lets the gain follow the velocity (cubed) and 1 makes every velocity equally loud. Until this is called the
    /// range is 0.15 (f = 0.47: velocity 64 is 7 dB and velocity 30 is 12 dB below velocity 127; the source's range
    /// member says 0.15 as well, although it starts with an f of 0.1, which gave only 2 dB between 30 and 127).
    void setDynamicRangeForVelocity(const float range) noexcept
    {
        m_dynamicRange = range;
        m_dynamicRangeFactor = 1.0f - std::cbrt(m_dynamicRange);
    }

    // --- filter ---

    /// Base cutoff as a note number (the scale of the Cutoff dial), set at once.
    void setCutoff(const float semitone) noexcept
    {
        m_connectors.filterCutoff.set(CtrlSource::BaseValue, semitone);
        m_connectors.filterCutoff.presetFast();
    }

    /// Normalised resonance (1 is the self-oscillation threshold); the filter limits it to 2.
    void setResonance(const float reso) noexcept
    {
        m_connectors.filterResonance.set(CtrlSource::BaseValue, reso);
    }

    /// Filter type as a row of c_poleMixingPresets (an index outside the table is ignored).
    void setFilterPreset(const size_t index) noexcept
    {
        if (index >= c_poleMixingPresetCount)
        {
            return;
        }
        m_filterIndex = index;
        m_filter.setPreset(index);
    }

    /// Filter type by the name of c_poleMixingPresets ("4 pole lp", "2 pole hp" ...); an unknown name is ignored.
    void setFilterType(const std::string_view name) noexcept
    {
        setFilterPreset(findPoleMixingPreset(name));
    }

    /// Cutoff in Hz of the extra lowpass that follows when the filter type is not the 4-pole lowpass.
    void setLowpassCutoff(const float value) noexcept
    {
        m_lowpassStage.setPole(onePoleLowpassPole(value, m_sampleRate));
    }

    /// Octaves of cutoff per octave of key, relative to middle C.
    void setKeyFollow(const float octaves) noexcept
    {
        m_connectors.filterCutoff.set(CtrlSource::KeyFollow, octaves * 12.0f);
    }

    /// Octaves of cutoff for the full velocity range.
    void setVeloFollow(const float octaves) noexcept
    {
        m_connectors.filterCutoff.set(CtrlSource::VelocityFollow, octaves * 12.0f);
    }

    /// Octaves the filter envelope moves the cutoff (0 none).
    void setContourF(const float contour) noexcept
    {
        m_connectors.filterCutoff.set(CtrlSource::ContourEnvFrequency, contour * 12.0f);
    }

    // --- distortion ---

    /// 0 off, 1 to 31 a preset of distortionPresets() (the first is 1).
    void setPresetWaveTable(const size_t index) noexcept
    {
        m_waveTableIndex = index;
    }

    // --- routing matrix ---

    void setCtrlDimension(const size_t slot, const CtrlDimension dimension) noexcept
    {
        if (slot < c_numMpeSlots)
        {
            m_mpeCtrl[slot].source = dimension;
        }
    }

    void setCtrlCurve(const size_t slot, const CtrlCurve curve) noexcept
    {
        if (slot < c_numMpeSlots)
        {
            m_mpeCtrl[slot].curve = curve;
        }
    }

    void setCtrlTarget(const size_t slot, const CtrlTarget target) noexcept
    {
        if (slot < c_numMpeSlots)
        {
            m_mpeCtrl[slot].target = target;
        }
    }

    void setCtrlType(const size_t slot, const CtrlValueType type) noexcept
    {
        if (slot < c_numMpeSlots)
        {
            m_mpeCtrl[slot].type = type;
        }
    }

    void setCtrlDepth(const size_t slot, const float value) noexcept
    {
        if (slot < c_numMpeSlots)
        {
            m_mpeCtrl[slot].depth = value;
        }
    }

    /// The per-note pitch bend, CC74 and channel pressure as 14-bit values (0 to 16383, centre 8192).
    void mpeX(const int value14Bit) noexcept
    {
        applyCtrl(CtrlDimension::X, value14Bit);
    }

    void mpeY(const int value14Bit) noexcept
    {
        applyCtrl(CtrlDimension::Y, value14Bit);
    }

    void mpeZ(const int value14Bit) noexcept
    {
        applyCtrl(CtrlDimension::Z, value14Bit);
    }

    // --- lifecycle ---

    /// Starts a note; lastNote is the note before (0 for none) for the glide. A voice that is silent starts its
    /// oscillators and its LFO at phase 0, so that a note sounds the same on every voice and notes of one pitch that
    /// overlap add up the same way whichever voices they land in (an idle voice would keep the phases where it stopped,
    /// and two notes of one pitch would add or cancel at random: up to 5.7 dB of level in Retro Poly). A voice that is
    /// still sounding (a steal) keeps its phases, so its waveform does not jump. (An idea for later: start phases
    /// spread in a controlled range, e.g. +-30 degrees, taking turns, as a round robin; docs/roadmap.md, "Next".)
    void triggerVoice(const int note, const int velocity, const int lastNote) noexcept
    {
        const bool silent = !isPlaying();
        if (m_glideTimePerOctave > 0.0f && lastNote != 0 && lastNote != note)
        {
            const auto semitoneDistance = static_cast<float>(lastNote - note);
            const float glideTime = m_glideTimePerOctave * std::abs(semitoneDistance) / 12.0f;
            m_envGlide.setSegment(0, glideTime, 0.0f, c_glideCurve);
            m_envGlide.noSustain();
            m_envGlide.triggerFrom(semitoneDistance);
        }
        else
        {
            m_envGlide.triggerFrom(0.0f);
        }

        m_connectors.filterCutoff.set(CtrlSource::Key, static_cast<float>(note - 60) / 12.0f);
        m_connectors.filterCutoff.set(CtrlSource::Velocity, static_cast<float>(velocity - 64) / 48.0f);
        m_connectors.filterCutoff.set(CtrlSource::Dimension, 0.0f);

        m_connectors.filterResonance.setForced(CtrlSource::Dimension, 0.0f);
        m_connectors.osc1Level.setForced(CtrlSource::Dimension, 0.0f);
        m_connectors.osc2Level.setForced(CtrlSource::Dimension, 0.0f);
        m_connectors.osc3Level.setForced(CtrlSource::Dimension, 0.0f);
        m_connectors.oscFrequency.setForced(CtrlSource::Pitch, 0.0f);
        m_connectors.panning.forceTarget(0.0f);
        m_connectors.sustainLevel.setForced(CtrlSource::Dimension, 0.0f);

        m_attackModify = 0.0f;
        m_decayModify = 0.0f;
        m_releaseModify = 0.0f;
        m_dimensionSeen.fill(false);
        applyCtrl(CtrlDimension::Note, note * 128);
        applyCtrl(CtrlDimension::Velocity, velocity * 128);

        if (silent)
        {
            m_lfo.setPhase(0.0f);
        }
        m_stepCount = c_granularity - 1;
        const auto normalizedVolume = static_cast<float>(velocity) / 127.0f;
        m_lfo.setSpeed(std::max(0.01f, m_lfoBaseFrequency *
                                           noteIntervalToRatio(static_cast<float>(note - 60) * m_lfoKeyFollowFactor)));
        m_connectors.oscFrequency.setForced(CtrlSource::BaseValue, static_cast<float>(note));

        const float detuneSemitones = m_randDetune != 0.0f ? m_random.gaussian() * m_randDetune : 0.0f;
        const float f = noteToFrequency(static_cast<float>(note)) * noteIntervalToRatio(detuneSemitones);

        for (size_t i = 0; i < m_oscillators.size(); ++i)
        {
            auto& osc = m_oscillators[i];
            if (i == 2 && m_osc3KeyFollowFactor != 1.0f)
            {
                osc.baseFrequency =
                    osc.pitchFactor * noteToFrequency(60.0f + static_cast<float>(note - 60) * m_osc3KeyFollowFactor);
            }
            else
            {
                osc.baseFrequency = osc.pitchFactor * f;
            }
            osc.bend = 1.0f;
            osc.detune = 1.0f;
            if (silent)
            {
                osc.oscillator.setPhase(0.0);
            }
            osc.oscillator.finishMorph();
            updateOscillatorFrequency(i);
        }

        m_connectors.distortion.forceTarget(0.125f);
        m_connectors.pitchBend.setForced(CtrlSource::Dimension, 0.0f);
        m_connectors.pitchBend.setForced(CtrlSource::BaseValue, 0.0f);
        m_connectors.pitchBend.setForced(CtrlSource::EnvelopePitch, 0.0f);
        m_connectors.pitchBend.setForced(CtrlSource::EnvelopeGlide, m_envGlide.isDone() ? 0.0f : m_envGlide.step());

        const float fA = noteIntervalToRatio(static_cast<float>(note - 60) * m_attackModify);
        const float fD = noteIntervalToRatio(static_cast<float>(note - 60) * m_decayModify);
        const float fR = noteIntervalToRatio(static_cast<float>(note - 60) * m_releaseModify);
        m_envAmplitude.setSegment(0, m_adsrAmplitude.attack * fA, 1.0f, 1.0f);
        m_envAmplitude.setSegment(1, m_adsrAmplitude.decay * fD, m_adsrAmplitude.sustainLevel, 1.0f);
        m_envAmplitude.setSegmentHoldPreviousValue(2, 1000.0f);
        m_envAmplitude.setSegment(3, m_adsrAmplitude.release * fR, 0.0f, 1.0f);
        m_envFilter.setSegment(0, m_adsrFilter.attack * fA, 1.0f, 1.0f);
        m_envFilter.setSegment(1, m_adsrFilter.decay * fD, m_adsrFilter.sustainLevel, 1.0f);
        m_envFilter.setSegment(3, m_adsrFilter.release * fR, 0.0f, 1.0f);

        m_filter.reset();
        m_envAmplitude.trigger();
        m_envFilter.trigger();
        m_envPitch.trigger();
        m_panLeft = m_panRight = std::sqrt(0.5f);

        m_gain = getVelocityResponse(normalizedVolume);
        m_connectors.filterCutoff.presetFast();
    }

    /// Note off: the release of the amplitude and filter envelopes.
    void stopVoice() noexcept
    {
        m_envAmplitude.release();
        m_envFilter.release();
    }

    /// Fast release (voice stealing, panic).
    void emergencyStop(const float releaseTimeMs) noexcept
    {
        m_envAmplitude.emergencyRelease(releaseTimeMs);
        m_envFilter.emergencyRelease(releaseTimeMs);
    }

    /// True from a trigger until the amplitude envelope has finished its release.
    [[nodiscard]] bool isPlaying() const noexcept
    {
        return !m_envAmplitude.isDone();
    }

    /// Renders numSamples of the voice into left and right (overwriting them). Any number of samples; the
    /// result does not depend on how a stream is cut into calls.
    void processBlock(float* left, float* right, const size_t numSamples) noexcept
    {
        size_t done = 0;
        while (done < numSamples)
        {
            const size_t windowPosition = (m_stepCount + 1) % c_granularity;
            const size_t chunk = std::min(numSamples - done, c_granularity - windowPosition);
            processChunk(left + done, right + done, chunk);
            done += chunk;
        }
    }

  private:
    [[nodiscard]] static BasicWave waveOf(const size_t index) noexcept
    {
        static constexpr std::array<BasicWave, 5> waveforms{BasicWave::Triangle, BasicWave::SharkFin, BasicWave::Saw,
                                                            BasicWave::Square, BasicWave::White};
        return waveforms[std::min(index, waveforms.size() - 1)];
    }

    struct AdsrTimes
    {
        float attack{10.0f};
        float decay{5.0f};
        float sustainLevel{0.5f};
        float release{100.0f};
    };

    struct Oscillator
    {
        Oscillator(const float sampleRate, const WaveTableBank& bank, const uint64_t seed)
            : oscillator(sampleRate, bank, seed)
        {
        }

        WaveTableOscillator oscillator;
        float pitchFactor{1.0f};
        float baseFrequency{440.0f};
        float bend{1.0f};
        float detune{1.0f};
    };

    void updateOscillatorFrequency(const size_t index) noexcept
    {
        auto& osc = m_oscillators[index];
        osc.oscillator.setFrequency(osc.baseFrequency * osc.bend * osc.detune);
    }

    /// The curved value of a slot (the value of its source through curve, type and depth), 0 while its source has not
    /// arrived since the note started.
    [[nodiscard]] float slotCurved(const MpeCtrl& slot) const noexcept
    {
        const auto index = static_cast<size_t>(slot.source);
        return m_dimensionSeen[index] ? mpeCtrlValue(slot, m_dimensionValue[index]) : 0.0f;
    }

    /// The same for a bipolar target in semitones (pitch bend).
    [[nodiscard]] float slotBipolar(const MpeCtrl& slot) const noexcept
    {
        const auto index = static_cast<size_t>(slot.source);
        return m_dimensionSeen[index] ? mpeCurveBipolar(slot.curve, m_dimensionValue[index]) * slot.depth * 12.0f
                                      : 0.0f;
    }

    [[nodiscard]] float sumCurved(const CtrlTarget target) const noexcept
    {
        float sum = 0.0f;
        for (const auto& slot : m_mpeCtrl)
        {
            sum += slot.target == target ? slotCurved(slot) : 0.0f;
        }
        return sum;
    }

    [[nodiscard]] float sumBipolar(const CtrlTarget target) const noexcept
    {
        float sum = 0.0f;
        for (const auto& slot : m_mpeCtrl)
        {
            sum += slot.target == target ? slotBipolar(slot) : 0.0f;
        }
        return sum;
    }

    /// A new value of a dimension: every target that a slot of this dimension drives is set to the sum of what all
    /// its slots give from the last values of their sources. (AbacDsp set the target to the slot of the arriving
    /// dimension alone, so two slots on one target, Y and Z on the cutoff, overwrote each other: the cutoff jumped
    /// between the two values at every message of either.)
    void applyCtrl(const CtrlDimension dimension, const int value) noexcept
    {
        const auto index = static_cast<size_t>(dimension);
        m_dimensionValue[index] = value;
        m_dimensionSeen[index] = true;
        uint32_t done = 0;
        for (const auto& mpe : m_mpeCtrl)
        {
            const uint32_t bit = 1u << static_cast<unsigned>(mpe.target);
            if (mpe.source != dimension || (done & bit) != 0)
            {
                continue;
            }
            done |= bit;
            applyTarget(mpe.target);
        }
    }

    void applyTarget(const CtrlTarget target) noexcept
    {
        switch (target)
        {
            case CtrlTarget::None:
                break;
            case CtrlTarget::PitchBend:
            {
                const float bend = sumBipolar(target);
                m_connectors.pitchBend.set(CtrlSource::BaseValue, m_quantizePitchbend > 0.0f
                                                                      ? pitchQuantize(bend, m_quantizePitchbend)
                                                                      : bend);
                break;
            }
            case CtrlTarget::PitchBendSecondary:
                m_connectors.pitchBend.set(CtrlSource::Dimension, sumBipolar(target));
                break;
            case CtrlTarget::FilterCutoff:
                m_connectors.filterCutoff.set(CtrlSource::Dimension, 12.0f * 2.0f * sumCurved(target));
                break;
            case CtrlTarget::FilterResonance:
                m_connectors.filterResonance.set(CtrlSource::Dimension, sumCurved(target));
                break;
            case CtrlTarget::Pan:
                m_connectors.panning.setTarget(sumCurved(target));
                break;
            case CtrlTarget::SustainVol:
                m_connectors.sustainLevel.set(CtrlSource::Dimension, sumCurved(target));
                break;
            case CtrlTarget::OscLevel1:
                m_connectors.osc1Level.set(CtrlSource::Dimension, sumCurved(target));
                break;
            case CtrlTarget::OscLevel2:
                m_connectors.osc2Level.set(CtrlSource::Dimension, sumCurved(target));
                break;
            case CtrlTarget::OscLevel3:
                m_connectors.osc3Level.set(CtrlSource::Dimension, sumCurved(target));
                break;
            case CtrlTarget::OscFreq2:
                m_oscillators[1].detune = noteIntervalToRatio(sumCurved(target));
                updateOscillatorFrequency(1);
                break;
            case CtrlTarget::OscFreq3:
                m_oscillators[2].detune = noteIntervalToRatio(sumCurved(target));
                updateOscillatorFrequency(2);
                break;
            case CtrlTarget::AttackTime:
                m_attackModify = sumCurved(target);
                break;
            case CtrlTarget::DecayTime:
                m_decayModify = sumCurved(target);
                break;
            case CtrlTarget::ReleaseTime:
                m_releaseModify = sumCurved(target);
                break;
            case CtrlTarget::LfoLevel:
                break;
            case CtrlTarget::Distortion:
                m_connectors.distortion.setTarget(0.125f + sumCurved(target) * 10.0f);
                break;
        }
    }

    void setEnvelopeAmplitudeShape() noexcept
    {
        m_envAmplitude.setSegment(0, m_adsrAmplitude.attack, 1.0f, 1.0f);
        m_envAmplitude.setSegment(1, m_adsrAmplitude.decay, m_adsrAmplitude.sustainLevel, 1.0f);
        m_envAmplitude.setSegmentHoldPreviousValue(2, 1000.0f);
        m_envAmplitude.markSustainAt(2);
        m_envAmplitude.setSegment(3, m_adsrAmplitude.release, 0.0f, 1.0f);
    }

    void setEnvelopeFilterShape() noexcept
    {
        m_envFilter.setSegment(0, m_adsrFilter.attack, 1.0f, 1.0f);
        m_envFilter.setSegment(1, m_adsrFilter.decay, m_adsrFilter.sustainLevel, 1.0f);
        m_envFilter.setSegmentHoldPreviousValue(2, 1000.0f);
        m_envFilter.markSustainAt(2);
        m_envFilter.setSegment(3, m_adsrFilter.release, 0.0f, 1.0f);
    }

    [[nodiscard]] float getVelocityResponse(const float x) const noexcept
    {
        const float gain = x * m_dynamicRangeFactor + 1.0f - m_dynamicRangeFactor;
        return gain * gain * gain;
    }

    static void blockScale(const float gain, float* data, const size_t numSamples) noexcept
    {
        for (size_t i = 0; i < numSamples; ++i)
        {
            data[i] *= gain;
        }
    }

    [[nodiscard]] float step() noexcept
    {
        if (++m_stepCount == c_granularity)
        {
            m_stepCount = 0;
            controlRateUpdate();
        }
        const float sum = m_tmp[m_tmpRead++];
        if (m_tmpRead == c_granularity)
        {
            m_tmpRead = 0;
            std::array<std::array<float, c_granularity>, c_numOscillators> tmpSum{};
            m_oscillators[0].oscillator.processBlock(tmpSum[0].data(), c_granularity);
            blockScale(m_connectors.osc1Level.get(), tmpSum[0].data(), c_granularity);
            m_oscillators[1].oscillator.processBlock(tmpSum[1].data(), c_granularity);
            blockScale(m_connectors.osc2Level.get(), tmpSum[1].data(), c_granularity);
            m_oscillators[2].oscillator.processBlock(tmpSum[2].data(), c_granularity);
            blockScale(m_connectors.osc3Level.get(), tmpSum[2].data(), c_granularity);
            for (size_t i = 0; i < c_granularity; ++i)
            {
                m_tmp[i] = tmpSum[0][i] + tmpSum[1][i] + tmpSum[2][i];
            }
        }
        return sum;
    }

    void controlRateUpdate() noexcept
    {
        if (m_connectors.distortion.isActive())
        {
            m_gainDistortion = m_connectors.distortion.getSmoothed();
            m_gainDistortionReciprocal = 1.0f / m_gainDistortion;
        }

        const float envFilterValue = m_envFilter.step();
        const float lfoValue = m_lfo.getValue();
        m_connectors.filterCutoff.set(CtrlSource::EnvelopeFilter, envFilterValue);
        applyCtrl(CtrlDimension::EnvelopeFilter, static_cast<int>(envFilterValue * 8191.0f));
        const float cf = m_connectors.filterCutoff.getSmoothed();
        m_connectors.filterCutoff.set(CtrlSource::Lfo, lfoValue * m_lfoFilterModulationDepth * 12.0f);
        m_connectors.pitchBend.set(CtrlSource::Lfo, lfoValue * m_lfoOscModulationDepth * 12.0f);
        m_filter.setCutoffFrequency(noteToFrequency(cf));
        m_filter.setResonance(std::clamp(m_connectors.filterResonance.getSmoothed(), 0.0f, 2.0f));

        if (!m_envPitch.isDone() || m_connectors.oscFrequency.hasChangedValue())
        {
            m_connectors.pitchBend.set(CtrlSource::EnvelopePitch, m_envPitch.step());
        }
        if (!m_envGlide.isDone())
        {
            m_connectors.pitchBend.set(CtrlSource::EnvelopeGlide, m_envGlide.step());
        }
        if (m_connectors.sustainLevel.hasChangedValue())
        {
            m_envAmplitude.setSegment(1, m_adsrAmplitude.decay, m_connectors.sustainLevel.get(), 1.0f);
            m_envAmplitude.setSegmentHoldPreviousValue(2, 1000.0f);
            m_envAmplitude.modifyTargetIfActive(1);
            m_envAmplitude.quickModifyIfSegmentActive(2, 1000);
        }
        if (m_connectors.panning.isActive())
        {
            const DryWet pan = constantPowerMix(m_connectors.panning.getSmoothed());
            m_panLeft = pan.dry;
            m_panRight = pan.wet;
        }

        applyCtrl(CtrlDimension::EnvelopeAmplitude, static_cast<int>(m_lastEnvelopeAmplitude * 8191.0f));

        if (m_connectors.pitchBend.isActive())
        {
            const float bend = m_connectors.pitchBend.get();
            for (auto& osc : m_oscillators)
            {
                osc.bend = bend;
            }
            updateOscillatorFrequency(0);
            updateOscillatorFrequency(1);
            updateOscillatorFrequency(2);
        }
    }

    /// Up to one window (the caller never crosses a window boundary inside a chunk).
    void processChunk(float* left, float* right, const size_t numSamples) noexcept
    {
        static constexpr std::array<float, 4> dither{1e-6f, 2.5e-6f, -1.4e-6f, -2.2e-6f};
        for (size_t s = 0; s < numSamples; ++s)
        {
            // a small fixed low-level dither keeps the resonant filter's feedback loop from idling on an
            // exact-zero input between notes
            left[s] = m_filter.process(step() + dither[m_ditherIndex]);
            m_ditherIndex = (m_ditherIndex + 1) % dither.size();
        }

        blockScale(m_gain, left, numSamples);
        for (size_t s = 0; s < numSamples; ++s)
        {
            m_lastEnvelopeAmplitude = m_envAmplitude.step();
            left[s] = std::clamp(left[s] * m_lastEnvelopeAmplitude, -4.0f, 4.0f);
        }

        if (m_filterIndex != m_lp4Index)
        {
            for (size_t s = 0; s < numSamples; ++s)
            {
                left[s] = m_lowpassStage.process(left[s]);
            }
        }

        if (m_waveTableIndex != 0)
        {
            blockScale(std::clamp(m_gainDistortion, 0.01f, 100.0f), left, numSamples);
            m_waveShaperTables.processBlock(m_waveTableIndex - 1, left, numSamples);
            blockScale(std::clamp(m_gainDistortionReciprocal * 2.0f, 0.01f, 100.0f), left, numSamples);
        }

        for (size_t i = 0; i < numSamples; ++i)
        {
            right[i] = left[i] * m_panRight;
            left[i] = left[i] * m_panLeft;
        }
    }

    float m_sampleRate;
    float m_quantizePitchbend{0.0f};
    float m_attackModify{0.0f};
    float m_decayModify{0.0f};
    float m_releaseModify{0.0f};

    std::array<MpeCtrl, c_numMpeSlots> m_mpeCtrl{};
    /// The last value of each dimension (CtrlDimension order) and whether one has arrived since the note started.
    std::array<int, 7> m_dimensionValue{};
    std::array<bool, 7> m_dimensionSeen{};
    float m_panLeft{0.7f};
    float m_panRight{0.7f};

    float m_dynamicRange{0.15f};
    float m_dynamicRangeFactor{0.4687f};
    float m_gain{1.0f};
    float m_lastEnvelopeAmplitude{0.0f};

    float m_pitchAttackTime{0.0f};
    float m_pitchFactorAD{1.2f};
    float m_glideTimePerOctave{0.0f};

    float m_lfoBaseFrequency{1.0f};
    float m_lfoOscModulationDepth{0.0f};
    float m_lfoFilterModulationDepth{0.0f};
    float m_lfoKeyFollowFactor{0.0f};
    float m_osc3KeyFollowFactor{1.0f};

    std::array<Oscillator, c_numOscillators> m_oscillators;
    SynthLfo m_lfo;
    AdsrTimes m_adsrAmplitude{};
    AdsrTimes m_adsrFilter{0.0f, 1.0f, 1.0f, 50.0f};
    Envelope<4> m_envAmplitude;
    Envelope<4> m_envFilter;
    Envelope<2> m_envPitch;
    Envelope<2> m_envGlide;

    PoleMixingBandpassFilter m_filter;
    size_t m_filterIndex{0};
    size_t m_lp4Index{0};
    OnePoleLowpass m_lowpassStage;

    MorphexsynthConnectors m_connectors;

    const WaveShaperTableStore& m_waveShaperTables;
    size_t m_waveTableIndex{0};
    float m_gainDistortion{2.0f};
    float m_gainDistortionReciprocal{0.5f};

    float m_randDetune{0.0f};
    Random m_random;

    std::array<float, c_granularity> m_tmp{};
    size_t m_tmpRead{0};
    size_t m_stepCount{c_granularity - 1};
    size_t m_ditherIndex{0};
};

}
