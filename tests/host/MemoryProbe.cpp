// Memory probe of one plugin (docs/testing.md, "Memory"): this file is compiled together with
// the plugin's own lv2/PluginEntry.cpp and its generated ports.h into an executable of its
// own, which replaces the global operator new and delete and counts what the plugin requests
// through them. It instantiates the plugin at 48 kHz, activates it, runs 2 s of noise in
// blocks of 128 (defaults, every control at its minimum, at its maximum, defaults again), then
// deactivates and cleans up, and prints JSON on stdout:
//   allocatedBytes   live after instantiate() and activate(): the plugin's memory
//   peakBytes        the most that was live during instantiate() and activate()
//   allocations      operator new calls during instantiate() and activate()
//   runAllocations, runBytes   operator new calls and bytes during all run() calls (must be 0)
//   leakedBytes      still live after cleanup()
//   workerAllocations   operator new calls inside work() (the host's worker thread; allocation is allowed there)
// With --check the exit code is 1 when run() allocates or cleanup leaves memory behind.
// std::malloc called directly is not counted (the plugins do not call it).

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#include "lv2/atom/atom.h"
#include "lv2/core/lv2.h"
#include "lv2/midi/midi.h"
#include "lv2/urid/urid.h"
#include "lv2/worker/worker.h"
#include "ports.h"

namespace
{
struct Counter
{
    bool active{false};
    size_t live{0};
    size_t peak{0};
    size_t calls{0};
    size_t bytes{0};
    size_t workerCalls{0};
    bool inWorker{false};
};

Counter g_counter;

// every block carries its size in a header, so operator delete without a size can subtract it
constexpr size_t c_header{alignof(std::max_align_t) > 16 ? alignof(std::max_align_t) : 16};

void* allocate(const size_t size, const size_t alignment)
{
    const size_t header = alignment > c_header ? alignment : c_header;
    void* raw = nullptr;
    if (posix_memalign(&raw, header, header + size) != 0)
    {
        throw std::bad_alloc();
    }
    auto* block = static_cast<unsigned char*>(raw) + header;
    std::memcpy(block - sizeof(size_t), &size, sizeof(size_t));
    std::memcpy(block - 2 * sizeof(size_t), &header, sizeof(size_t));
    if (g_counter.active)
    {
        g_counter.live += size;
        g_counter.peak = g_counter.live > g_counter.peak ? g_counter.live : g_counter.peak;
        if (g_counter.inWorker)
        {
            g_counter.workerCalls += 1;
        }
        else
        {
            g_counter.calls += 1;
            g_counter.bytes += size;
        }
    }
    return block;
}

void release(void* pointer) noexcept
{
    if (pointer == nullptr)
    {
        return;
    }
    auto* block = static_cast<unsigned char*>(pointer);
    size_t size = 0;
    size_t header = 0;
    std::memcpy(&size, block - sizeof(size_t), sizeof(size_t));
    std::memcpy(&header, block - 2 * sizeof(size_t), sizeof(size_t));
    if (g_counter.active)
    {
        g_counter.live -= size < g_counter.live ? size : g_counter.live;
    }
    std::free(block - header);
}
}

void* operator new(const size_t size)
{
    return allocate(size, 0);
}

void* operator new[](const size_t size)
{
    return allocate(size, 0);
}

void* operator new(const size_t size, const std::align_val_t alignment)
{
    return allocate(size, static_cast<size_t>(alignment));
}

void* operator new[](const size_t size, const std::align_val_t alignment)
{
    return allocate(size, static_cast<size_t>(alignment));
}

void* operator new(const size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return allocate(size, 0);
    }
    catch (...)
    {
        return nullptr;
    }
}

void operator delete(void* pointer) noexcept
{
    release(pointer);
}

void operator delete[](void* pointer) noexcept
{
    release(pointer);
}

void operator delete(void* pointer, size_t) noexcept
{
    release(pointer);
}

void operator delete[](void* pointer, size_t) noexcept
{
    release(pointer);
}

void operator delete(void* pointer, std::align_val_t) noexcept
{
    release(pointer);
}

void operator delete[](void* pointer, std::align_val_t) noexcept
{
    release(pointer);
}

