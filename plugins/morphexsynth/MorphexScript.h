#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "dsp/Filters/PoleMixingFilter.h"
#include "plugins/morphexsynth/MorphPatch.h"
#include "scripting/Binding.h"
#include "scripting/ScriptHost.h"

namespace jp
{

/// Which part of the sound a command from a script sets.
enum class MorphCommandKind : uint8_t
{
    Volume,
    Oscillator,
    WaveMorph,
    AmplitudeEnvelope,
    FilterEnvelope,
    PitchEnvelope,
    Lfo,
    Filter,
    Distortion,
    Slot,
    MpeZone,
    Phaser,
    Chorus,
    Reverb
};

/// The MPE zone of SetMpeZone, channels 1 to 16.
struct MpeZoneCommand
{
    int master{1};
    int lower{2};
    int upper{16};
};

/// One setting a script made, as plain values: the part, which oscillator or slot it is for, and the new value of that
/// part. Only the member that belongs to `kind` means anything.
struct MorphCommand
{
    MorphCommandKind kind{MorphCommandKind::Volume};
    size_t index{0};
    float volumeDb{0.0f};
    OscillatorPatch oscillator{};
    WaveMorphPatch waveMorph{};
    EnvelopePatch amplitude{};
    FilterEnvelopePatch filterEnvelope{};
    PitchEnvelopePatch pitchEnvelope{};
    LfoPatch lfo{};
    FilterPatch filter{};
    size_t distortion{0};
    SlotPatch slot{};
    MpeZoneCommand zone{};
    PhaserPatch phaser{};
    ChorusPatch chorus{};
    ReverbPatch reverb{};
};

/// A MIDI message for the script's hooks, as the pedal forwards it (channel 0 to 15).
struct MorphEvent
{
    enum class Kind : uint8_t
    {
        NoteOn,
        NoteOff,
        ControlChange,
        ProgramChange,
        PitchBend
    };

