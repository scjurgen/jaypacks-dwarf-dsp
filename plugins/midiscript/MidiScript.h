#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "scripting/Binding.h"
#include "scripting/ScriptHost.h"
#include "scripting/Timers.h"

namespace jp
{

/// A MIDI message for the script's hooks, as the pedal forwards it: the bytes as they came, and which hook they are
/// for.
struct MidiScriptEvent
{
    enum class Kind : uint8_t
    {
        NoteOn,
        NoteOff, ///< also a note on with velocity 0
        ControlChange,
        PitchBend,
        ProgramChange,
        Other ///< channel pressure, poly pressure and system common messages
    };

    Kind kind{Kind::NoteOn};
    uint8_t size{3};
    std::array<uint8_t, 3> bytes{};
};

/// What a command of a script asks the pedal to do.
struct MidiScriptCommand
{
    enum class Kind : uint8_t
    {
        Message, ///< send `bytes` at the time
        Note,    ///< a note on at the time and its note off one length later
        Panic
    };

    Kind kind{Kind::Message};
    uint8_t size{3};
    std::array<uint8_t, 3> bytes{};
    float delayMs{0.0f};
    float delayTicks{0.0f};
    float lengthMs{0.0f};
    float lengthTicks{0.0f};
};

namespace midi_script
{

using script::CommandSpec;
using script::EnumEntry;
using script::EnumSpec;
using script::HookSpec;
using script::ValueKind;
using script::ValueSpec;

/// Ticks of the clock per quarter note: the clock the scripts see (OnTick, delayTicks, lengthTicks, SetTimer ticks).
inline constexpr uint32_t c_ticksPerQuarter{24};

inline constexpr std::array<ValueSpec, 3> c_noteArgs{{
    {"channel", ValueKind::Integer, 1.0, 16.0, 1.0, "MIDI channel 1 to 16"},
    {"note", ValueKind::Integer, 0.0, 127.0, 60.0, "MIDI note number (60 = middle C)"},
    {"velocity", ValueKind::Integer, 1.0, 127.0, 100.0, "velocity 1 to 127"},
}};
inline constexpr std::array<ValueSpec, 2> c_noteOffArgs{{
    {"channel", ValueKind::Integer, 1.0, 16.0, 1.0, "MIDI channel 1 to 16"},
    {"note", ValueKind::Integer, 0.0, 127.0, 60.0, "MIDI note number"},
}};
inline constexpr std::array<ValueSpec, 3> c_ccArgs{{
    {"channel", ValueKind::Integer, 1.0, 16.0, 1.0, "MIDI channel 1 to 16"},
    {"controller", ValueKind::Integer, 0.0, 127.0, 0.0, "controller number"},
    {"value", ValueKind::Integer, 0.0, 127.0, 0.0, "controller value"},
}};
inline constexpr std::array<ValueSpec, 2> c_bendArgs{{
    {"channel", ValueKind::Integer, 1.0, 16.0, 1.0, "MIDI channel 1 to 16"},
    {"value", ValueKind::Integer, -8192.0, 8191.0, 0.0, "pitch bend, 0 is the centre"},
}};
inline constexpr std::array<ValueSpec, 2> c_programArgs{{
    {"channel", ValueKind::Integer, 1.0, 16.0, 1.0, "MIDI channel 1 to 16"},
    {"program", ValueKind::Integer, 0.0, 127.0, 0.0, "program number 0 to 127"},
}};

inline constexpr ValueSpec c_delayMs{
    "delayMs", ValueKind::Number, 0.0, 60000.0, 0.0, "send this many milliseconds after the event whose hook calls it"};
inline constexpr ValueSpec c_delayTicks{"delayTicks", ValueKind::Number,
                                        0.0,          96000.0,
                                        0.0,          "send this many clock ticks after the event (added to delayMs)"};
inline constexpr ValueSpec c_lengthMs{
    "lengthMs", ValueKind::Number, 0.0, 60000.0, 0.0, "how long the note sounds, in milliseconds"};
inline constexpr ValueSpec c_lengthTicks{"lengthTicks",
                                         ValueKind::Number,
                                         0.0,
                                         96000.0,
                                         0.0,
                                         "how long the note sounds, in ticks (added to lengthMs); with both 0 "
                                         "the note sounds for one tick"};

inline constexpr std::array<ValueSpec, 2> c_delayFields{{c_delayMs, c_delayTicks}};
inline constexpr std::array<ValueSpec, 4> c_noteFields{{c_delayMs, c_delayTicks, c_lengthMs, c_lengthTicks}};

/// The index of SetTimer in c_commands: the host answers it and the one after it itself.
inline constexpr size_t c_timerCommandBase{8};

inline constexpr std::array<CommandSpec, 10> c_commands{{
    {"Note",
     "Sends a note on and, one length later, its note off. The note off is certain: it goes out even if the script "
     "is replaced or fails in between.",
     c_noteArgs.data(), 3, c_noteFields.data(), 4},
    {"NoteOn", "Sends a note on.", c_noteArgs.data(), 3, c_delayFields.data(), 2},
    {"NoteOff", "Sends a note off.", c_noteOffArgs.data(), 2, c_delayFields.data(), 2},
    {"CC", "Sends a control change.", c_ccArgs.data(), 3, c_delayFields.data(), 2},
    {"PitchBend", "Sends a pitch bend.", c_bendArgs.data(), 2, c_delayFields.data(), 2},
    {"ProgramChange", "Sends a program change.", c_programArgs.data(), 2, c_delayFields.data(), 2},
    {"Pass",
     "Sends the message of the event being handled on, unchanged. Only inside OnNoteOn, OnNoteOff, OnCC, "
     "OnPitchBend, OnProgramChange and OnOther.",
     nullptr, 0, c_delayFields.data(), 2},
    {"Panic",
     "Ends every sounding note now, drops everything that was waiting and sends All Notes Off on all channels.",
     nullptr, 0, nullptr, 0},
    script::c_timerCommands[0],
    script::c_timerCommands[1],
}};

inline constexpr std::array<const char*, 3> c_noteHookArguments{"channel", "note", "velocity"};
inline constexpr std::array<const char*, 3> c_ccHookArguments{"channel", "controller", "value"};
inline constexpr std::array<const char*, 2> c_bendHookArguments{"channel", "value"};
inline constexpr std::array<const char*, 2> c_programHookArguments{"channel", "program"};
inline constexpr std::array<const char*, 3> c_otherHookArguments{"status", "data1", "data2"};

/// The order is the index that hookOf() gives: OnStart, then one hook per MidiScriptEvent::Kind, then the hooks of the
/// clock and the timers (scripting/Timers.h).
inline constexpr std::array<HookSpec, 11> c_hooks{{
    {"OnStart", "Runs once after the script is loaded.", nullptr, 0},
    {"OnNoteOn", "A note on arrived. Channels count from 1.", c_noteHookArguments.data(), 3},
    {"OnNoteOff", "A note off arrived (or a note on with velocity 0, then velocity is 0).", c_noteHookArguments.data(),
     3},
    {"OnCC", "A control change arrived.", c_ccHookArguments.data(), 3},
    {"OnPitchBend", "A pitch bend arrived; value is -8192 to 8191.", c_bendHookArguments.data(), 2},
    {"OnProgramChange", "A program change arrived.", c_programHookArguments.data(), 2},
    {"OnOther",
     "Any other message of 1 to 3 bytes except the real-time messages (channel pressure, poly pressure, song "
     "position ...): the status byte and the two data bytes (0 when absent).",
     c_otherHookArguments.data(), 3},
    script::c_coreHooks[0],
    script::c_coreHooks[1],
    script::c_coreHooks[2],
    script::c_coreHooks[3],
}};

inline constexpr std::array<EnumEntry, 8> c_tickEntries{{{"Whole", 96},
                                                         {"Half", 48},
                                                         {"Quarter", 24},
                                                         {"Eighth", 12},
                                                         {"EighthTriplet", 8},
                                                         {"Sixteenth", 6},
                                                         {"SixteenthTriplet", 4},
                                                         {"ThirtySecond", 3}}};

inline constexpr std::array<EnumSpec, 1> c_enums{
    {{"Ticks", "Lengths of notes in ticks of the clock (24 per quarter note).", c_tickEntries.data(), 8}}};

/// The index in c_hooks of the hook that handles a kind of MIDI message.
[[nodiscard]] constexpr size_t hookOf(const MidiScriptEvent::Kind kind) noexcept
{
    return static_cast<size_t>(kind) + 1;
}

inline const script::BindingSet& bindings()
{
    static const script::BindingSet set{"MIDI Script",  c_commands.data(), c_commands.size(), c_hooks.data(),
                                        c_hooks.size(), c_enums.data(),    c_enums.size()};
    return set;
}

/// The event for a MIDI message of 1 to 3 bytes that has a hook; false for real-time messages and anything without
/// a status byte.
[[nodiscard]] inline bool eventOf(const uint8_t* message, const uint32_t size, MidiScriptEvent& event) noexcept
{
    if (size == 0 || size > 3 || message[0] < 0x80 || message[0] >= 0xF8)
    {
        return false;
    }
    event = MidiScriptEvent{};
    event.size = static_cast<uint8_t>(size);
    for (uint32_t i = 0; i < size; ++i)
    {
        event.bytes[i] = message[i];
    }
    switch (message[0] & 0xF0)
    {
        case 0x90:
            event.kind =
                (size == 3 && message[2] == 0) ? MidiScriptEvent::Kind::NoteOff : MidiScriptEvent::Kind::NoteOn;
            break;
        case 0x80:
            event.kind = MidiScriptEvent::Kind::NoteOff;
            break;
        case 0xB0:
            event.kind = MidiScriptEvent::Kind::ControlChange;
            break;
        case 0xE0:
            event.kind = MidiScriptEvent::Kind::PitchBend;
            break;
        case 0xC0:
            event.kind = MidiScriptEvent::Kind::ProgramChange;
            break;
        default:
            event.kind = MidiScriptEvent::Kind::Other;
            break;
    }
    if ((event.kind == MidiScriptEvent::Kind::NoteOn || event.kind == MidiScriptEvent::Kind::NoteOff ||
         event.kind == MidiScriptEvent::Kind::ControlChange || event.kind == MidiScriptEvent::Kind::PitchBend) &&
        size != 3)
    {
        return false; // a truncated message
    }
    return true;
}

}

/**
 * @brief The MIDI Script side of scripting: turns a checked call of a script command into a MidiScriptCommand and an
 * incoming MIDI message into the call of a hook.
 *
 * Channels count from 1 in hooks and commands. Pass() sends the message of the event whose hook is running, so it
 * needs the binder to remember it (worker thread only, like the hooks).
 */
class MidiScriptBinder : public script::ScriptBinder<MidiScriptEvent, MidiScriptCommand>
{
  public:
    [[nodiscard]] const script::BindingSet& bindings() const noexcept override
    {
        return midi_script::bindings();
    }