void operator delete(void* pointer, size_t, std::align_val_t) noexcept
{
    release(pointer);
}

void operator delete[](void* pointer, size_t, std::align_val_t) noexcept
{
    release(pointer);
}

// A plugin with a MIDI input gets urid:map and a sequence of two events per block (a note on
// and a note off), so the allocation check covers the MIDI path of run() as well.
LV2_URID mapUri(LV2_URID_Map_Handle /*handle*/, const char* uri)
{
    return std::strcmp(uri, LV2_MIDI__MidiEvent) == 0 ? 1u : 2u;
}

// The host's worker: jobs are kept in fixed storage (no allocation in run()), worked after the run() that
// scheduled them, and their answers reach work_response() at the start of the next run().
struct WorkQueue
{
    static constexpr size_t c_slots{64};
    static constexpr size_t c_bytes{256};
    uint8_t data[c_slots][c_bytes]{};
    uint32_t sizes[c_slots]{};
    size_t count{0};
    uint8_t answers[c_slots][c_bytes]{};
    uint32_t answerSizes[c_slots]{};
    size_t answerCount{0};
};

WorkQueue g_work;

LV2_Worker_Status scheduleWork(LV2_Worker_Schedule_Handle /*handle*/, const uint32_t size, const void* data)
{
    if (g_work.count >= WorkQueue::c_slots || size > WorkQueue::c_bytes)
    {
        return LV2_WORKER_ERR_NO_SPACE;
    }
    std::memcpy(g_work.data[g_work.count], data, size);
    g_work.sizes[g_work.count++] = size;
    return LV2_WORKER_SUCCESS;
}

LV2_Worker_Status respondToWork(LV2_Worker_Respond_Handle /*handle*/, const uint32_t size, const void* data)
{
    if (g_work.answerCount >= WorkQueue::c_slots || size > WorkQueue::c_bytes)
    {
        return LV2_WORKER_ERR_NO_SPACE;
    }
    std::memcpy(g_work.answers[g_work.answerCount], data, size);
    g_work.answerSizes[g_work.answerCount++] = size;
    return LV2_WORKER_SUCCESS;
}

struct MidiOut
{
    LV2_Atom_Sequence sequence;
    uint8_t space[4096];
};

struct MidiEvents
{
    LV2_Atom_Sequence sequence;
    struct Event
    {
        LV2_Atom_Event header;
        uint8_t message[8];
    } events[2];
};