    Kind kind{Kind::NoteOn};
    uint8_t channel{0};
    int16_t first{0};  ///< note, controller number, program, or the bend centred on 0 (-8192 to 8191)
    int16_t second{0}; ///< velocity or controller value
};

/**
 * @brief Writes a command into a patch: the part the command sets takes the new value (the oscillator or slot named by
 * its index). Returns false for the MPE zone, which is no part of a patch.
 */
inline bool applyCommand(MorphPatch& patch, const MorphCommand& command) noexcept
{
    switch (command.kind)
    {
        case MorphCommandKind::Volume:
            patch.volumeDb = command.volumeDb;
            return true;
        case MorphCommandKind::Oscillator:
            patch.oscillators[command.index % patch.oscillators.size()] = command.oscillator;
            return true;
        case MorphCommandKind::WaveMorph:
            patch.morphs[command.index % patch.morphs.size()] = command.waveMorph;
            return true;
        case MorphCommandKind::AmplitudeEnvelope:
            patch.amplitude = command.amplitude;
            return true;
        case MorphCommandKind::FilterEnvelope:
            patch.filterEnvelope = command.filterEnvelope;
            return true;
        case MorphCommandKind::PitchEnvelope:
            patch.pitchEnvelope = command.pitchEnvelope;
            return true;
        case MorphCommandKind::Lfo:
            patch.lfo = command.lfo;
            return true;
        case MorphCommandKind::Filter:
            patch.filter = command.filter;
            return true;
        case MorphCommandKind::Distortion:
            patch.distortion = command.distortion;
            return true;
        case MorphCommandKind::Slot:
            patch.slots[command.index % patch.slots.size()] = command.slot;
            return true;
        case MorphCommandKind::Phaser:
            patch.phaser = command.phaser;
            return true;
        case MorphCommandKind::Chorus:
            patch.chorus = command.chorus;
            return true;
        case MorphCommandKind::Reverb:
            patch.reverb = command.reverb;
            return true;
        case MorphCommandKind::MpeZone:
            return false;
    }
    return false;
}

namespace morphex_script
{

using script::CommandSpec;
using script::EnumEntry;
using script::EnumSpec;
using script::HookSpec;
using script::ValueKind;
using script::ValueSpec;

inline constexpr double c_maxMs{20000.0};
inline constexpr int c_distortionPresets{31};

// -- commands ----------------------------------------------------------------------------------------------------

inline constexpr std::array<ValueSpec, 1> c_volumeArgs{
    {{"db", ValueKind::Number, -60.0, 12.0, 0.0, "level of the patch in dB (the Vol dial adds to it)"}}};

inline constexpr std::array<ValueSpec, 1> c_oscillatorArgs{
    {{"index", ValueKind::Integer, 0.0, 2.0, 0.0, "which oscillator, 0 to 2"}}};
inline constexpr std::array<ValueSpec, 5> c_oscillatorFields{{
    {"waveform", ValueKind::Integer, 0.0, 4.0, 0.0, "waveform", "OscWaveform"},
    {"detune", ValueKind::Number, -48.0, 48.0, 0.0, "tuning against the note in semitones"},
    {"level", ValueKind::Number, -1.0, 1.0, 0.0, "level of the oscillator"},
    {"pwm", ValueKind::Number, -1.0, 1.0, 0.0, "pulse-width style phase distortion"},
    {"pitchFactor", ValueKind::Number, 0.01, 16.0, 1.0, "frequency factor against the note (2 is an octave up)"},
}};

inline constexpr std::array<ValueSpec, 3> c_waveMorphFields{{
    {"waveform2", ValueKind::Integer, 0.0, 4.0, 3.0, "waveform of the second slot (default Square)", "OscWaveform"},
    {"waveform3", ValueKind::Integer, 0.0, 4.0, 2.0, "waveform of the third slot (default Saw)", "OscWaveform"},
    {"morph", ValueKind::Number, 0.0, 1.0, 0.0,
     "position over the three slots: 0 the waveform of SetOscillator, 0.5 the second, 1 the third, between them a "
     "crossfade; a change takes 10 ms"},
}};

inline constexpr std::array<ValueSpec, 4> c_envelopeFields{{
    {"attackMs", ValueKind::Number, 0.0, c_maxMs, 10.0, "attack time in ms"},
    {"decayMs", ValueKind::Number, 0.0, c_maxMs, 200.0, "decay time in ms"},
    {"sustainLevel", ValueKind::Number, 0.0, 1.0, 0.5, "sustain level"},
    {"releaseMs", ValueKind::Number, 0.0, c_maxMs, 100.0, "release time in ms"},
}};

inline constexpr std::array<ValueSpec, 5> c_filterEnvelopeFields{{
    {"attackMs", ValueKind::Number, 0.0, c_maxMs, 10.0, "attack time in ms"},
    {"decayMs", ValueKind::Number, 0.0, c_maxMs, 200.0, "decay time in ms"},
    {"sustainLevel", ValueKind::Number, 0.0, 1.0, 0.5, "sustain level"},
    {"releaseMs", ValueKind::Number, 0.0, c_maxMs, 100.0, "release time in ms"},
    {"contour", ValueKind::Number, -4.0, 4.0, 0.0,
     "how far the envelope moves the cutoff (positive opens the filter as the envelope rises)"},
}};

inline constexpr std::array<ValueSpec, 4> c_pitchEnvelopeFields{{
    {"attackMs", ValueKind::Number, 0.0, c_maxMs, 0.0, "attack time in ms"},
    {"decayMs", ValueKind::Number, 0.0, c_maxMs, 0.0, "decay time in ms"},
    {"depthSemitones", ValueKind::Number, -48.0, 48.0, 0.0, "how far the envelope bends the pitch in semitones"},
    {"glideMsPerOctave", ValueKind::Number, 0.0, 5000.0, 0.0, "glide time per octave from note to note in ms"},
}};

inline constexpr std::array<ValueSpec, 5> c_lfoFields{{
    {"waveform", ValueKind::Integer, 0.0, 7.0, 0.0, "waveform", "LfoWaveform"},
    {"speedHz", ValueKind::Number, 0.01, 50.0, 1.0, "rate in Hz"},
    {"filterDepth", ValueKind::Number, -4.0, 4.0, 0.0, "how far the LFO moves the cutoff"},
    {"oscDepth", ValueKind::Number, -4.0, 4.0, 0.0, "pitch modulation of all three oscillators in semitones"},
    {"keyFollow", ValueKind::Number, -4.0, 4.0, 0.0, "how much the note changes the LFO rate"},
}};

inline constexpr std::array<ValueSpec, 3> c_filterFields{{
    {"cutoff", ValueKind::Number, 0.0, 135.0, 72.0, "cutoff as a note number"},
    {"resonance", ValueKind::Number, 0.0, 2.0, 0.0, "resonance, 0 none, 1 near self-oscillation"},
    {"type", ValueKind::Text, 0.0, 0.0, 0.0, "filter type, default LP4 (see Filter types below)"},
}};

inline constexpr std::array<ValueSpec, 1> c_distortionArgs{
    {{"preset", ValueKind::Integer, 0.0, c_distortionPresets, 0.0, "0 off, 1 to 31 a distortion curve"}}};

inline constexpr std::array<ValueSpec, 1> c_slotArgs{
    {{"slot", ValueKind::Integer, 0.0, 9.0, 0.0, "which of the 10 routing slots, 0 to 9"}}};
inline constexpr std::array<ValueSpec, 5> c_slotFields{{
    {"source", ValueKind::Integer, 0.0, 6.0, 0.0, "what the slot reads", "CtrlSource"},
    {"curve", ValueKind::Integer, 0.0, 4.0, 2.0, "shape of the source", "CtrlCurve"},
    {"target", ValueKind::Integer, 0.0, 16.0, 0.0, "what the slot moves (None switches the slot off)", "CtrlTarget"},
    {"valueType", ValueKind::Integer, 0.0, 1.0, 0.0, "one-sided or signed source", "CtrlValueType"},
    {"depth", ValueKind::Number, -100.0, 100.0, 0.0, "how far the slot moves the target"},
}};

inline constexpr std::array<ValueSpec, 3> c_zoneArgs{{
    {"master", ValueKind::Integer, 1.0, 16.0, 1.0, "master channel, 1 to 16"},
    {"lower", ValueKind::Integer, 1.0, 16.0, 2.0, "first member channel"},
    {"upper", ValueKind::Integer, 1.0, 16.0, 16.0, "last member channel"},
}};

inline constexpr std::array<ValueSpec, 4> c_phaserFields{{
    {"rateHz", ValueKind::Number, 0.01, 10.0, 0.3, "sweep rate in Hz"},
    {"depth", ValueKind::Number, 0.0, 1.0, 0.5, "how far the sweep spans 200 Hz to 2 kHz"},
    {"feedback", ValueKind::Number, 0.0, 1.1, 0.0, "resonance around the allpass chain"},
    {"mix", ValueKind::Number, 0.0, 1.0, 0.5, "0 dry, 1 fully phased"},
}};

inline constexpr std::array<ValueSpec, 3> c_chorusFields{{
    {"rateHz", ValueKind::Number, 0.01, 8.0, 0.6, "sweep rate in Hz"},
    {"depth", ValueKind::Number, 0.0, 1.0, 0.5, "sweep depth"},
    {"mix", ValueKind::Number, 0.0, 1.0, 0.5, "0 dry, 1 fully wet"},
}};

inline constexpr std::array<ValueSpec, 4> c_reverbFields{{
    {"sizeMeters", ValueKind::Number, 1.0, 60.0, 12.0, "size of the room in metres"},
    {"decayMs", ValueKind::Number, 0.0, c_maxMs, 1500.0, "decay time in ms"},
    {"dryDb", ValueKind::Number, -100.0, 12.0, 0.0, "level of the dry signal in dB"},
    {"mixDb", ValueKind::Number, -100.0, 12.0, -100.0, "level of the reverb in dB (-100 is off)"},
}};

inline constexpr std::array<CommandSpec, 14> c_commands{{
    {"SetVolume", "Sets the level of the patch.", c_volumeArgs.data(), 1, nullptr, 0},
    {"SetOscillator", "Sets one of the three oscillators.", c_oscillatorArgs.data(), 1, c_oscillatorFields.data(), 5},
    {"SetAmpEnvelope", "Sets the amplitude envelope.", nullptr, 0, c_envelopeFields.data(), 4},
    {"SetFilterEnvelope", "Sets the filter envelope and how far it moves the cutoff.", nullptr, 0,
     c_filterEnvelopeFields.data(), 5},
    {"SetPitchEnvelope", "Sets the pitch envelope and the glide between notes.", nullptr, 0,
     c_pitchEnvelopeFields.data(), 4},
    {"SetLfo", "Sets the LFO of every voice.", nullptr, 0, c_lfoFields.data(), 5},
    {"SetFilter", "Sets the filter: cutoff, resonance and type.", nullptr, 0, c_filterFields.data(), 3},
    {"SetDistortion", "Selects the distortion curve.", c_distortionArgs.data(), 1, nullptr, 0},
    {"SetCtrlSlot",
     "Sets one slot of the routing matrix: it reads one source, shapes it by a curve, scales it by the depth and adds "
     "the result to a target.",
     c_slotArgs.data(), 1, c_slotFields.data(), 5},
    {"SetMpeZone", "Sets the MPE channels (1 to 16): master and the member channels lower to upper.", c_zoneArgs.data(),
     3, nullptr, 0},
    {"SetPhaser", "Sets the phaser of the effects bus.", nullptr, 0, c_phaserFields.data(), 4},
    {"SetChorus", "Sets the chorus of the effects bus.", nullptr, 0, c_chorusFields.data(), 3},
    {"SetReverb", "Sets the reverb of the effects bus.", nullptr, 0, c_reverbFields.data(), 4},
    {"SetWaveMorph",
     "Lets an oscillator morph between three waveforms: the one of SetOscillator, then waveform2 and waveform3. With "
     "morph = 0 (the default) the oscillator sounds exactly as without it; call it again from a hook to move the "
     "position while notes sound.",
     c_oscillatorArgs.data(), 1, c_waveMorphFields.data(), 3},
}};

// -- hooks and constants -----------------------------------------------------------------------------------------

inline constexpr std::array<const char*, 3> c_noteArguments{"channel", "note", "velocity"};
inline constexpr std::array<const char*, 3> c_controllerArguments{"channel", "controller", "value"};
inline constexpr std::array<const char*, 2> c_programArguments{"channel", "program"};
inline constexpr std::array<const char*, 2> c_bendArguments{"channel", "bend"};

inline constexpr std::array<HookSpec, 6> c_hooks{{
    {"OnStart", "Called once after the script is loaded; the usual place to set up a patch.", nullptr, 0},
    {"OnNoteOn", "A note began. channel counts from 0, velocity is 1 to 127.", c_noteArguments.data(), 3},
    {"OnNoteOff", "A note ended (a note on with velocity 0 counts as one).", c_noteArguments.data(), 3},
    {"OnCC", "A controller changed (value 0 to 127).", c_controllerArguments.data(), 3},
    {"OnProgramChange", "A program change arrived.", c_programArguments.data(), 2},
    {"OnPitchBend", "A pitch bend arrived, centred on 0 (-8192 to 8191).", c_bendArguments.data(), 2},
}};

#define JP_MORPHEX_ENTRY(Type, Name) {#Name, static_cast<int>(Type::Name)}

inline constexpr std::array<EnumEntry, 7> c_sourceEntries{{
    JP_MORPHEX_ENTRY(CtrlDimension, X),
    JP_MORPHEX_ENTRY(CtrlDimension, Y),
    JP_MORPHEX_ENTRY(CtrlDimension, Z),
    JP_MORPHEX_ENTRY(CtrlDimension, Velocity),
    JP_MORPHEX_ENTRY(CtrlDimension, Note),
    JP_MORPHEX_ENTRY(CtrlDimension, EnvelopeFilter),
    JP_MORPHEX_ENTRY(CtrlDimension, EnvelopeAmplitude),
}};
inline constexpr std::array<EnumEntry, 5> c_curveEntries{{
    JP_MORPHEX_ENTRY(CtrlCurve, CubeRoot),
    JP_MORPHEX_ENTRY(CtrlCurve, SquareRoot),
    JP_MORPHEX_ENTRY(CtrlCurve, Linear),
    JP_MORPHEX_ENTRY(CtrlCurve, Square),
    JP_MORPHEX_ENTRY(CtrlCurve, Cube),
}};
inline constexpr std::array<EnumEntry, 17> c_targetEntries{{
    JP_MORPHEX_ENTRY(CtrlTarget, None),
    JP_MORPHEX_ENTRY(CtrlTarget, PitchBend),
    JP_MORPHEX_ENTRY(CtrlTarget, PitchBendSecondary),
    JP_MORPHEX_ENTRY(CtrlTarget, FilterCutoff),
    JP_MORPHEX_ENTRY(CtrlTarget, FilterResonance),
    JP_MORPHEX_ENTRY(CtrlTarget, Pan),
    JP_MORPHEX_ENTRY(CtrlTarget, OscFreq2),
    JP_MORPHEX_ENTRY(CtrlTarget, OscFreq3),
    JP_MORPHEX_ENTRY(CtrlTarget, OscLevel1),
    JP_MORPHEX_ENTRY(CtrlTarget, OscLevel2),
    JP_MORPHEX_ENTRY(CtrlTarget, OscLevel3),
    JP_MORPHEX_ENTRY(CtrlTarget, LfoLevel),
    JP_MORPHEX_ENTRY(CtrlTarget, SustainVol),
    JP_MORPHEX_ENTRY(CtrlTarget, AttackTime),
    JP_MORPHEX_ENTRY(CtrlTarget, DecayTime),
    JP_MORPHEX_ENTRY(CtrlTarget, ReleaseTime),
    JP_MORPHEX_ENTRY(CtrlTarget, Distortion),
}};
inline constexpr std::array<EnumEntry, 2> c_valueTypeEntries{{
    JP_MORPHEX_ENTRY(CtrlValueType, Abs),
    JP_MORPHEX_ENTRY(CtrlValueType, BiPolar),
}};
inline constexpr std::array<EnumEntry, 5> c_waveformEntries{{
    JP_MORPHEX_ENTRY(OscWaveform, Triangle),
    JP_MORPHEX_ENTRY(OscWaveform, SharkFin),
    JP_MORPHEX_ENTRY(OscWaveform, Saw),
    JP_MORPHEX_ENTRY(OscWaveform, Square),
    JP_MORPHEX_ENTRY(OscWaveform, White),
}};
inline constexpr std::array<EnumEntry, 8> c_lfoEntries{{
    JP_MORPHEX_ENTRY(LfoType, Sine),
    JP_MORPHEX_ENTRY(LfoType, Triangle),
    JP_MORPHEX_ENTRY(LfoType, Saw),
    JP_MORPHEX_ENTRY(LfoType, Square),
    JP_MORPHEX_ENTRY(LfoType, Noise),
    JP_MORPHEX_ENTRY(LfoType, SampleHoldNoise),
    JP_MORPHEX_ENTRY(LfoType, SampleHoldFlipFlop),
    JP_MORPHEX_ENTRY(LfoType, BrownNoise),
}};

#undef JP_MORPHEX_ENTRY

inline constexpr std::array<EnumSpec, 6> c_enums{{
    {"CtrlSource", "What a routing slot reads (X: per-note pitch bend, Y: CC 74 and mod wheel, Z: pressure).",
     c_sourceEntries.data(), 7},
    {"CtrlCurve", "Shape applied to the source before it is scaled.", c_curveEntries.data(), 5},
    {"CtrlTarget", "What a routing slot moves.", c_targetEntries.data(), 17},
    {"CtrlValueType", "Abs: the source runs 0 to 1; BiPolar: it runs -1 to 1 around the middle.",
     c_valueTypeEntries.data(), 2},
    {"OscWaveform", "Oscillator waveforms.", c_waveformEntries.data(), 5},
    {"LfoWaveform", "LFO waveforms.", c_lfoEntries.data(), 8},
}};

// -- filter types -----------------------------------------------------------------------------------------------

/// A name AbacDsp's scripts use and the entry of c_poleMixingPresets that stands for it.
struct FilterAlias
{
    const char* alias;
    const char* preset;
};

inline constexpr std::array<FilterAlias, 15> c_filterAliases{{
    {"LP1", "6 lp(inv)"},
    {"LP2", "2 pole lp"},
    {"LP3", "3 pole lp(inv)"},
    {"LP4", "4 pole lp"},
    {"HP1", "1 pole hp"},
    {"HP2", "2 pole hp"},
    {"HP3", "3 pole hp"},
    {"HP4", "* 4 pole hp"},
    {"BP2", "2 pole bp"},
    {"BP4", "4 pole bp"},
    {"AP1", "* 1 pole ap (inv)"},
    {"AP2", "* 2 pole ap"},
    {"AP3", "* 3 pole ap (inv)"},
    {"AP4", "* 4 pole ap"},
    {"Notch", "2 pole notch"},
}};

/// The row of c_poleMixingPresets for a type name: one of the aliases, or the name of a row. Not found: the count.
[[nodiscard]] constexpr size_t filterPresetOf(const std::string_view name) noexcept
{
    for (const FilterAlias& alias : c_filterAliases)
    {
        if (name == alias.alias)
        {
            return findPoleMixingPreset(alias.preset);
        }
    }
    return findPoleMixingPreset(name);
}

/// The Markdown that ends the API description: the filter type names.
inline std::string filterTypeNotes()
{
    std::string text = "## Filter types\n\n`SetFilter` takes `type = \"...\"`: one of these short names (they stand "
                       "for the entry on the right)\n\n";
    for (const FilterAlias& alias : c_filterAliases)
    {
        text += std::string("- `") + alias.alias + "` = `" + alias.preset + "`\n";
    }
    text += "\nor the exact name of one of the filter's mixes:\n\n";
    for (size_t i = 0; i < c_poleMixingPresetCount; ++i)
    {
        text += std::string("- `") + c_poleMixingPresets[i].name + "`\n";
    }
    return text;
}

/// The index in c_hooks of the hook that handles a kind of MIDI message.
[[nodiscard]] constexpr size_t hookOf(const MorphEvent::Kind kind) noexcept
{
    return static_cast<size_t>(kind) + 1;
}

inline const script::BindingSet& bindings()
{
    static const script::BindingSet set{"Morphexsynth", c_commands.data(), c_commands.size(), c_hooks.data(),
                                        c_hooks.size(), c_enums.data(),    c_enums.size(),    &filterTypeNotes};
    return set;
}

}

/**
 * @brief The Morphexsynth side of scripting: turns a checked call of a script command into a MorphCommand and a MIDI
 * message into the call of a hook.
 *
 * The commands are those of AbacDsp's morphexsynth scripts with their ranges and defaults, plus SetVolume (the level
 * of a patch is a field of the patch file there, here a command). The filter type is looked up in the table of the
 * filter (morphex_script::filterPresetOf); an unknown name rejects the call. Channels in hooks count from 0, as in
 * AbacDsp; SetMpeZone counts from 1.
 */
class MorphBinder : public script::ScriptBinder<MorphEvent, MorphCommand>
{
  public:
    [[nodiscard]] const script::BindingSet& bindings() const noexcept override
    {
        return morphex_script::bindings();
    }

