#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "scripting/Binding.h"
#include "scripting/ScriptHost.h"
#include "scripting/Timers.h"

namespace jp
{

/// What the pedal tells the hooks of a script besides the clock's own events: a step of the clock, a note that was
/// played into the plugin, the end of that note, a move of X or Y.
struct ShipAhoyEvent
{
    enum class Kind : uint8_t
    {
        Step,     ///< OnStep(step)
        Pitch,    ///< OnNewPitch(note, hz, pitch, periodicity, level)
        PitchEnd, ///< OnPitchEnd(note)
        Control   ///< OnControl(x, y)
    };

    Kind kind{Kind::Step};
    uint32_t step{0};
    float note{0.0f};
    float hz{0.0f};
    float pitch{0.0f};
    float periodicity{0.0f};
    float level{0.0f};
    float x{0.0f};
    float y{0.0f};
};

/// What a command of a script asks the pedal to do.
struct ShipAhoyCommand
{
    enum class Kind : uint8_t
    {
        Cutoff,     ///< glide the filter frequency to `note`
        SpeedRatio, ///< multiply the clock tempo by `ratio`
        Resonance,  ///< the resonance, `value` in percent
        FilterType, ///< the filter type, `value` as menu index
        Drift       ///< the drift: `value` depth in semitones, `rate` in Hz
    };

