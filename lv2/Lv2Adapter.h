#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

#include "lv2/FlushDenormals.h"
#include "lv2/PedalServices.h"
#include "lv2/PortTable.h"
#include "lv2/atom/atom.h"
#include "lv2/atom/util.h"
#include "lv2/core/lv2.h"
#include "lv2/core/lv2_util.h"
#include "lv2/midi/midi.h"
#include "lv2/urid/urid.h"
#include "lv2/worker/worker.h"

namespace jp
{

namespace detail
{

/// Whether a pedal wants to know the folder of its bundle: setBundlePath(const char*).
template <typename Pedal, typename = void>
struct WantsBundlePath : std::false_type
{
};

template <typename Pedal>
struct WantsBundlePath<Pedal,
                       std::void_t<decltype(std::declval<Pedal&>().setBundlePath(static_cast<const char*>(nullptr)))>>
    : std::true_type
{
};

}

/**
 * @brief The LV2 plugin interface for one pedal class, driven by a generated port table.
 *
 * Ports describes the plugin (generator/jpgen.py writes it into ports.h):
 *   using Pedal = ...;                         the DSP class
 *   static constexpr const char* c_uri;        plugin URI, as in the TTL
 *   static constexpr uint32_t c_audioInputs, c_audioOutputs;
 *   static constexpr std::array<ControlInput<Pedal>, N> c_controlInputs;
 *   static constexpr std::array<ControlOutput<Pedal>, M> c_controlOutputs;
 *   static constexpr bool c_midiInput;         whether the plugin has a MIDI input
 *   static constexpr uint32_t c_midiIndex;     its port index (only used if c_midiInput)
 *   static constexpr bool c_midiOutput;        whether the plugin has a MIDI output
 *   static constexpr uint32_t c_midiOutIndex;  its port index (only used if c_midiOutput)
 *   static constexpr bool c_worker;            whether the plugin uses the host's worker thread
 * Port indices are audio inputs, then audio outputs, then the control inputs, then the control
 * outputs, each in table order, then the MIDI input, then the MIDI output (checked at compile time).
 *
 * Pedal needs setSampleRate(float), reset(), one setter per control input taking the port
 * value and one const getter per control output; for one input and one output
 * process(const float* in, float* out, size_t n), for no input and one output (a signal
 * source) process(float* out, size_t n), otherwise
 * process(const float* const* in, float* const* out, size_t n). process() must work in place
 * (LV2 hosts may pass the same buffer as input and output) and for any n. A plugin with neither
 * audio input nor audio output (a MIDI processor) has process(size_t n).
 *
 * A plugin with a MIDI input (an atom sequence port that supports midi:MidiEvent) also needs
 * midi(const uint8_t* msg, uint32_t size). run() cuts the block at the frame of every MIDI event: the
 * audio before the event is processed first, then midi() is called, so events act sample accurately and
 * the output does not depend on the block size. msg points at 3 readable bytes (the bytes beyond size
 * are 0); events of 1 to 3 bytes are forwarded (channel voice and system messages), longer ones
 * (system exclusive) are dropped. Controls are applied once per block, before the first event.
 *
 * A pedal that has setBundlePath(const char* folder) is told the folder of its bundle (with the trailing slash the host
 * gives, valid for the call only: the pedal copies it) before setSampleRate(), for the files it ships.
 *
 * A plugin with a MIDI output needs setMidiOutput(MidiOutput&), called once after construction: the pedal
 * writes messages from process() and midi() through it (lv2/PedalServices.h), at frames counted from the start of
 * the call, and the adapter empties the output sequence at the start of every run(). The sequence is in the order
 * of the frames written.
 *
 * A plugin with c_worker (the host feature work:schedule is required, instantiate() fails without it) needs
 * setWorkScheduler(WorkScheduler&), called once after construction, work(const void* data, uint32_t size,
 * WorkResponder&) and workResponse(const void* data, uint32_t size). work() runs on the host's worker thread at
 * any time during or after a run(), concurrently with it, so the pedal synchronises what both sides touch (the
 * adapter does not); it may allocate and block. workResponse() runs on the audio thread at the start of a later
 * run(), before the controls are applied, and must follow the real-time rules of run(), but does not write MIDI.
 *
 * Contract towards the host: the pedal is created with new and destroyed with delete (it may
 * hold non-trivial members); run() handles any block length, allocates nothing, takes no
 * locks, does no I/O and runs with flush-to-zero (ScopedFlushDenormals). A control value is
 * clamped to its range and passed to the setter only when it differs from the last one
 * applied; NaN is ignored (tested on the bit pattern, so it also holds under -ffast-math). Smoothing is the pedal's
 * job, so the adapter has no block-rate steps. activate() applies all connected controls and calls reset(), which
 * clears the DSP state and snaps smoothed parameters to the host's values instead of gliding from the constructor
 * defaults. After process() every connected control output receives its getter's value (block rate, which is
 * what hosts read anyway). No extension data; the only required feature is urid:map, and only for a
 * plugin with a MIDI input (instantiate() fails without it).
 */
template <typename Ports>
class Lv2Adapter
{
  public:
    using Pedal = typename Ports::Pedal;
    static constexpr uint32_t c_audioInputs{Ports::c_audioInputs};
    static constexpr uint32_t c_audioOutputs{Ports::c_audioOutputs};
    static constexpr size_t c_controlCount{Ports::c_controlInputs.size()};
    static constexpr size_t c_outputCount{Ports::c_controlOutputs.size()};
    static constexpr uint32_t c_firstControl{c_audioInputs + c_audioOutputs};
    static constexpr uint32_t c_firstOutput{c_firstControl + static_cast<uint32_t>(c_controlCount)};
    static constexpr bool c_midiInput{Ports::c_midiInput};
    static constexpr uint32_t c_midiIndex{Ports::c_midiIndex};
    static constexpr bool c_midiOutput{Ports::c_midiOutput};
    static constexpr uint32_t c_midiOutIndex{Ports::c_midiOutIndex};
    static constexpr bool c_worker{Ports::c_worker};