    bool makeCommand(const script::CommandCall& call, MidiScriptCommand& command, char* error,
                     const size_t errorSize) noexcept override
    {
        command = MidiScriptCommand{};
        switch (call.command)
        {
            case 0:
                setMessage(command, 0x90, call.integer(0), call.integer(1), call.integer(2));
                command.kind = MidiScriptCommand::Kind::Note;
                command.delayMs = call.real(3);
                command.delayTicks = call.real(4);
                command.lengthMs = call.real(5);
                command.lengthTicks = call.real(6);
                return true;
            case 1:
                setMessage(command, 0x90, call.integer(0), call.integer(1), call.integer(2));
                setDelay(command, call, 3);
                return true;
            case 2:
                setMessage(command, 0x80, call.integer(0), call.integer(1), 64);
                setDelay(command, call, 2);
                return true;
            case 3:
                setMessage(command, 0xB0, call.integer(0), call.integer(1), call.integer(2));
                setDelay(command, call, 3);
                return true;
            case 4:
            {
                const int bend = call.integer(1) + 8192;
                setMessage(command, 0xE0, call.integer(0), bend & 0x7F, (bend >> 7) & 0x7F);
                setDelay(command, call, 2);
                return true;
            }
            case 5:
                setMessage(command, 0xC0, call.integer(0), call.integer(1), 0);
                command.size = 2;
                setDelay(command, call, 2);
                return true;
            case 6:
                if (!m_inMidiHook)
                {
                    std::snprintf(error, errorSize, "Pass() works only inside a hook for incoming MIDI");
                    return false;
                }
                command.bytes = m_current.bytes;
                command.size = m_current.size;
                setDelay(command, call, 0);
                return true;
            default:
                command.kind = MidiScriptCommand::Kind::Panic;
                return true;
        }
    }