    Kind kind{Kind::Cutoff};
    float note{69.0f};
    float ticks{0.0f};
    float ms{0.0f};
    float glide{0.05f};
    float delayTicks{0.0f};
    float delayMs{0.0f};
    float ratio{1.0f};
    float value{0.0f};
    float rate{1.0f};
    /// Issued by the hook of an external event (a played note, a move of X or Y): applied at the first block boundary
    /// after it arrives, without the lead of the clock.
    bool external{false};
};

namespace ship_ahoy
{

using script::CommandSpec;
using script::EnumEntry;
using script::EnumSpec;
using script::GetterSpec;
using script::HookSpec;
using script::ValueKind;
using script::ValueSpec;

/// Ticks of the clock per quarter note: the clock the scripts see (OnTick, delayTicks, ticks, SetTimer ticks). 96 makes
/// every note value from 1/64 up a whole number of ticks.
inline constexpr uint32_t c_ticksPerQuarter{96};

/// The step lengths of the Step port, in ticks: 8/1, 4/1, 2/1, 1/1, 1/2, 1/4, 1/8, 1/16, 1/32, 1/64.
inline constexpr size_t c_stepCount{10};
inline constexpr std::array<uint32_t, c_stepCount> c_stepTicks{3072, 1536, 768, 384, 192, 96, 48, 24, 12, 6};
inline constexpr std::array<const char*, c_stepCount> c_stepLabels{"8/1", "4/1", "2/1",  "1/1",  "1/2",
                                                                   "1/4", "1/8", "1/16", "1/32", "1/64"};
inline constexpr size_t c_defaultStep{7};

/// The range of the speed ratio a script can set.
inline constexpr double c_minSpeedRatio{0.1};
inline constexpr double c_maxSpeedRatio{10.0};

inline constexpr std::array<ValueSpec, 1> c_cutoffArgs{{
    {"note", ValueKind::Number, 0.0, 127.0, 69.0,
     "MIDI note as a number, 69 = A4 = 440 Hz, fractions are cents; the frequency of the resonance peak, limited to "
     "10 Hz up to a sixth of the sample rate"},
}};

inline constexpr ValueSpec c_delayMs{"delayMs", ValueKind::Number,
                                     0.0,       60000.0,
                                     0.0,       "start this many milliseconds after the event whose hook calls it"};
inline constexpr ValueSpec c_delayTicks{"delayTicks", ValueKind::Number,
                                        0.0,          1000000.0,
                                        0.0,          "start this many clock ticks after the event (added to delayMs)"};

inline constexpr std::array<ValueSpec, 5> c_cutoffFields{{
    {"ticks", ValueKind::Number, 0.0, 1000000.0, 0.0,
     "length of the step in clock ticks (added to ms); with ms also 0 the step is the present step length of the Step "
     "setting"},
    {"ms", ValueKind::Number, 0.0, 600000.0, 0.0, "length of the step in milliseconds"},
    {"glide", ValueKind::Number, 0.0, 1.0, 0.05,
     "the glide lasts this fraction of the step: 0 jumps at once, 0.5 glides during the first half and then holds, "
     "1 arrives as the step ends"},
    c_delayTicks,
    c_delayMs,
}};

inline constexpr std::array<ValueSpec, 1> c_ratioArgs{{
    {"ratio", ValueKind::Number, c_minSpeedRatio, c_maxSpeedRatio, 1.0,
     "factor on the speed of the clock: 2 doubles the tempo, 0.5 halves it; 1 again at every new script"},
}};
inline constexpr std::array<ValueSpec, 2> c_delayFields{{c_delayTicks, c_delayMs}};

inline constexpr std::array<ValueSpec, 1> c_resonanceArgs{{
    {"percent", ValueKind::Number, 0.0, 100.0, 85.0,
     "resonance as a fraction of self-oscillation at the present frequency: 100 is the edge, the top 10 ring hard and "
     "raise the level"},
}};

inline constexpr size_t c_filterTypeCount{15};

inline constexpr std::array<ValueSpec, 1> c_filterTypeArgs{{
    {"type", ValueKind::Integer, 0.0, static_cast<double>(c_filterTypeCount - 1), 1.0,
     "the pole-mixing preset of the filter", "FilterType"},
}};

inline constexpr std::array<ValueSpec, 1> c_driftArgs{{
    {"depth", ValueKind::Number, 0.0, 12.0, 0.0,
     "standard deviation of the drift in semitones, 0 switches it off; peaks reach about three times as much"},
}};
inline constexpr std::array<ValueSpec, 1> c_driftFields{{
    {"rate", ValueKind::Number, 0.05, 20.0, 1.0, "corner frequency of the drift in Hz"},
}};

/// The index of SetTimer in c_commands: the host answers it and the one after it itself.
inline constexpr size_t c_timerCommandBase{5};

inline constexpr std::array<CommandSpec, 7> c_commands{{
    {"SetCutoff",
     "Glides the frequency of the filter to a note. The glide starts at the sample of the event whose hook calls it "
     "(plus the delay, and the lead of the clock for clock events) from where the frequency is; a command during a "
     "glide continues from "
     "the present value. The plugin has no other way to move the frequency: no script, no movement.",
     c_cutoffArgs.data(), 1, c_cutoffFields.data(), 5},
    {"SpeedRatio",
     "Multiplies the speed of the clock (tempo, steps, ticks, timers set from now on), 0.1 to 10. It starts at the "
     "sample of the event plus the delay and, for clock events, the lead; a new script starts at 1.",
     c_ratioArgs.data(), 1, c_delayFields.data(), 2},
    {"SetResonance",
     "Sets the resonance, smoothed over 20 ms. It stays until the next call; a script that does not call it gets 85. "
     "Applied at the sample of the event plus the lead (at once for an external event).",
     c_resonanceArgs.data(), 1, nullptr, 0},
    {"SetFilterType",
     "Sets the type of the filter (low, band and high pass, notch, phasers; the FilterType constants). It stays until "
     "the next call; a script that does not call it gets Lp12.",
     c_filterTypeArgs.data(), 1, nullptr, 0},
    {"SetDrift",
     "Sets the drift: a slow random wobble added to the frequency, independent of the notes. It stays until the next "
     "call; a script that does not call it gets none.",
     c_driftArgs.data(), 1, c_driftFields.data(), 1},
    script::c_timerCommands[0],
    script::c_timerCommands[1],
}};

inline constexpr std::array<const char*, 1> c_stepArguments{"step"};
inline constexpr std::array<const char*, 5> c_pitchArguments{"note", "hz", "pitch", "periodicity", "level"};
inline constexpr std::array<const char*, 1> c_pitchEndArguments{"note"};
inline constexpr std::array<const char*, 2> c_controlArguments{"x", "y"};

/// The order is the index that hookOf() gives: OnStart, OnStep, OnNewPitch, OnPitchEnd, OnControl, then the hooks of
/// the clock and the timers (scripting/Timers.h).
inline constexpr std::array<HookSpec, 9> c_hooks{{
    {"OnStart", "Runs once after the script is loaded.", nullptr, 0},
    {"OnStep",
     "A step of the clock, as long as the Step setting says (1/16 by default), at the tempo and speed ratio of the "
     "moment; step counts from 0 at the start of the clock.",
     c_stepArguments.data(), 1},
    {"OnNewPitch",
     "A note was played into the plugin (one voice, 80 Hz to 1 kHz): about 80 ms after its start, once its pitch has "
     "been steady for three readings, and again when the same note is struck anew. note is the nearest MIDI note "
     "(69 = A4), hz its frequency, pitch the MIDI note with fractions (cents), periodicity how clearly periodic the "
     "sound was, 0 to 1 (1 minus the YIN difference at the period; not a probability), level the RMS of the last "
     "25 ms, linear. The commands it calls are applied at the first block boundary after the note was reported, "
     "without the lead of the clock. Called only while the script defines it.",
     c_pitchArguments.data(), 5},
    {"OnPitchEnd",
     "The note that was playing has ended (silence or noise for three readings, 60 ms); note is the note that ended. "
     "Its commands are applied at the first block boundary, like those of OnNewPitch.",
     c_pitchEndArguments.data(), 1},
    {"OnControl",
     "X or Y was moved (0 to 100 each, both given), at most once per audio block and only on a change. Its commands "
     "are applied at the first block boundary, like those of OnNewPitch. GetX() and GetY() give the same values "
     "anywhere else. Called only while the script defines it.",
     c_controlArguments.data(), 2},
    script::c_coreHooks[0],
    script::c_coreHooks[1],
    script::c_coreHooks[2],
    script::c_coreHooks[3],
}};

inline constexpr std::array<EnumEntry, 9> c_tickEntries{{{"Whole", 384},
                                                         {"Half", 192},
                                                         {"Quarter", 96},
                                                         {"Eighth", 48},
                                                         {"EighthTriplet", 32},
                                                         {"Sixteenth", 24},
                                                         {"SixteenthTriplet", 16},
                                                         {"ThirtySecond", 12},
                                                         {"SixtyFourth", 6}}};

inline constexpr std::array<EnumEntry, c_filterTypeCount> c_filterTypeEntries{{{"Lp6", 0},
                                                                               {"Lp12", 1},
                                                                               {"Lp18", 2},
                                                                               {"Lp24", 3},
                                                                               {"Bp6", 4},
                                                                               {"Bp12", 5},
                                                                               {"Hp6", 6},
                                                                               {"Hp12", 7},
                                                                               {"Hp18", 8},
                                                                               {"Hp24", 9},
                                                                               {"Notch", 10},
                                                                               {"NotchOctaves", 11},
                                                                               {"Phaser2", 12},
                                                                               {"Phaser3", 13},
                                                                               {"Phaser4", 14}}};

inline constexpr std::array<EnumSpec, 2> c_enums{
    {{"Ticks", "Lengths of notes in ticks of the clock (96 per quarter note).", c_tickEntries.data(), 9},
     {"FilterType", "Types of the filter, for SetFilterType (the pole-mixing presets of the plugin).",
      c_filterTypeEntries.data(), 15}}};

/// The getters: X and Y, the two knobs without a meaning of their own.
inline constexpr std::array<GetterSpec, 2> c_getters{{
    {"GetX", "The X knob of the plugin, whose meaning the script decides.", 0.0, 100.0},
    {"GetY", "The Y knob of the plugin, whose meaning the script decides.", 0.0, 100.0},
}};

inline const script::BindingSet& bindings()
{
    static const script::BindingSet set{"Ship Ahoy",      c_commands.data(), c_commands.size(), c_hooks.data(),
                                        c_hooks.size(),   c_enums.data(),    c_enums.size(),    nullptr,
                                        c_getters.data(), c_getters.size()};
    return set;
}

/// The indices in c_hooks of OnStep, OnNewPitch, OnPitchEnd and OnControl.
inline constexpr size_t c_stepHook{1};
inline constexpr size_t c_pitchHook{2};
inline constexpr size_t c_pitchEndHook{3};
inline constexpr size_t c_controlHook{4};

/// The indices of the getters.
inline constexpr size_t c_getX{0};
inline constexpr size_t c_getY{1};

}

/**
 * @brief The Ship Ahoy side of scripting: turns a checked call of a command into a ShipAhoyCommand and an event into
 * the call of OnStep, OnNewPitch, OnPitchEnd or OnControl.
 *
 * The hooks of external events (a played note, its end, a move of X or Y) mark their commands as external: the pedal
 * applies those at the first block boundary after they arrive, the commands of the clock's hooks one lead later.
 * dispatch() and makeCommand() are both in the worker thread, so a plain flag says which hook is running.
 */
class ShipAhoyBinder : public script::ScriptBinder<ShipAhoyEvent, ShipAhoyCommand>
{
  public:
    [[nodiscard]] const script::BindingSet& bindings() const noexcept override
    {
        return ship_ahoy::bindings();
    }