    static const LV2_Descriptor* descriptor() noexcept
    {
        static const LV2_Descriptor descriptor{Ports::c_uri, instantiate, connectPort, activate,
                                               run,          deactivate,  cleanup,     extensionData};
        return &descriptor;
    }

  private:
    static constexpr bool indicesFollowTheLayout() noexcept
    {
        for (size_t i = 0; i < c_controlCount; ++i)
        {
            if (Ports::c_controlInputs[i].index != c_firstControl + i)
            {
                return false;
            }
        }
        for (size_t i = 0; i < c_outputCount; ++i)
        {
            if (Ports::c_controlOutputs[i].index != c_firstOutput + i)
            {
                return false;
            }
        }
        return true;
    }
    static_assert(indicesFollowTheLayout(), "control port indices must follow the audio ports in table order");
    static_assert(!c_midiInput || c_midiIndex == c_firstOutput + c_outputCount,
                  "the MIDI input follows the control outputs");
    static_assert(!c_midiOutput || c_midiOutIndex == c_firstOutput + c_outputCount + (c_midiInput ? 1u : 0u),
                  "the MIDI output follows the MIDI input");
    static_assert(c_audioOutputs > 0 || c_midiOutput, "a pedal has audio outputs or a MIDI output");

    /// What instantiate() takes from the host's features.
    struct HostFeatures
    {
        LV2_URID midiEvent{0};
        LV2_URID sequence{0};
        const LV2_Worker_Schedule* schedule{nullptr};
        const char* bundlePath{""};
    };

    explicit Lv2Adapter(const double sampleRate, const HostFeatures& features)
        : m_midiEventUrid(features.midiEvent)
        , m_sequenceUrid(features.sequence)
    {
        if constexpr (detail::WantsBundlePath<Pedal>::value)
        {
            m_pedal.setBundlePath(features.bundlePath);
        }
        m_pedal.setSampleRate(static_cast<float>(sampleRate));
        if constexpr (c_midiOutput)
        {
            m_pedal.setMidiOutput(m_midiOut);
        }
        if constexpr (c_worker)
        {
            m_scheduler.setSchedule(features.schedule);
            m_pedal.setWorkScheduler(m_scheduler);
        }
    }