int main(const int argc, const char* const* argv)
{
    using Ports = jp::generated::PluginPorts;
    const bool check = argc > 1 && std::strcmp(argv[1], "--check") == 0;
    constexpr double c_sampleRate{48000.0};
    constexpr size_t c_block{128};
    const LV2_Descriptor* descriptor = lv2_descriptor(0);
    LV2_URID_Map uridMap{nullptr, mapUri};
    const LV2_Feature mapFeature{LV2_URID__map, &uridMap};
    LV2_Worker_Schedule workSchedule{nullptr, scheduleWork};
    const LV2_Feature workFeature{LV2_WORKER__schedule, &workSchedule};
    const LV2_Feature* features[3]{&mapFeature, &workFeature, nullptr};

    constexpr uint32_t audio = Ports::c_audioInputs + Ports::c_audioOutputs;
    constexpr uint32_t controlPorts = audio + Ports::c_controlInputs.size() + Ports::c_controlOutputs.size();
    constexpr uint32_t portCount = controlPorts + (Ports::c_midiInput ? 1u : 0u) + (Ports::c_midiOutput ? 1u : 0u);
    MidiOut midiOut{};
    MidiEvents midi{};
    midi.sequence.atom.type = 3u;
    midi.sequence.atom.size = sizeof(LV2_Atom_Sequence_Body) + sizeof(midi.events);
    for (size_t i = 0; i < 2; ++i)
    {
        auto& event = midi.events[i];
        event.header.time.frames = i == 0 ? 10 : 60;
        event.header.body.type = 1u;
        event.header.body.size = 3;
        event.message[0] = i == 0 ? 0x90 : 0x80;
        event.message[1] = 60;
        event.message[2] = 100;
    }
    std::vector<std::array<float, c_block>> buffers(audio);
    std::vector<float> controls(portCount, 0.0f);
    for (const auto& port : Ports::c_controlInputs)
    {
        controls[port.index] = port.defaultValue;
    }

    g_counter = Counter{};
    g_counter.active = true;
    LV2_Handle handle = descriptor->instantiate(descriptor, c_sampleRate, ".", features);
    if (handle == nullptr)
    {
        std::fprintf(stderr, "instantiate failed\n");
        return 2;
    }
    for (uint32_t i = 0; i < portCount; ++i)
    {
        if (Ports::c_midiInput && i == Ports::c_midiIndex)
        {
            descriptor->connect_port(handle, i, &midi);
            continue;
        }
        if (Ports::c_midiOutput && i == Ports::c_midiOutIndex)
        {
            descriptor->connect_port(handle, i, &midiOut);
            continue;
        }
        descriptor->connect_port(handle, i, i < audio ? buffers[i].data() : &controls[i]);
    }
    descriptor->activate(handle);
    const Counter setup = g_counter;
    const auto* worker = static_cast<const LV2_Worker_Interface*>(descriptor->extension_data(LV2_WORKER__interface));
    if (Ports::c_worker && worker == nullptr)
    {
        std::fprintf(stderr, "no worker interface\n");
        return 2;
    }

    g_counter.calls = 0;
    g_counter.bytes = 0;
    uint32_t noise = 12345u;
    const auto runFor = [&](const size_t blocks)
    {
        for (size_t b = 0; b < blocks; ++b)
        {
            for (uint32_t i = 0; i < Ports::c_audioInputs; ++i)
            {
                for (auto& x : buffers[i])
                {
                    noise = noise * 1664525u + 1013904223u;
                    x = 0.25f * (static_cast<float>(noise >> 8) / 8388608.0f - 1.0f);
                }
            }
            midiOut.sequence.atom.type = 0;
            midiOut.sequence.atom.size = sizeof(LV2_Atom_Sequence_Body) + sizeof(midiOut.space);
            // the answers of the jobs worked after the last run() arrive first
            for (size_t k = 0; Ports::c_worker && k < g_work.answerCount; ++k)
            {
                worker->work_response(handle, g_work.answerSizes[k], g_work.answers[k]);
            }
            g_work.answerCount = 0;
            descriptor->run(handle, static_cast<uint32_t>(c_block));
            // the host's worker thread: allocation is allowed here and counted apart
            g_counter.inWorker = true;
            for (size_t k = 0; Ports::c_worker && k < g_work.count; ++k)
            {
                worker->work(handle, respondToWork, nullptr, g_work.sizes[k], g_work.data[k]);
            }
            g_work.count = 0;
            g_counter.inWorker = false;
        }
    };
    const size_t second = static_cast<size_t>(c_sampleRate) / c_block;
    runFor(second / 2);
    for (const auto& port : Ports::c_controlInputs)
    {
        controls[port.index] = port.minimum;
    }
    runFor(second / 2);
    for (const auto& port : Ports::c_controlInputs)
    {
        controls[port.index] = port.maximum;
    }
    runFor(second / 2);
    for (const auto& port : Ports::c_controlInputs)
    {
        controls[port.index] = port.defaultValue;
    }
    runFor(second / 2);
    const size_t runCalls = g_counter.calls;
    const size_t runBytes = g_counter.bytes;
    const size_t workerCalls = g_counter.workerCalls;

    descriptor->deactivate(handle);
    descriptor->cleanup(handle);
    const size_t leaked = g_counter.live;
    g_counter.active = false;

    std::printf("{\"uri\": \"%s\", \"sampleRate\": %.0f, \"allocatedBytes\": %zu, \"peakBytes\": %zu, "
                "\"allocations\": %zu, \"runAllocations\": %zu, \"runBytes\": %zu, \"workerAllocations\": %zu, "
                "\"leakedBytes\": %zu}\n",
                Ports::c_uri, c_sampleRate, setup.live, setup.peak, setup.calls, runCalls, runBytes, workerCalls,
                leaked);
    if (check && (runCalls > 0 || leaked > 0))
    {
        std::fprintf(stderr, "run() allocated %zu times (%zu bytes), %zu bytes leaked after cleanup()\n", runCalls,
                     runBytes, leaked);
        return 1;
    }
    return 0;
}