    bool makeCommand(const script::CommandCall& call, ShipAhoyCommand& command, char* /*error*/,
                     const size_t /*errorSize*/) noexcept override
    {
        command = ShipAhoyCommand{};
        command.external = m_external;
        switch (call.command)
        {
            case 0:
                command.kind = ShipAhoyCommand::Kind::Cutoff;
                command.note = call.real(0);
                command.ticks = call.real(1);
                command.ms = call.real(2);
                command.glide = call.real(3);
                command.delayTicks = call.real(4);
                command.delayMs = call.real(5);
                return true;
            case 1:
                command.kind = ShipAhoyCommand::Kind::SpeedRatio;
                command.ratio = call.real(0);
                command.delayTicks = call.real(1);
                command.delayMs = call.real(2);
                return true;
            case 2:
                command.kind = ShipAhoyCommand::Kind::Resonance;
                command.value = call.real(0);
                return true;
            case 3:
                command.kind = ShipAhoyCommand::Kind::FilterType;
                command.value = static_cast<float>(call.integer(0));
                return true;
            default:
                command.kind = ShipAhoyCommand::Kind::Drift;
                command.value = call.real(0);
                command.rate = call.real(1);
                return true;
        }
    }

    script::Status dispatch(script::Runtime& runtime, const ShipAhoyEvent& event) override
    {
        script::Status status = script::Status::Ok;
        if (event.kind == ShipAhoyEvent::Kind::Pitch)
        {
            const std::array<double, 5> arguments{
                static_cast<double>(event.note), static_cast<double>(event.hz), static_cast<double>(event.pitch),
                static_cast<double>(event.periodicity), static_cast<double>(event.level)};
            m_external = true;
            status = runtime.call("OnNewPitch", arguments.data(), arguments.size());
        }
        else if (event.kind == ShipAhoyEvent::Kind::PitchEnd)
        {
            const std::array<double, 1> arguments{static_cast<double>(event.note)};
            m_external = true;
            status = runtime.call("OnPitchEnd", arguments.data(), arguments.size());
        }
        else if (event.kind == ShipAhoyEvent::Kind::Control)
        {
            const std::array<double, 2> arguments{static_cast<double>(event.x), static_cast<double>(event.y)};
            m_external = true;
            status = runtime.call("OnControl", arguments.data(), arguments.size());
        }
        else
        {
            const std::array<double, 1> arguments{static_cast<double>(event.step)};
            status = runtime.call("OnStep", arguments.data(), arguments.size());
        }
        m_external = false;
        return status;
    }

  private:
    bool m_external{false};
};

}