    bool makeCommand(const script::CommandCall& call, MorphCommand& command, char* error,
                     const size_t errorSize) noexcept override
    {
        switch (call.command)
        {
            case 0:
                command.kind = MorphCommandKind::Volume;
                command.volumeDb = call.real(0);
                return true;
            case 1:
                command.kind = MorphCommandKind::Oscillator;
                command.index = static_cast<size_t>(call.integer(0));
                command.oscillator = OscillatorPatch{static_cast<OscWaveform>(call.integer(1)), call.real(3),
                                                     call.real(2), call.real(4), call.real(5)};
                return true;
            case 2:
                command.kind = MorphCommandKind::AmplitudeEnvelope;
                command.amplitude = EnvelopePatch{call.real(0), call.real(1), call.real(2), call.real(3)};
                return true;
            case 3:
                command.kind = MorphCommandKind::FilterEnvelope;
                command.filterEnvelope =
                    FilterEnvelopePatch{call.real(0), call.real(1), call.real(2), call.real(3), call.real(4)};
                return true;
            case 4:
                command.kind = MorphCommandKind::PitchEnvelope;
                command.pitchEnvelope = PitchEnvelopePatch{call.real(0), call.real(1), call.real(2), call.real(3)};
                return true;
            case 5:
                command.kind = MorphCommandKind::Lfo;
                command.lfo = LfoPatch{static_cast<LfoType>(call.integer(0)), call.real(1), call.real(2), call.real(3),
                                       call.real(4)};
                return true;
            case 6:
            {
                const char* type = call.given[2] ? call.string(2) : "LP4";
                const size_t preset = morphex_script::filterPresetOf(type);
                if (preset == c_poleMixingPresetNotFound)
                {
                    std::snprintf(error, errorSize, "unknown filter type '%s'", type);
                    return false;
                }
                command.kind = MorphCommandKind::Filter;
                command.filter = FilterPatch{call.real(0), call.real(1), preset};
                return true;
            }
            case 7:
                command.kind = MorphCommandKind::Distortion;
                command.distortion = static_cast<size_t>(call.integer(0));
                return true;
            case 8:
                command.kind = MorphCommandKind::Slot;
                command.index = static_cast<size_t>(call.integer(0));
                command.slot =
                    SlotPatch{static_cast<CtrlDimension>(call.integer(1)), static_cast<CtrlCurve>(call.integer(2)),
                              static_cast<CtrlTarget>(call.integer(3)), static_cast<CtrlValueType>(call.integer(4)),
                              call.real(5)};
                return true;
            case 9:
                command.kind = MorphCommandKind::MpeZone;
                command.zone = MpeZoneCommand{call.integer(0), call.integer(1), call.integer(2)};
                return true;
            case 10:
                command.kind = MorphCommandKind::Phaser;
                command.phaser = PhaserPatch{call.real(0), call.real(1), call.real(2), call.real(3)};
                return true;
            case 11:
                command.kind = MorphCommandKind::Chorus;
                command.chorus = ChorusPatch{call.real(0), call.real(1), call.real(2)};
                return true;
            case 13:
                command.kind = MorphCommandKind::WaveMorph;
                command.index = static_cast<size_t>(call.integer(0));
                command.waveMorph = WaveMorphPatch{static_cast<OscWaveform>(call.integer(1)),
                                                   static_cast<OscWaveform>(call.integer(2)), call.real(3)};
                return true;
            default:
                command.kind = MorphCommandKind::Reverb;
                command.reverb = ReverbPatch{call.real(0), call.real(1), call.real(2), call.real(3)};
                return true;
        }
    }

