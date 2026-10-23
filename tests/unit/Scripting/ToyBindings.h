#pragma once

// A tiny binding set that has nothing to do with any plugin: it shows that the scripting core works with commands,
// hooks, constants, events and commands of any plugin. Three commands (a number, a table of fields of every kind, two
// positional values), two hooks and a table of constants.

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "scripting/Binding.h"
#include "scripting/LuaRuntime.h"
#include "scripting/ScriptHost.h"
#include "scripting/Timers.h"

namespace jp::script::test
{

enum class ToyCommandKind : uint8_t
{
    Gain,
    Voice,
    Pair
};

struct ToyCommand
{
    ToyCommandKind kind{ToyCommandKind::Gain};
    int32_t index{0};
    float a{0.0f};
    float b{0.0f};
    int32_t wave{0};
    bool on{false};
    char label[16]{};
};

struct ToyEvent
{
    uint8_t kind{0}; ///< 1 note on, 2 note off
    uint8_t channel{0};
    uint8_t note{0};
    uint8_t velocity{0};
};

inline constexpr std::array<ValueSpec, 1> c_gainArgs{{{"db", ValueKind::Number, -60.0, 12.0, 0.0, "level in dB"}}};
inline constexpr std::array<ValueSpec, 1> c_voiceArgs{{{"index", ValueKind::Integer, 0.0, 2.0, 0.0, "which voice"}}};
inline constexpr std::array<ValueSpec, 5> c_voiceFields{{
    {"level", ValueKind::Number, 0.0, 1.0, 0.5, "linear level"},
    {"detune", ValueKind::Number, -12.0, 12.0, 0.0, "semitones"},
    {"wave", ValueKind::Integer, 0.0, 2.0, 0.0, "waveform", "Wave"},
    {"label", ValueKind::Text, 0.0, 0.0, 0.0, "a short name"},
    {"on", ValueKind::Flag, 0.0, 1.0, 0.0, "switched on"},
}};
inline constexpr std::array<ValueSpec, 2> c_pairArgs{{
    {"a", ValueKind::Number, 0.0, 100.0, 0.0, "first"},
    {"b", ValueKind::Number, 0.0, 100.0, 0.0, "second"},
}};
/// The index of SetTimer in the table: the host answers it and the one after it itself.
inline constexpr size_t c_timerCommandBase{3};

inline constexpr std::array<CommandSpec, 5> c_commands{{
    {"SetGain", "Sets the gain.", c_gainArgs.data(), 1, nullptr, 0},
    {"SetVoice", "Sets one voice.", c_voiceArgs.data(), 1, c_voiceFields.data(), 5},
    {"SetPair", "Two numbers.", c_pairArgs.data(), 2, nullptr, 0},
    c_timerCommands[0],
    c_timerCommands[1],
}};
inline constexpr std::array<const char*, 3> c_noteArguments{"channel", "note", "velocity"};
inline constexpr std::array<HookSpec, 6> c_hooks{{
    {"OnStart", "Runs once after the script is loaded.", nullptr, 0},
    {"OnNoteOn", "A note began.", c_noteArguments.data(), 3},
    c_coreHooks[0],
    c_coreHooks[1],
    c_coreHooks[2],
    c_coreHooks[3],
}};
inline constexpr std::array<EnumEntry, 3> c_waves{{{"Sine", 0}, {"Saw", 1}, {"Square", 2}}};
inline constexpr std::array<EnumSpec, 1> c_enums{{{"Wave", "Waveforms of a voice.", c_waves.data(), 3}}};

inline const BindingSet& toyBindings()
{
    static const BindingSet set{"Toy",          c_commands.data(), c_commands.size(), c_hooks.data(),
                                c_hooks.size(), c_enums.data(),    c_enums.size()};
    return set;
}

/// Fills a ToyCommand from a call; "bad" as a label is rejected.
inline bool toyCommand(const CommandCall& call, ToyCommand& command, char* error, const size_t errorSize) noexcept
{
    switch (call.command)
    {
        case 0:
            command.kind = ToyCommandKind::Gain;
            command.a = call.real(0);
            return true;
        case 1:
            command.kind = ToyCommandKind::Voice;
            command.index = call.integer(0);
            command.a = call.real(1);
            command.b = call.real(2);
            command.wave = call.integer(3);
            command.on = call.flag(5);
            std::strncpy(command.label, call.string(4), sizeof(command.label) - 1);
            if (std::strcmp(call.string(4), "bad") == 0)
            {
                std::snprintf(error, errorSize, "the label 'bad' is not allowed");
                return false;
            }
            return true;
        default:
            command.kind = ToyCommandKind::Pair;
            command.a = call.real(0);
            command.b = call.real(1);
            return true;
    }
}

class ToyBinder : public ScriptBinder<ToyEvent, ToyCommand>
{
  public:
    [[nodiscard]] const BindingSet& bindings() const noexcept override
    {
        return toyBindings();
    }

    bool makeCommand(const CommandCall& call, ToyCommand& command, char* error, size_t errorSize) noexcept override
    {
        return toyCommand(call, command, error, errorSize);
    }

    Status dispatch(Runtime& runtime, const ToyEvent& event) override
    {
        if (event.kind != 1)
        {
            return Status::Ok;
        }
        const std::array<double, 3> arguments{static_cast<double>(event.channel), static_cast<double>(event.note),
                                              static_cast<double>(event.velocity)};
        return runtime.call("OnNoteOn", arguments.data(), arguments.size());
    }

    bool embeddedModule(const char* name, std::string& text) noexcept override
    {
        if (std::strcmp(name, "embedded") == 0)
        {
            text = "embedded_loaded = true";
            return true;
        }
        return false;
    }
};

/// Collects the calls a Runtime passes on.
class RecordingSink : public CommandSink
{
  public:
    bool onCommand(const CommandCall& call, char* error, const size_t errorSize) noexcept override
    {
        ToyCommand command;
        if (!toyCommand(call, command, error, errorSize))
        {
            return false;
        }
        calls.push_back(call);
        return true;
    }

    std::vector<CommandCall> calls;
};

}