    static LV2_Handle instantiate(const LV2_Descriptor* /*descriptor*/, const double sampleRate, const char* bundlePath,
                                  [[maybe_unused]] const LV2_Feature* const* features)
    {
        HostFeatures host;
        host.bundlePath = bundlePath != nullptr ? bundlePath : "";
        if constexpr (c_midiInput || c_midiOutput)
        {
            const auto* map = static_cast<const LV2_URID_Map*>(lv2_features_data(features, LV2_URID__map));
            if (map == nullptr || map->map == nullptr)
            {
                return nullptr; // urid:map is a required feature of this plugin
            }
            host.midiEvent = map->map(map->handle, LV2_MIDI__MidiEvent);
            host.sequence = map->map(map->handle, LV2_ATOM__Sequence);
        }
        if constexpr (c_worker)
        {
            host.schedule = static_cast<const LV2_Worker_Schedule*>(lv2_features_data(features, LV2_WORKER__schedule));
            if (host.schedule == nullptr || host.schedule->schedule_work == nullptr)
            {
                return nullptr; // work:schedule is a required feature of this plugin
            }
        }
        return new (std::nothrow) Lv2Adapter(sampleRate, host);
    }

    static void connectPort(LV2_Handle instance, const uint32_t port, void* data) noexcept
    {
        auto& self = *static_cast<Lv2Adapter*>(instance);
        if (port < c_audioInputs)
        {
            self.m_inputs[port] = static_cast<const float*>(data);
        }
        else if (port < c_firstControl)
        {
            self.m_outputs[port - c_audioInputs] = static_cast<float*>(data);
        }
        else if (port < c_firstOutput)
        {
            self.m_controls[port - c_firstControl] = static_cast<const float*>(data);
        }
        else if (port - c_firstOutput < c_outputCount)
        {
            self.m_reports[port - c_firstOutput] = static_cast<float*>(data);
        }
        else if (c_midiInput && port == c_midiIndex)
        {
            self.m_midiIn = static_cast<const LV2_Atom_Sequence*>(data);
        }
        else if (c_midiOutput && port == c_midiOutIndex)
        {
            self.m_midiOutPort = static_cast<LV2_Atom_Sequence*>(data);
        }
    }

    static void activate(LV2_Handle instance) noexcept
    {
        auto& self = *static_cast<Lv2Adapter*>(instance);
        self.m_hasApplied.fill(false);
        self.applyControls();
        self.m_pedal.reset();
        self.writeReports();
    }

    static void run(LV2_Handle instance, const uint32_t sampleCount) noexcept
    {
        auto& self = *static_cast<Lv2Adapter*>(instance);
        if constexpr (c_midiOutput)
        {
            self.m_midiOut.begin(self.m_midiOutPort, self.m_sequenceUrid, self.m_midiEventUrid, sampleCount);
        }
        for (const float* input : self.m_inputs)
        {
            if (input == nullptr)
            {
                return;
            }
        }
        for (float* output : self.m_outputs)
        {
            if (output == nullptr)
            {
                return;
            }
        }
        const ScopedFlushDenormals flushDenormals;
        self.applyControls();
        if constexpr (c_midiInput)
        {
            self.processWithMidi(sampleCount);
        }
        else
        {
            self.processRange(0, sampleCount);
        }
        self.writeReports();
    }

    static void deactivate(LV2_Handle /*instance*/) noexcept {}

    static void cleanup(LV2_Handle instance) noexcept
    {
        delete static_cast<Lv2Adapter*>(instance);
    }

    static const void* extensionData(const char* uri) noexcept
    {
        if constexpr (c_worker)
        {
            if (uri != nullptr && std::strcmp(uri, LV2_WORKER__interface) == 0)
            {
                static const LV2_Worker_Interface interface{work, workResponse, endRun};
                return &interface;
            }
        }
        (void) uri;
        return nullptr;
    }

    static LV2_Worker_Status work(LV2_Handle instance, const LV2_Worker_Respond_Function respond,
                                  const LV2_Worker_Respond_Handle handle, const uint32_t size,
                                  const void* data) noexcept
    {
        WorkResponder responder(respond, handle);
        static_cast<Lv2Adapter*>(instance)->m_pedal.work(data, size, responder);
        return LV2_WORKER_SUCCESS;
    }

    static LV2_Worker_Status workResponse(LV2_Handle instance, const uint32_t size, const void* body) noexcept
    {
        static_cast<Lv2Adapter*>(instance)->m_pedal.workResponse(body, size);
        return LV2_WORKER_SUCCESS;
    }

    static LV2_Worker_Status endRun(LV2_Handle /*instance*/) noexcept
    {
        return LV2_WORKER_SUCCESS;
    }