    script::Status dispatch(script::Runtime& runtime, const MidiScriptEvent& event) override
    {
        m_current = event;
        m_inMidiHook = true;
        const script::Status status = call(runtime, event);
        m_inMidiHook = false;
        return status;
    }

  private:
    static void setMessage(MidiScriptCommand& command, const int status, const int channel, const int first,
                           const int second) noexcept
    {
        command.bytes = {static_cast<uint8_t>(status | (channel - 1)), static_cast<uint8_t>(first),
                         static_cast<uint8_t>(second)};
        command.size = 3;
    }

    static void setDelay(MidiScriptCommand& command, const script::CommandCall& call, const size_t first) noexcept
    {
        command.delayMs = call.real(first);
        command.delayTicks = call.real(first + 1);
    }

    static script::Status call(script::Runtime& runtime, const MidiScriptEvent& event)
    {
        const auto status = static_cast<double>(event.bytes[0]);
        const auto channel = static_cast<double>((event.bytes[0] & 0x0F) + 1);
        const auto first = static_cast<double>(event.bytes[1]);
        const auto second = static_cast<double>(event.bytes[2]);
        switch (event.kind)
        {
            case MidiScriptEvent::Kind::NoteOn:
            {
                const std::array<double, 3> arguments{channel, first, second};
                return runtime.call("OnNoteOn", arguments.data(), arguments.size());
            }
            case MidiScriptEvent::Kind::NoteOff:
            {
                const bool noteOnZero = (event.bytes[0] & 0xF0) == 0x90;
                const std::array<double, 3> arguments{channel, first, noteOnZero ? 0.0 : second};
                return runtime.call("OnNoteOff", arguments.data(), arguments.size());
            }
            case MidiScriptEvent::Kind::ControlChange:
            {
                const std::array<double, 3> arguments{channel, first, second};
                return runtime.call("OnCC", arguments.data(), arguments.size());
            }
            case MidiScriptEvent::Kind::PitchBend:
            {
                const std::array<double, 2> arguments{channel, ((event.bytes[2] << 7) | event.bytes[1]) - 8192.0};
                return runtime.call("OnPitchBend", arguments.data(), arguments.size());
            }
            case MidiScriptEvent::Kind::ProgramChange:
            {
                const std::array<double, 2> arguments{channel, first};
                return runtime.call("OnProgramChange", arguments.data(), arguments.size());
            }
            case MidiScriptEvent::Kind::Other:
            {
                const std::array<double, 3> arguments{status, first, second};
                return runtime.call("OnOther", arguments.data(), arguments.size());
            }
        }
        return script::Status::Ok;
    }

    MidiScriptEvent m_current{};
    bool m_inMidiHook{false};
};

}