    script::Status dispatch(script::Runtime& runtime, const MorphEvent& event) override
    {
        const auto channel = static_cast<double>(event.channel);
        const auto first = static_cast<double>(event.first);
        const auto second = static_cast<double>(event.second);
        switch (event.kind)
        {
            case MorphEvent::Kind::NoteOn:
            {
                const std::array<double, 3> arguments{channel, first, second};
                return runtime.call("OnNoteOn", arguments.data(), arguments.size());
            }
            case MorphEvent::Kind::NoteOff:
            {
                const std::array<double, 3> arguments{channel, first, second};
                return runtime.call("OnNoteOff", arguments.data(), arguments.size());
            }
            case MorphEvent::Kind::ControlChange:
            {
                const std::array<double, 3> arguments{channel, first, second};
                return runtime.call("OnCC", arguments.data(), arguments.size());
            }
            case MorphEvent::Kind::ProgramChange:
            {
                const std::array<double, 2> arguments{channel, first};
                return runtime.call("OnProgramChange", arguments.data(), arguments.size());
            }
            case MorphEvent::Kind::PitchBend:
            {
                const std::array<double, 2> arguments{channel, first};
                return runtime.call("OnPitchBend", arguments.data(), arguments.size());
            }
        }
        return script::Status::Ok;
    }
};

/// How a patch script ran on its own (evaluatePatchScript()).
struct PatchScriptResult
{
    script::Status status{script::Status::Ok};
    std::string error;            ///< the Lua error, empty when it worked
    std::vector<std::string> log; ///< the script's Log lines and the warnings about dropped calls
    bool zoneSet{false};          ///< the script called SetMpeZone
    MpeZoneCommand zone{};
};

/**
 * @brief Runs a patch script once, as the factory patches are: the top-level code and OnStart, nothing else. The
 * commands it calls are written into the patch, which starts as a default MorphPatch (everything silent and off); the
 * hooks are not run.
 *
 * A script that fails leaves the patch as it was before the call. Used for the patches that ship with the plugin, so
 * that choosing one is a copy of a value; a script that needs its hooks is a user script, run by a ScriptHost.
 */
inline PatchScriptResult evaluatePatchScript(const std::string& text, const char* name, const script::Limits& limits,
                                             const uint32_t seed, script::ModuleSource* modules, MorphPatch& patch)
{
    class Collector : public script::CommandSink
    {
      public:
        bool onCommand(const script::CommandCall& call, char* error, const size_t errorSize) noexcept override
        {
            MorphCommand command;
            if (!binder.makeCommand(call, command, error, errorSize))
            {
                return false;
            }
            if (!applyCommand(result, command))
            {
                zoneSet = true;
                zone = command.zone;
            }
            return true;
        }

        MorphBinder binder;
        MorphPatch result;
        bool zoneSet{false};
        MpeZoneCommand zone{};
    };

    Collector collector;
    PatchScriptResult outcome;
    script::Runtime runtime(morphex_script::bindings(), collector, modules, limits, seed);
    outcome.status = runtime.load(name, text);
    if (outcome.status == script::Status::Ok && runtime.hasFunction("OnStart"))
    {
        outcome.status = runtime.call("OnStart");
    }
    outcome.error = runtime.error();
    outcome.log = runtime.log();
    if (outcome.status == script::Status::Ok)
    {
        patch = collector.result;
        outcome.zoneSet = collector.zoneSet;
        outcome.zone = collector.zone;
    }
    return outcome;
}

}