    /// Runs the pedal over the frames [offset, offset + count) of the block.
    void processRange(const uint32_t offset, const uint32_t count) noexcept
    {
        if (count == 0)
        {
            return;
        }
        if constexpr (c_midiOutput)
        {
            m_midiOut.setCallOffset(offset);
        }
        if constexpr (c_audioInputs == 0 && c_audioOutputs == 0)
        {
            m_pedal.process(count);
        }
        else if constexpr (c_audioInputs == 1 && c_audioOutputs == 1)
        {
            m_pedal.process(m_inputs[0] + offset, m_outputs[0] + offset, count);
        }
        else if constexpr (c_audioInputs == 0 && c_audioOutputs == 1)
        {
            m_pedal.process(m_outputs[0] + offset, count);
        }
        else if (offset == 0)
        {
            m_pedal.process(m_inputs.data(), m_outputs.data(), count);
        }
        else
        {
            std::array<const float*, c_audioInputs> inputs{};
            std::array<float*, c_audioOutputs> outputs{};
            for (size_t i = 0; i < c_audioInputs; ++i)
            {
                inputs[i] = m_inputs[i] + offset;
            }
            for (size_t i = 0; i < c_audioOutputs; ++i)
            {
                outputs[i] = m_outputs[i] + offset;
            }
            m_pedal.process(inputs.data(), outputs.data(), count);
        }
    }

    /// Cuts the block at the frame of every MIDI event: audio up to the event, then the event.
    /// Events outside the block are moved to its edges, events out of order keep the position of the
    /// one before.
    void processWithMidi(const uint32_t sampleCount) noexcept
    {
        uint32_t done = 0;
        if (m_midiIn != nullptr)
        {
            LV2_ATOM_SEQUENCE_FOREACH(m_midiIn, event)
            {
                const uint32_t size = event->body.size;
                if (event->body.type != m_midiEventUrid || size == 0 || size > 3)
                {
                    continue;
                }
                uint32_t frame = event->time.frames <= 0 ? 0u : sampleCount;
                if (event->time.frames > 0 && event->time.frames < static_cast<int64_t>(sampleCount))
                {
                    frame = static_cast<uint32_t>(event->time.frames);
                }
                frame = frame < done ? done : frame;
                processRange(done, frame - done);
                done = frame;
                std::array<uint8_t, 3> message{};
                std::memcpy(message.data(), LV2_ATOM_BODY_CONST(&event->body), size);
                if constexpr (c_midiOutput)
                {
                    m_midiOut.setCallOffset(frame);
                }
                m_pedal.midi(message.data(), size);
            }
        }
        processRange(done, sampleCount - done);
    }

    [[nodiscard]] static bool isNan(const float value) noexcept
    {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        return (bits & 0x7fffffffu) > 0x7f800000u;
    }

    void applyControls() noexcept
    {
        for (size_t i = 0; i < c_controlCount; ++i)
        {
            if (m_controls[i] == nullptr)
            {
                continue;
            }
            const float raw = *m_controls[i];
            if (isNan(raw))
            {
                continue;
            }
            const auto& port = Ports::c_controlInputs[i];
            const float value = raw < port.minimum ? port.minimum : (raw > port.maximum ? port.maximum : raw);
            if (!m_hasApplied[i] || value != m_applied[i])
            {
                m_hasApplied[i] = true;
                m_applied[i] = value;
                (m_pedal.*port.setter)(value);
            }
        }
    }

    void writeReports() const noexcept
    {
        for (size_t i = 0; i < c_outputCount; ++i)
        {
            if (m_reports[i] != nullptr)
            {
                *m_reports[i] = (m_pedal.*Ports::c_controlOutputs[i].getter)();
            }
        }
    }

    Pedal m_pedal;
    std::array<const float*, c_audioInputs> m_inputs{};
    std::array<float*, c_audioOutputs> m_outputs{};
    std::array<const float*, c_controlCount> m_controls{};
    std::array<float, c_controlCount> m_applied{};
    std::array<bool, c_controlCount> m_hasApplied{};
    std::array<float*, c_outputCount> m_reports{};
    const LV2_Atom_Sequence* m_midiIn{nullptr};
    LV2_Atom_Sequence* m_midiOutPort{nullptr};
    MidiOutput m_midiOut;
    WorkScheduler m_scheduler;
    LV2_URID m_midiEventUrid{0};
    LV2_URID m_sequenceUrid{0};
};

}
