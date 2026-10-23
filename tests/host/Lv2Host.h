#pragma once

#include <dlfcn.h>

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "lv2/atom/atom.h"
#include "lv2/core/lv2.h"
#include "lv2/midi/midi.h"
#include "lv2/urid/urid.h"
#include "lv2/worker/worker.h"

namespace jp::host
{

namespace fs = std::filesystem;

/// One port as the bundle's TTL describes it.
struct PortInfo
{
    uint32_t index{0};
    std::string symbol;
    bool audio{false};
    bool atom{false}; ///< an atom port (the MIDI input or output), neither audio nor control
    bool input{false};
    bool enumeration{false};
    float minimum{0.0f};
    float maximum{0.0f};
    float defaultValue{0.0f};
};

/// What a host learns from a bundle: plugin URI, binary, ports and the files the manifest
/// points to.
struct BundleInfo
{
    fs::path dir;
    std::string uri;
    std::string binary;
    std::vector<std::string> seeAlso;
    std::vector<PortInfo> ports;
};

inline std::string readFile(const fs::path& path)
{
    std::ifstream in(path);
    if (!in)
    {
        throw std::runtime_error("cannot read " + path.string());
    }
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

/// The top-level [ ... ] blocks that follow "lv2:port", with nested blocks (scale points)
/// kept inside their port. Reads the Turtle subset generator/jpgen.py writes, not any TTL.
inline std::vector<std::string> portBlocks(const std::string& ttl)
{
    std::vector<std::string> blocks;
    size_t pos = ttl.find("lv2:port ");
    if (pos == std::string::npos)
    {
        return blocks;
    }
    pos += 9;
    while (pos < ttl.size())
    {
        const char c = ttl[pos];
        if (c == '[')
        {
            int depth = 0;
            bool quoted = false;
            const size_t start = pos;
            for (; pos < ttl.size(); ++pos)
            {
                const char d = ttl[pos];
                if (quoted)
                {
                    if (d == '\\')
                    {
                        ++pos;
                    }
                    else if (d == '"')
                    {
                        quoted = false;
                    }
                    continue;
                }
                if (d == '"')
                {
                    quoted = true;
                }
                else if (d == '[')
                {
                    ++depth;
                }
                else if (d == ']' && --depth == 0)
                {
                    break;
                }
            }
            blocks.push_back(ttl.substr(start, pos - start + 1));
            ++pos;
        }
        else if (c == '.')
        {
            break;
        }
        else
        {
            ++pos; // whitespace and the commas between blocks
        }
    }
    return blocks;
}

inline std::string match(const std::string& text, const std::string& pattern)
{
    std::smatch result;
    if (!std::regex_search(text, result, std::regex(pattern)))
    {
        return {};
    }
    return result[1].str();
}

inline float numberOrZero(const std::string& text)
{
    return text.empty() ? 0.0f : std::stof(text);
}

inline BundleInfo readBundle(const fs::path& dir)
{
    BundleInfo info;
    info.dir = dir;
    const std::string manifest = readFile(dir / "manifest.ttl");
    info.uri = match(manifest, R"(<([^>#]+)>\s+a lv2:Plugin)");
    info.binary = match(manifest, R"(lv2:binary <([^>]+)>)");
    const std::regex seeAlso(R"(rdfs:seeAlso <([^>]+)>)");
    for (auto it = std::sregex_iterator(manifest.begin(), manifest.end(), seeAlso); it != std::sregex_iterator(); ++it)
    {
        const std::string file = (*it)[1].str();
        if (std::find(info.seeAlso.begin(), info.seeAlso.end(), file) == info.seeAlso.end())
        {
            info.seeAlso.push_back(file);
        }
    }
    if (info.uri.empty() || info.binary.empty() || info.seeAlso.empty())
    {
        throw std::runtime_error("incomplete manifest in " + dir.string());
    }
    const std::string number = R"(([-+0-9.eE]+))";
    const std::string ttl = readFile(dir / info.seeAlso.front());
    for (const std::string& block : portBlocks(ttl))
    {
        PortInfo port;
        port.index = static_cast<uint32_t>(std::stoul(match(block, R"(lv2:index (\d+))")));
        port.symbol = match(block, R"(lv2:symbol "([^"]+)\")");
        port.audio = block.find("lv2:AudioPort") != std::string::npos;
        port.atom = block.find("atom:AtomPort") != std::string::npos;
        port.input = block.find("lv2:InputPort") != std::string::npos;
        port.enumeration = block.find("lv2:enumeration") != std::string::npos;
        port.minimum = numberOrZero(match(block, "lv2:minimum " + number));
        port.maximum = numberOrZero(match(block, "lv2:maximum " + number));
        port.defaultValue = numberOrZero(match(block, "lv2:default " + number));
        info.ports.push_back(port);
    }
    std::sort(info.ports.begin(), info.ports.end(),
              [](const PortInfo& a, const PortInfo& b) { return a.index < b.index; });
    return info;
}

/// Bundle directories (jaypacks-*.lv2) below an lv2 output directory, sorted.
inline std::vector<fs::path> findBundles(const fs::path& lv2Dir)
{
    std::vector<fs::path> bundles;
    if (!fs::is_directory(lv2Dir))
    {
        return bundles;
    }
    for (const auto& entry : fs::directory_iterator(lv2Dir))
    {
        const std::string name = entry.path().filename().string();
        if (entry.is_directory() && name.rfind("jaypacks-", 0) == 0 && entry.path().extension() == ".lv2")
        {
            bundles.push_back(entry.path());
        }
    }
    std::sort(bundles.begin(), bundles.end());
    return bundles;
}

/// The plugin binary of a bundle, loaded with dlopen; unloaded on destruction.
class Library
{
  public:
    explicit Library(const BundleInfo& bundle)
    {
        const fs::path path = bundle.dir / bundle.binary;
        m_handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (m_handle == nullptr)
        {
            throw std::runtime_error(std::string("dlopen failed: ") + dlerror());
        }
        using DescriptorFunction = const LV2_Descriptor* (*) (uint32_t);
        const auto function = reinterpret_cast<DescriptorFunction>(dlsym(m_handle, "lv2_descriptor"));
        if (function == nullptr)
        {
            throw std::runtime_error("no lv2_descriptor in " + path.string());
        }
        m_descriptor = function(0);
        m_secondDescriptor = function(1);
    }

    ~Library()
    {
        dlclose(m_handle);
    }

    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;

    [[nodiscard]] const LV2_Descriptor* descriptor() const
    {
        return m_descriptor;
    }

    [[nodiscard]] const LV2_Descriptor* secondDescriptor() const
    {
        return m_secondDescriptor;
    }

  private:
    void* m_handle{nullptr};
    const LV2_Descriptor* m_descriptor{nullptr};
    const LV2_Descriptor* m_secondDescriptor{nullptr};
};

/// One MIDI message at an absolute sample position (counted from the first sample of the next
/// process() or processChannels() call). size is 1 to 3 bytes; unused data bytes are 0.
struct MidiMessage
{
    uint64_t sample{0};
    uint8_t status{0};
    uint8_t data1{0};
    uint8_t data2{0};
    uint32_t size{3};

    bool operator==(const MidiMessage& other) const
    {
        return sample == other.sample && status == other.status && data1 == other.data1 && data2 == other.data2 &&
               size == other.size;
    }
};

/// One plugin instance with every port connected: control inputs start at the TTL defaults,
/// control outputs are read with output(); process() gives every audio input the same signal,
/// processChannels() one signal per input. The host provides urid:map and the worker; a MIDI input is fed
/// from the messages queued with sendMidi(), the messages a MIDI output wrote are collected by takeMidiOut().
///
/// The worker behaves like mod-host's: schedule_work() queues a copy of the job, the plugin's work() runs after
/// the run() that scheduled it (on a thread of its own with startWorkerThread(), otherwise at the end of that
/// run()), and the answers reach work_response() at the start of the next run(), never in the same one.
class Instance
{
  public:
    Instance(const LV2_Descriptor* descriptor, const BundleInfo& bundle, const double sampleRate)
        : m_descriptor(descriptor)
        , m_ports(bundle.ports)
        , m_controls(bundle.ports.size(), 0.0f)
        , m_midiBuffer(c_midiBufferBytes / sizeof(uint64_t), 0)
        , m_midiOutBuffer(c_midiOutBufferBytes / sizeof(uint64_t), 0)
    {
        m_uridMap.handle = this;
        m_uridMap.map = &Instance::mapUri;
        m_mapFeature = {LV2_URID__map, &m_uridMap};
        m_workSchedule = {this, &Instance::scheduleWork};
        m_workFeature = {LV2_WORKER__schedule, &m_workSchedule};
        m_features[0] = &m_mapFeature;
        m_features[1] = &m_workFeature;
        m_features[2] = nullptr;
        m_handle = descriptor->instantiate(descriptor, sampleRate, bundle.dir.c_str(), m_features);
        if (m_handle == nullptr)
        {
            throw std::runtime_error("instantiate failed");
        }
        for (const PortInfo& port : m_ports)
        {
            if (port.audio)
            {
                (port.input ? m_inputIndices : m_outputIndices).push_back(port.index);
            }
            else if (port.atom && port.input)
            {
                m_midiIndex = static_cast<int64_t>(port.index);
                writeMidiBlock(0, 0);
                m_descriptor->connect_port(m_handle, port.index, m_midiBuffer.data());
            }
            else if (port.atom)
            {
                m_midiOutIndex = static_cast<int64_t>(port.index);
                m_descriptor->connect_port(m_handle, port.index, m_midiOutBuffer.data());
            }
            else
            {
                m_controls[port.index] = port.defaultValue;
                m_descriptor->connect_port(m_handle, port.index, &m_controls[port.index]);
            }
        }
        m_descriptor->activate(m_handle);
        m_workerInterface =
            static_cast<const LV2_Worker_Interface*>(m_descriptor->extension_data(LV2_WORKER__interface));
    }

    ~Instance()
    {
        stopWorkerThread();
        m_descriptor->deactivate(m_handle);
        m_descriptor->cleanup(m_handle);
    }

    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;

    void setControl(const std::string& symbol, const float value)
    {
        for (const PortInfo& port : m_ports)
        {
            if (port.symbol == symbol)
            {
                m_controls[port.index] = value;
                return;
            }
        }
        throw std::runtime_error("no control " + symbol);
    }

    void setControlByIndex(const uint32_t index, const float value)
    {
        m_controls.at(index) = value;
    }

    /// The value the plugin last wrote to a control output.
    [[nodiscard]] float output(const std::string& symbol) const
    {
        for (const PortInfo& port : m_ports)
        {
            if (port.symbol == symbol && !port.audio && !port.atom && !port.input)
            {
                return m_controls[port.index];
            }
        }
        throw std::runtime_error("no control output " + symbol);
    }

    /// All control outputs in port order.
    [[nodiscard]] std::vector<float> outputs() const
    {
        std::vector<float> values;
        for (const PortInfo& port : m_ports)
        {
            if (!port.audio && !port.atom && !port.input)
            {
                values.push_back(m_controls[port.index]);
            }
        }
        return values;
    }

    [[nodiscard]] size_t audioInputs() const
    {
        return m_inputIndices.size();
    }

    /// Queues a MIDI message for the next process() or processChannels() call; the queue is
    /// cleared when that call returns. Messages are delivered in the block that contains their
    /// sample, at their frame inside it.
    void sendMidi(const uint64_t sample, const uint8_t status, const uint8_t data1 = 0, const uint8_t data2 = 0,
                  const uint32_t size = 3)
    {
        if (m_midiIndex < 0)
        {
            throw std::runtime_error("the plugin has no MIDI input");
        }
        m_midiQueue.push_back({sample, status, data1, data2, size});
    }

    [[nodiscard]] bool hasMidiInput() const
    {
        return m_midiIndex >= 0;
    }

    [[nodiscard]] bool hasMidiOutput() const
    {
        return m_midiOutIndex >= 0;
    }

    [[nodiscard]] bool hasWorker() const
    {
        return m_workerInterface != nullptr;
    }

    /// The messages a MIDI output wrote since the last call, at their absolute sample (counted from the first sample
    /// of the first process() call after the instance was made).
    std::vector<MidiMessage> takeMidiOut()
    {
        std::vector<MidiMessage> messages;
        messages.swap(m_midiOutMessages);
        return messages;
    }

    /// Runs work() on a thread of the instance's own (as the host does) instead of at the end of each run().
    void startWorkerThread()
    {
        if (m_workerThread.joinable() || !hasWorker())
        {
            return;
        }
        m_stopWorker = false;
        m_workerThread = std::thread([this] { workerLoop(); });
    }

    /// Waits until every scheduled job has been worked (with a worker thread; otherwise returns at once).
    void waitForWorker()
    {
        std::unique_lock<std::mutex> lock(m_workMutex);
        m_workIdle.wait(lock, [this] { return m_jobs.empty() && !m_working; });
    }

    /// The host refuses the next jobs, as a full ring does (schedule_work returns an error).
    void refuseJobs(const bool refuse)
    {
        std::lock_guard<std::mutex> lock(m_workMutex);
        m_refuseJobs = refuse;
    }

    /// Jobs the plugin scheduled so far.
    [[nodiscard]] size_t jobsScheduled() const
    {
        std::lock_guard<std::mutex> lock(m_workMutex);
        return m_jobsScheduled;
    }

    void reactivate()
    {
        m_descriptor->deactivate(m_handle);
        m_descriptor->activate(m_handle);
    }

    /// Runs the whole input in blocks of blockSize and returns the first output channel (zeros for a plugin
    /// without audio output). A plugin without audio inputs runs for input.size() samples. With inPlace, the first
    /// input and output share one buffer. beforeBlock, if given, is called with the index of the block's first sample
    /// before each run(), e.g. to move a control the way a host does between blocks.
    std::vector<float> process(const std::vector<float>& input, const size_t blockSize, const bool inPlace = false,
                               const std::function<void(size_t)>& beforeBlock = {})
    {
        return processChannels(std::vector<std::vector<float>>(m_inputIndices.size(), input), input.size(), blockSize,
                               inPlace, beforeBlock);
    }

    /// As process(), with one signal per audio input (each at least length samples long).
    std::vector<float> processChannels(const std::vector<std::vector<float>>& channels, const size_t length,
                                       const size_t blockSize, const bool inPlace = false,
                                       const std::function<void(size_t)>& beforeBlock = {})
    {
        if (channels.size() != m_inputIndices.size())
        {
            throw std::runtime_error("one signal per audio input expected");
        }
        const bool shared = inPlace && !m_inputIndices.empty();
        std::vector<float> result(length);
        std::vector<std::vector<float>> inputs(m_inputIndices.size(), std::vector<float>(blockSize));
        std::vector<std::vector<float>> outputs(m_outputIndices.size(), std::vector<float>(blockSize));
        for (size_t pos = 0; pos < length; pos += blockSize)
        {
            const size_t n = std::min(blockSize, length - pos);
            for (size_t i = 0; i < inputs.size(); ++i)
            {
                std::copy(channels[i].begin() + static_cast<std::ptrdiff_t>(pos),
                          channels[i].begin() + static_cast<std::ptrdiff_t>(pos + n), inputs[i].begin());
            }
            for (size_t i = 0; i < m_inputIndices.size(); ++i)
            {
                m_descriptor->connect_port(m_handle, m_inputIndices[i], inputs[i].data());
            }
            for (size_t i = 0; i < m_outputIndices.size(); ++i)
            {
                float* buffer = (shared && i == 0) ? inputs[0].data() : outputs[i].data();
                m_descriptor->connect_port(m_handle, m_outputIndices[i], buffer);
            }
            if (beforeBlock)
            {
                beforeBlock(pos);
            }
            writeMidiBlock(pos, n);
            prepareMidiOut();
            deliverAnswers();
            m_descriptor->run(m_handle, static_cast<uint32_t>(n));
            collectMidiOut(pos, n);
            if (!m_workerThread.joinable())
            {
                workPendingJobs();
            }
            if (!outputs.empty()) // a MIDI processor has no audio output: the result stays 0
            {
                const float* first = shared ? inputs[0].data() : outputs[0].data();
                std::copy(first, first + n, result.begin() + static_cast<std::ptrdiff_t>(pos));
            }
        }
        m_midiQueue.clear();
        return result;
    }

  private:
    static constexpr size_t c_midiBufferBytes{16384};
    static constexpr size_t c_midiOutBufferBytes{65536};

    static LV2_Worker_Status scheduleWork(LV2_Worker_Schedule_Handle handle, const uint32_t size, const void* data)
    {
        auto* self = static_cast<Instance*>(handle);
        std::lock_guard<std::mutex> lock(self->m_workMutex);
        if (self->m_refuseJobs)
        {
            return LV2_WORKER_ERR_NO_SPACE;
        }
        const auto* bytes = static_cast<const uint8_t*>(data);
        self->m_jobs.emplace_back(bytes, bytes + size);
        ++self->m_jobsScheduled;
        self->m_workWake.notify_one();
        return LV2_WORKER_SUCCESS;
    }

    static LV2_Worker_Status respondToWork(LV2_Worker_Respond_Handle handle, const uint32_t size, const void* data)
    {
        auto* self = static_cast<Instance*>(handle);
        const auto* bytes = static_cast<const uint8_t*>(data);
        std::lock_guard<std::mutex> lock(self->m_answerMutex);
        self->m_answers.emplace_back(bytes, bytes + size);
        return LV2_WORKER_SUCCESS;
    }

    /// Runs the jobs queued so far (the calling thread plays the host's worker).
    void workPendingJobs()
    {
        if (m_workerInterface == nullptr)
        {
            return;
        }
        for (;;)
        {
            std::vector<uint8_t> job;
            {
                std::lock_guard<std::mutex> lock(m_workMutex);
                if (m_jobs.empty())
                {
                    return;
                }
                job = std::move(m_jobs.front());
                m_jobs.erase(m_jobs.begin());
            }
            m_workerInterface->work(m_handle, &Instance::respondToWork, this, static_cast<uint32_t>(job.size()),
                                    job.data());
        }
    }

    void workerLoop()
    {
        std::unique_lock<std::mutex> lock(m_workMutex);
        while (!m_stopWorker)
        {
            m_workWake.wait(lock, [this] { return m_stopWorker || !m_jobs.empty(); });
            while (!m_jobs.empty())
            {
                std::vector<uint8_t> job = std::move(m_jobs.front());
                m_jobs.erase(m_jobs.begin());
                m_working = true;
                lock.unlock();
                m_workerInterface->work(m_handle, &Instance::respondToWork, this, static_cast<uint32_t>(job.size()),
                                        job.data());
                lock.lock();
                m_working = false;
            }
            m_workIdle.notify_all();
        }
    }

    void stopWorkerThread()
    {
        if (!m_workerThread.joinable())
        {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m_workMutex);
            m_stopWorker = true;
        }
        m_workWake.notify_all();
        m_workerThread.join();
    }

    /// Hands the answers of finished jobs to the plugin, as the host does at the start of a cycle.
    void deliverAnswers()
    {
        if (m_workerInterface == nullptr)
        {
            return;
        }
        std::vector<std::vector<uint8_t>> answers;
        {
            std::lock_guard<std::mutex> lock(m_answerMutex);
            answers.swap(m_answers);
        }
        for (const auto& answer : answers)
        {
            m_workerInterface->work_response(m_handle, static_cast<uint32_t>(answer.size()), answer.data());
        }
    }

    /// An output sequence arrives with its capacity in the atom size and no type, as in a host.
    void prepareMidiOut()
    {
        if (m_midiOutIndex < 0)
        {
            return;
        }
        auto* sequence = reinterpret_cast<LV2_Atom_Sequence*>(m_midiOutBuffer.data());
        sequence->atom.type = 0;
        sequence->atom.size = static_cast<uint32_t>(c_midiOutBufferBytes - sizeof(LV2_Atom));
    }

    /// Reads the events the plugin wrote; the sequence must be well formed with frames inside the block, in order.
    void collectMidiOut(const size_t pos, const size_t n)
    {
        if (m_midiOutIndex < 0)
        {
            return;
        }
        const auto* sequence = reinterpret_cast<const LV2_Atom_Sequence*>(m_midiOutBuffer.data());
        if (sequence->atom.type != mapUri(this, LV2_ATOM__Sequence))
        {
            throw std::runtime_error("the MIDI output was not made a sequence");
        }
        const auto* cursor = reinterpret_cast<const uint8_t*>(m_midiOutBuffer.data()) + sizeof(LV2_Atom_Sequence);
        const auto* end =
            reinterpret_cast<const uint8_t*>(m_midiOutBuffer.data()) + sizeof(LV2_Atom) + sequence->atom.size;
        int64_t previous = 0;
        while (cursor < end)
        {
            const auto* event = reinterpret_cast<const LV2_Atom_Event*>(cursor);
            if (event->time.frames < previous || event->time.frames >= static_cast<int64_t>(n) ||
                event->body.size == 0 || event->body.size > 3 || event->body.type != mapUri(this, LV2_MIDI__MidiEvent))
            {
                throw std::runtime_error("the MIDI output has an invalid event");
            }
            previous = event->time.frames;
            const uint8_t* body = cursor + sizeof(LV2_Atom_Event);
            MidiMessage message;
            message.sample = pos + static_cast<uint64_t>(event->time.frames);
            message.size = event->body.size;
            message.status = body[0];
            message.data1 = event->body.size > 1 ? body[1] : 0;
            message.data2 = event->body.size > 2 ? body[2] : 0;
            m_midiOutMessages.push_back(message);
            cursor += (sizeof(LV2_Atom_Event) + event->body.size + 7u) & ~size_t{7};
        }
    }

    static LV2_URID mapUri(LV2_URID_Map_Handle handle, const char* uri)
    {
        auto& uris = static_cast<Instance*>(handle)->m_uris;
        const auto found = std::find(uris.begin(), uris.end(), uri);
        if (found != uris.end())
        {
            return static_cast<LV2_URID>(found - uris.begin() + 1);
        }
        uris.emplace_back(uri);
        return static_cast<LV2_URID>(uris.size());
    }

    /// Fills the MIDI input with the queued messages whose sample lies in [pos, pos + n), in the
    /// atom sequence layout (8-byte header, then events padded to 8 bytes).
    void writeMidiBlock(const size_t pos, const size_t n)
    {
        if (m_midiIndex < 0)
        {
            return;
        }
        auto* sequence = reinterpret_cast<LV2_Atom_Sequence*>(m_midiBuffer.data());
        uint8_t* cursor = reinterpret_cast<uint8_t*>(m_midiBuffer.data()) + sizeof(LV2_Atom_Sequence);
        std::stable_sort(m_midiQueue.begin(), m_midiQueue.end(),
                         [](const MidiMessage& a, const MidiMessage& b) { return a.sample < b.sample; });
        for (const MidiMessage& message : m_midiQueue)
        {
            if (message.sample < pos || message.sample >= pos + n)
            {
                continue;
            }
            auto* event = reinterpret_cast<LV2_Atom_Event*>(cursor);
            event->time.frames = static_cast<int64_t>(message.sample - pos);
            event->body.type = mapUri(this, LV2_MIDI__MidiEvent);
            event->body.size = message.size;
            uint8_t* body = cursor + sizeof(LV2_Atom_Event);
            body[0] = message.status;
            body[1] = message.data1;
            body[2] = message.data2;
            cursor += (sizeof(LV2_Atom_Event) + message.size + 7u) & ~7u;
            if (static_cast<size_t>(cursor - reinterpret_cast<uint8_t*>(m_midiBuffer.data())) + 32 > c_midiBufferBytes)
            {
                throw std::runtime_error("too many MIDI messages in one block");
            }
        }
        sequence->atom.type = mapUri(this, LV2_ATOM__Sequence);
        sequence->atom.size = static_cast<uint32_t>(cursor - reinterpret_cast<uint8_t*>(&sequence->body));
        sequence->body.unit = 0;
        sequence->body.pad = 0;
    }

    const LV2_Descriptor* m_descriptor;
    std::vector<PortInfo> m_ports;
    std::vector<float> m_controls;
    std::vector<uint32_t> m_inputIndices;
    std::vector<uint32_t> m_outputIndices;
    LV2_URID_Map m_uridMap{};
    LV2_Feature m_mapFeature{};
    LV2_Worker_Schedule m_workSchedule{};
    LV2_Feature m_workFeature{};
    const LV2_Feature* m_features[3]{};
    std::vector<std::string> m_uris;
    std::vector<uint64_t> m_midiBuffer; ///< 8-byte aligned storage of the atom sequence
    std::vector<MidiMessage> m_midiQueue;
    std::vector<uint64_t> m_midiOutBuffer;
    std::vector<MidiMessage> m_midiOutMessages;
    int64_t m_midiIndex{-1};
    int64_t m_midiOutIndex{-1};
    LV2_Handle m_handle{nullptr};
    const LV2_Worker_Interface* m_workerInterface{nullptr};
    mutable std::mutex m_workMutex;
    std::condition_variable m_workWake;
    std::condition_variable m_workIdle;
    std::vector<std::vector<uint8_t>> m_jobs;
    size_t m_jobsScheduled{0};
    bool m_refuseJobs{false};
    bool m_stopWorker{false};
    bool m_working{false};
    std::thread m_workerThread;
    std::mutex m_answerMutex;
    std::vector<std::vector<uint8_t>> m_answers;
};

}
