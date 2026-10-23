#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "dsp/Filters/OnePoleHighpass.h"
#include "dsp/Filters/OnePoleLowpass.h"
#include "dsp/Filters/PoleMixingFilter.h"
#include "dsp/NonLinear/WaveShaperTables.h"
#include "dsp/Numbers/Convert.h"
#include "dsp/Numbers/Random.h"
#include "dsp/Parameters/ParameterSmoothing.h"
#include "dsp/SynthHandling/SustainPedalHandler.h"
#include "dsp/Synthesizer/MorphexsynthEffects.h"
#include "dsp/Synthesizer/MorphexsynthVoice.h"
#include "dsp/Synthesizer/MpeZone.h"
#include "dsp/Synthesizer/VoiceAllocator.h"
#include "dsp/Wavetables/WaveTableBank.h"
#include "lv2/PedalServices.h"
#include "plugins/morphexsynth/MorphPatch.h"
#include "plugins/morphexsynth/MorphexScript.h"
#include "plugins/morphexsynth/MorphexScriptFiles.h"
#include "scripting/ScriptHost.h"

namespace jp
{

/**
 * @brief Morphexsynth: an MPE subtractive synth of 12 voices (three wavetable oscillators, a resonant filter, three
 * envelopes, an LFO, distortion and a 10-slot routing matrix per voice) on a phaser, chorus and reverb bus, played
 * from MIDI, no audio input. After AbacDsp's examples/morphexsynth; the sounds are Lua scripts: the ten factory patches
 * (the .lua files of the scripts folder of the bundle, selected by the Patch port) and the user's own (user-1.lua to
 * user-8.lua in the user's scripts folder, selected by the Script port).
 *
 * Scripts: a factory script is run once when the plugin is created (setSampleRate()) and its settings are kept as a
 * value, so choosing a patch is a copy of that value and happens in the audio thread at once. A user script lives in
 * a ScriptHost (scripting/): the host's worker thread loads it, reloads it when the file changes, and calls its hooks
 * (OnNoteOn ... ) for the MIDI the pedal forwards; the commands it calls (SetFilter ...) come back and are applied at
 * the start of the next process() call, so a hook acts one or two blocks after its event. A script that takes over
 * (the first load, every reload) starts from a blank patch and releases all notes; one that fails to load is rejected
 * and the previous sound stays (scriptStatus says why, <name>.log in the scripts folder has the text). The Patch
 * port is ignored while the Script port is above 0.
 *
 * MIDI (midi()): channel voice messages on the master channel and on the member channels of the MPE zone (ports
 * MPE master, lower, upper; default 1, 2 and 16) play and control; others are ignored. Note on (velocity 0 is a note
 * off) and note off go through a sustain pedal handler (CC 64, 64 and above is down); the voices come from a pool of
 * 12 (a silent voice, else the one assigned longest ago); CC 74 and CC 1 are the per-note Y, channel pressure the Z
 * (poly pressure too, for the one note it names), pitch bend (14 bit, centre 8192) the X of the notes of their channel,
 * which the patch routes to pitch, cutoff, resonance, pan, oscillator levels and others; CC 123 releases all notes. A
 * value of X, Y or Z that arrives before the note on of its channel (an MPE controller sends the bend of a touch first)
 * is kept per channel and applied to the next note on that channel, where AbacDsp lets the first note start without it
 * (poly pressure is not kept: it belongs to one note). Program change is ignored.
 *
 * Controls: Patch (the patch applies completely, all notes are released first); Vol in dB, added to the level of the
 * patch; Cutoff as an offset in semitones and Resonance as an offset in percent of the normalised resonance (both
 * added to the patch's values; the patches carry their own cutoff, so a dial value would override them); Attack and
 * Release as offsets in ms added to the times of the patch's amplitude envelope (at least 1 ms remain, so a patch with
 * a short attack can be lengthened and a long one shortened); Reverb as an offset in dB of the patch's reverb mix (the
 * lowest value switches the reverb off), Chorus and Phaser as a percentage of the patch's mixes (clamped to 1); Bend
 * range in semitones (default 12; 0: as the patch) for the PitchBend slots; Glide in ms per octave (0: as the patch);
 * Pressure as a percentage (default 200) of the depth of the slots that read Z; the MPE zone. The defaults of Vol (-6
 * dB) and Reverb (-10 dB) bring the factory patches, which are loud, down.
 *
 * Audio: the voices sum into a stereo pair that passes a DC blocker (20 Hz), the effects bus and the volume (smoothed,
 * 5 ms). The sound is made in blocks of 16 samples (the block of the reverb tank and of the voices' control rate): a
 * MIDI event takes effect at the start of the next block, up to 15 samples (0.3 ms) after its frame, and the output
 * does not depend on how the host cuts its blocks. Everything is allocated by setSampleRate() (the wavetables take
 * about 60 ms on the Dwarf); process() and midi() allocate nothing.
 */
class MorphexsynthPedal
{
  public:
    static constexpr size_t c_voices{12};
    static constexpr size_t c_blockSize{16};
    static constexpr size_t c_maxHeldNotes{64};
    static constexpr float c_reverbOffMinDb{-60.0f};
    static constexpr float c_volumeSmoothingMs{5.0f};
    static constexpr float c_dcBlockerHz{20.0f};

    void setSampleRate(const float sampleRate)
    {
        m_sampleRate = sampleRate;
        m_engine = std::make_unique<Engine>(sampleRate, m_settings);
        loadFactoryPatches();
        script::HostConfig config;
        config.factoryDir = m_factoryDirectory;
        config.userDir = m_userDirectory;
        config.sampleRate = static_cast<double>(sampleRate);
        m_script.configure(config);
    }

    void reset()
    {
        if (m_engine)
        {
            m_engine->reset(m_settings);
            activateSound();
        }
    }

    // --- controls ---

    void setPatch(const float value)
    {
        const auto index =
            static_cast<size_t>(std::clamp(std::lround(value), 0L, static_cast<long>(c_factoryPatchCount - 1)));
        m_settings.patch = index;
        if (m_engine && m_settings.script == 0)
        {
            loadFactory();
        }
    }

    void setVol(const float decibels)
    {
        m_settings.volumeDb = decibels;
        if (m_engine)
        {
            m_engine->applyVolume(m_settings);
        }
    }

    void setCutoff(const float semitones)
    {
        m_settings.cutoffOffset = semitones;
        if (m_engine)
        {
            m_engine->applyFilter(m_settings);
        }
    }

    void setResonance(const float percent)
    {
        m_settings.resonanceOffset = percent;
        if (m_engine)
        {
            m_engine->applyFilter(m_settings);
        }
    }

    void setAttack(const float milliseconds)
    {
        m_settings.attackOffsetMs = milliseconds;
        if (m_engine)
        {
            m_engine->applyEnvelope(m_settings);
        }
    }

    void setRelease(const float milliseconds)
    {
        m_settings.releaseOffsetMs = milliseconds;
        if (m_engine)
        {
            m_engine->applyEnvelope(m_settings);
        }
    }

    void setReverb(const float decibels)
    {
        m_settings.reverbOffsetDb = decibels;
        if (m_engine)
        {
            m_engine->applyEffects(m_settings);
        }
    }

    void setChorus(const float percent)
    {
        m_settings.chorusPercent = percent;
        if (m_engine)
        {
            m_engine->applyEffects(m_settings);
        }
    }

    void setPhaser(const float percent)
    {
        m_settings.phaserPercent = percent;
        if (m_engine)
        {
            m_engine->applyEffects(m_settings);
        }
    }

    void setBendRange(const float semitones)
    {
        m_settings.bendRange = semitones;
        if (m_engine)
        {
            m_engine->applyRouting(m_settings);
        }
    }

    void setPressure(const float percent)
    {
        m_settings.pressurePercent = percent;
        if (m_engine)
        {
            m_engine->applyRouting(m_settings);
        }
    }

    void setGlide(const float msPerOctave)
    {
        m_settings.glide = msPerOctave;
        if (m_engine)
        {
            m_engine->applyRouting(m_settings);
        }
    }

    void setMpeMaster(const float channel)
    {
        m_settings.master = static_cast<int>(std::lround(channel));
        if (m_engine)
        {
            m_engine->applyZone(m_settings);
        }
    }

    void setMpeLower(const float channel)
    {
        m_settings.lower = static_cast<int>(std::lround(channel));
        if (m_engine)
        {
            m_engine->applyZone(m_settings);
        }
    }

    void setMpeUpper(const float channel)
    {
        m_settings.upper = static_cast<int>(std::lround(channel));
        if (m_engine)
        {
            m_engine->applyZone(m_settings);
        }
    }

    /// 0: the Patch port selects the sound, 1 to 8: the user script user-<n>.lua.
    void setScript(const float value)
    {
        m_settings.script =
            static_cast<size_t>(std::clamp(std::lround(value), 0L, static_cast<long>(c_userScriptCount)));
        if (m_engine)
        {
            selectSound();
        }
    }

    // --- scripts ---

    /// The folder of the bundle (with or without the trailing slash): the factory scripts are in its scripts folder.
    /// Before setSampleRate().
    void setBundlePath(const char* folder)
    {
        std::string base = folder != nullptr ? folder : "";
        while (base.size() > 1 && base.back() == '/')
        {
            base.pop_back();
        }
        setScriptDirectories(base + "/scripts", defaultUserScriptDirectory());
    }

    /// The folders of the factory and of the user's scripts. Before setSampleRate().
    void setScriptDirectories(const std::string& factory, const std::string& user)
    {
        m_factoryDirectory = factory;
        m_userDirectory = user;
    }

    void setWorkScheduler(WorkScheduler& scheduler) noexcept
    {
        m_script.setWorkScheduler(scheduler);
    }

    /// The host's worker thread: loads and reloads the user script and runs its hooks (scripting/ScriptHost.h).
    void work(const void* /*data*/, const uint32_t /*size*/, WorkResponder& /*responder*/) noexcept
    {
        m_script.work();
    }

    void workResponse(const void* /*data*/, const uint32_t /*size*/) noexcept {}

    /// How the selected script is: 0 idle, 1 ok, 2 file missing, 3 compile error, 4 runtime error, 5 over the
    /// instruction or time budget, 6 out of memory (scripting::ScriptState).
    [[nodiscard]] float getScriptStatus() const noexcept
    {
        const script::ScriptState state = m_settings.script == 0 ? m_factory[m_settings.patch].state : m_script.state();
        return static_cast<float>(state);
    }

    // --- MIDI and audio ---

    /// msg points at 3 readable bytes, those beyond size are 0 (lv2/Lv2Adapter.h).
    void midi(const uint8_t* msg, const uint32_t /*size*/) noexcept
    {
        if (m_engine)
        {
            if (m_settings.script > 0)
            {
                forwardToScript(msg);
            }
            m_engine->midi(msg);
        }
    }

    void process(const float* const* /*in*/, float* const* out, const size_t numSamples) noexcept
    {
        if (!m_engine)
        {
            std::fill_n(out[0], numSamples, 0.0f);
            std::fill_n(out[1], numSamples, 0.0f);
            return;
        }
        if (m_settings.script > 0)
        {
            m_script.advance(static_cast<uint32_t>(numSamples));
            drainScript();
        }
        m_engine->process(out[0], out[1], numSamples);
    }

    /// Voices that are sounding (including their release).
    [[nodiscard]] size_t activeVoices() const noexcept
    {
        return m_engine ? m_engine->activeVoices() : 0;
    }

    [[nodiscard]] size_t getPatchIndex() const noexcept
    {
        return m_settings.patch;
    }

  private:
    /// The values of the control ports.
    struct Settings
    {
        size_t patch{c_defaultFactoryPatch};
        float volumeDb{-6.0f};
        float cutoffOffset{0.0f};
        float resonanceOffset{0.0f};
        float attackOffsetMs{0.0f};
        float releaseOffsetMs{0.0f};
        float reverbOffsetDb{-10.0f};
        float chorusPercent{100.0f};
        float phaserPercent{100.0f};
        float bendRange{12.0f};
        float pressurePercent{200.0f};
        float glide{0.0f};
        int master{1};
        int lower{2};
        int upper{16};
        size_t script{0};
    };

    /// A factory patch: the settings its script made, and how the run of the script went.
    struct FactoryPatch
    {
        MorphPatch patch{};
        script::ScriptState state{script::ScriptState::Idle};
    };

    /// import "name" in a factory script finds <name>.lua next to it.
    class FolderModules : public script::ModuleSource
    {
      public:
        explicit FolderModules(std::string folder)
            : m_folder(std::move(folder))
        {
        }

        bool find(const char* name, std::string& text) noexcept override
        {
            return script::detail::readTextFile(m_folder + "/" + name + ".lua", c_maxScriptBytes, text);
        }

      private:
        std::string m_folder;
    };

    static constexpr size_t c_maxScriptBytes{100000};
    static constexpr std::array<const char*, c_userScriptCount> c_userScriptNames{
        "user-1", "user-2", "user-3", "user-4", "user-5", "user-6", "user-7", "user-8"};

    using Voice = MorphexsynthVoice;
    using Effects = MorphexsynthEffects<c_blockSize>;

    class Engine : public NoteListener
    {
      public:
        Engine(const float sampleRate, const Settings& settings)
            : m_sampleRate(sampleRate)
            , m_bank({BasicWave::Triangle, BasicWave::SharkFin, BasicWave::Saw, BasicWave::Square, BasicWave::White})
            , m_effects(sampleRate)
        {
            m_sustain.setListener(this);
            buildVoices();
            const float pole = onePoleLowpassPole(c_dcBlockerHz, sampleRate);
            for (auto& blocker : m_dcBlocker)
            {
                blocker.setPole(pole);
            }
            m_volume.setSampleRate(sampleRate);
            m_volume.setTimeMs(c_volumeSmoothingMs);
            applyEverything(settings);
            m_volume.snapTo(m_volume.getTarget());
        }

        void reset(const Settings& settings)
        {
            m_sustain.setListener(nullptr);
            m_sustain.allNotesOff();
            m_sustain.setListener(this);
            m_patch = MorphPatch{};
            m_reverbSize = -1.0f;
            m_reverbDecay = -1.0f;
            buildVoices();
            m_allocator.clear();
            m_channels = {};
            for (auto& blocker : m_dcBlocker)
            {
                blocker.reset();
            }
            m_effects.reset();
            m_position = c_blockSize;
            applyEverything(settings);
            m_volume.snapTo(m_volume.getTarget());
        }

        // --- settings ---

        /// Takes a patch: all notes are released, every voice and the effects get its settings.
        void loadPatch(const MorphPatch& patch, const Settings& settings)
        {
            m_sustain.allNotesOff();
            m_patch = patch;
            applyEverything(settings);
        }

        /// One setting of a script, applied at once to every voice (a playing note follows) and to the patch.
        void applyScriptCommand(const MorphCommand& command, const Settings& settings) noexcept
        {
            jp::applyCommand(m_patch, command);
            switch (command.kind)
            {
                case MorphCommandKind::Volume:
                    applyVolume(settings);
                    break;
                case MorphCommandKind::Oscillator:
                {
                    const size_t index = command.index % m_patch.oscillators.size();
                    for (auto& voice : m_voices)
                    {
                        configureOscillator(*voice, index, m_patch.oscillators[index]);
                    }
                    break;
                }
                case MorphCommandKind::WaveMorph:
                {
                    const size_t index = command.index % m_patch.morphs.size();
                    for (auto& voice : m_voices)
                    {
                        configureMorph(*voice, index, m_patch.morphs[index]);
                    }
                    break;
                }
                case MorphCommandKind::AmplitudeEnvelope:
                    applyEnvelope(settings);
                    break;
                case MorphCommandKind::FilterEnvelope:
                    for (auto& voice : m_voices)
                    {
                        configureFilterEnvelope(*voice, m_patch.filterEnvelope);
                    }
                    break;
                case MorphCommandKind::PitchEnvelope:
                    for (auto& voice : m_voices)
                    {
                        configurePitchEnvelope(*voice, m_patch.pitchEnvelope);
                    }
                    applyRouting(settings);
                    break;
                case MorphCommandKind::Lfo:
                    for (auto& voice : m_voices)
                    {
                        configureLfo(*voice, m_patch.lfo);
                    }
                    break;
                case MorphCommandKind::Filter:
                    for (auto& voice : m_voices)
                    {
                        voice->setFilterPreset(m_patch.filter.preset);
                    }
                    applyFilter(settings);
                    break;
                case MorphCommandKind::Distortion:
                    for (auto& voice : m_voices)
                    {
                        voice->setPresetWaveTable(m_patch.distortion);
                    }
                    break;
                case MorphCommandKind::Slot:
                {
                    const size_t index = command.index % c_mpeSlotCount;
                    for (auto& voice : m_voices)
                    {
                        configureSlot(*voice, index, m_patch.slots[index]);
                    }
                    applyRouting(settings);
                    break;
                }
                case MorphCommandKind::Phaser:
                case MorphCommandKind::Chorus:
                case MorphCommandKind::Reverb:
                    applyEffects(settings);
                    break;
                case MorphCommandKind::MpeZone:
                    break;
            }
        }

        [[nodiscard]] bool zoneAccepts(const int channel) const noexcept
        {
            return m_zone.accepts(channel);
        }

        void applyEverything(const Settings& settings)
        {
            const MorphPatch& patch = m_patch;
            for (auto& voice : m_voices)
            {
                configureVoice(*voice, patch, settings);
            }
            applyFilter(settings);
            applyEnvelope(settings);
            applyVolume(settings);
            applyZone(settings);
            applyEffects(settings, true);
        }

        void applyVolume(const Settings& settings) noexcept
        {
            m_volume.setTarget(dbToGain(m_patch.volumeDb + settings.volumeDb));
        }

        void applyFilter(const Settings& settings) noexcept
        {
            const MorphPatch& patch = m_patch;
            const float cutoff = std::clamp(patch.filter.cutoff + settings.cutoffOffset, 0.0f, 127.0f);
            const float resonance =
                std::clamp(patch.filter.resonance + 0.01f * settings.resonanceOffset, 0.0f, c_maxResonance);
            for (auto& voice : m_voices)
            {
                voice->setCutoff(cutoff);
                voice->setResonance(resonance);
            }
        }

        void applyEnvelope(const Settings& settings) noexcept
        {
            for (auto& voice : m_voices)
            {
                configureAmplitude(*voice, m_patch.amplitude, settings);
            }
        }

        void applyRouting(const Settings& settings) noexcept
        {
            const MorphPatch& patch = m_patch;
            for (auto& voice : m_voices)
            {
                applyRouting(*voice, patch, settings);
            }
        }

        void applyZone(const Settings& settings) noexcept
        {
            m_zone.setMasterChannel(settings.master);
            m_zone.setLowerMemberChannel(settings.lower);
            m_zone.setUpperMemberChannel(settings.upper);
        }

        void applyEffects(const Settings& settings, const bool force = false) noexcept
        {
            const MorphPatch& patch = m_patch;
            m_effects.setPhaser(patch.phaser.rateHz, patch.phaser.depth, patch.phaser.feedback,
                                std::clamp(patch.phaser.mix * 0.01f * settings.phaserPercent, 0.0f, 1.0f));
            m_effects.setChorus(patch.chorus.rateHz, patch.chorus.depth,
                                std::clamp(patch.chorus.mix * 0.01f * settings.chorusPercent, 0.0f, 1.0f));
            const float mixDb =
                settings.reverbOffsetDb <= c_reverbOffMinDb ? -100.0f : patch.reverb.mixDb + settings.reverbOffsetDb;
            m_effects.setReverbDry(patch.reverb.dryDb);
            m_effects.setReverbMix(mixDb);
            if (force || patch.reverb.sizeMeters != m_reverbSize)
            {
                m_effects.setReverbSize(patch.reverb.sizeMeters);
                m_reverbSize = patch.reverb.sizeMeters;
            }
            if (force || patch.reverb.decayMs != m_reverbDecay)
            {
                m_effects.setReverbDecay(patch.reverb.decayMs);
                m_reverbDecay = patch.reverb.decayMs;
            }
        }

        // --- MIDI ---

        void midi(const uint8_t* msg) noexcept
        {
            const uint8_t status = msg[0];
            if (status < 0x80 || status >= 0xF0)
            {
                return;
            }
            const int channel = status & 0x0F;
            if (!m_zone.accepts(channel))
            {
                return;
            }
            switch (status & 0xF0)
            {
                case 0x90:
                    if (msg[2] == 0)
                    {
                        m_sustain.noteOff(channel, msg[1], 0x40);
                    }
                    else
                    {
                        m_sustain.noteOn(channel, msg[1], msg[2]);
                    }
                    break;
                case 0x80:
                    m_sustain.noteOff(channel, msg[1], msg[2]);
                    break;
                case 0xB0:
                    if (msg[1] == c_ccSustainPedal)
                    {
                        m_sustain.setSustain(msg[2] >= 64);
                    }
                    else if (msg[1] == c_ccTimbre || msg[1] == c_ccModWheel)
                    {
                        setY(channel, msg[2] << 7);
                    }
                    else if (msg[1] == c_ccAllNotesOff)
                    {
                        m_sustain.allNotesOff();
                    }
                    break;
                case 0xA0:
                    // poly pressure: the Z of one note (the channel's voices that play it)
                    m_allocator.forNote(channel, msg[1],
                                        [this, value = msg[2] << 7](const size_t index)
                                        {
                                            if (m_voices[index]->isPlaying())
                                            {
                                                m_voices[index]->mpeZ(value);
                                            }
                                        });
                    break;
                case 0xD0:
                    setZ(channel, msg[1] << 7);
                    break;
                case 0xE0:
                    setX(channel, (static_cast<int>(msg[2]) << 7) | msg[1]);
                    break;
                default:
                    break;
            }
        }

        void noteOn(const int channel, const int note, const int velocity) override
        {
            const VoiceAssignment assignment = m_allocator.allocate(channel, note, [this](const size_t index)
                                                                    { return !m_voices[index]->isPlaying(); });
            Voice& voice = *m_voices[assignment.voice];
            voice.triggerVoice(note, velocity, assignment.previousNote);
            const ChannelState& state = m_channels[static_cast<size_t>(channel)];
            if (state.x >= 0)
            {
                voice.mpeX(state.x);
            }
            if (state.y >= 0)
            {
                voice.mpeY(state.y);
            }
            if (state.z >= 0)
            {
                voice.mpeZ(state.z);
            }
        }

        void noteOff(const int channel, const int note, const int /*velocity*/) override
        {
            m_allocator.forNote(channel, note, [this](const size_t index) { m_voices[index]->stopVoice(); });
        }

        // --- audio ---

        void process(float* left, float* right, const size_t numSamples) noexcept
        {
            for (size_t i = 0; i < numSamples; ++i)
            {
                if (m_position == c_blockSize)
                {
                    renderBlock();
                }
                left[i] = m_outLeft[m_position];
                right[i] = m_outRight[m_position];
                ++m_position;
            }
        }

        [[nodiscard]] size_t activeVoices() const noexcept
        {
            return static_cast<size_t>(
                std::count_if(m_voices.begin(), m_voices.end(), [](const auto& voice) { return voice->isPlaying(); }));
        }

      private:
        static constexpr uint8_t c_ccSustainPedal{64};
        static constexpr uint8_t c_ccModWheel{1};
        static constexpr uint8_t c_ccTimbre{74};
        static constexpr uint8_t c_ccAllNotesOff{123};
        static constexpr float c_maxResonance{1.3f};
        static constexpr float c_minEnvelopeMs{1.0f};

        /// The last X, Y and Z of a channel (14 bit), -1 until one has arrived.
        struct ChannelState
        {
            int x{-1};
            int y{-1};
            int z{-1};
        };

        void buildVoices()
        {
            for (size_t i = 0; i < c_voices; ++i)
            {
                m_voices[i] = std::make_unique<Voice>(m_sampleRate, m_bank, m_shaperTables,
                                                      Random::c_defaultSeed + 16 * static_cast<uint64_t>(i));
            }
        }

        void setX(const int channel, const int value) noexcept
        {
            m_channels[static_cast<size_t>(channel)].x = value;
            m_allocator.forChannel(channel,
                                   [this, value](const size_t index)
                                   {
                                       if (m_voices[index]->isPlaying())
                                       {
                                           m_voices[index]->mpeX(value);
                                       }
                                   });
        }

        void setY(const int channel, const int value) noexcept
        {
            m_channels[static_cast<size_t>(channel)].y = value;
            m_allocator.forChannel(channel,
                                   [this, value](const size_t index)
                                   {
                                       if (m_voices[index]->isPlaying())
                                       {
                                           m_voices[index]->mpeY(value);
                                       }
                                   });
        }

        void setZ(const int channel, const int value) noexcept
        {
            m_channels[static_cast<size_t>(channel)].z = value;
            m_allocator.forChannel(channel,
                                   [this, value](const size_t index)
                                   {
                                       if (m_voices[index]->isPlaying())
                                       {
                                           m_voices[index]->mpeZ(value);
                                       }
                                   });
        }

        static void applyRouting(Voice& voice, const MorphPatch& patch, const Settings& settings) noexcept
        {
            for (size_t i = 0; i < c_mpeSlotCount; ++i)
            {
                const SlotPatch& slot = patch.slots[i];
                const bool bend = slot.target == CtrlTarget::PitchBend && settings.bendRange > 0.0f;
                float depth = bend ? settings.bendRange / 12.0f : slot.depth;
                if (slot.source == CtrlDimension::Z)
                {
                    depth *= 0.01f * settings.pressurePercent;
                }
                voice.setCtrlDepth(i, depth);
            }
            voice.setGlide(settings.glide > 0.0f ? settings.glide : patch.pitchEnvelope.glideMsPerOctave);
        }

        static void configureOscillator(Voice& voice, const size_t index, const OscillatorPatch& osc) noexcept
        {
            voice.setWaveForm(index, static_cast<size_t>(osc.waveform));
            voice.setLevelOscillator(index, osc.level);
            voice.setDetune(index, osc.detune);
            voice.setPwmOscillator(index, osc.pwm);
            voice.setPitchFactor(index, osc.pitchFactor);
        }

        static void configureMorph(Voice& voice, const size_t index, const WaveMorphPatch& morph) noexcept
        {
            voice.setWaveMorph(index, static_cast<size_t>(morph.second), static_cast<size_t>(morph.third),
                               morph.position);
        }

        static void configureAmplitude(Voice& voice, const EnvelopePatch& envelope, const Settings& settings) noexcept
        {
            voice.setEnvelopeAttack(std::max(envelope.attackMs + settings.attackOffsetMs, c_minEnvelopeMs));
            voice.setEnvelopeDecay(envelope.decayMs);
            voice.setEnvelopeSustainLevel(envelope.sustainLevel);
            voice.setEnvelopeRelease(std::max(envelope.releaseMs + settings.releaseOffsetMs, c_minEnvelopeMs));
        }

        static void configureFilterEnvelope(Voice& voice, const FilterEnvelopePatch& envelope) noexcept
        {
            voice.setEnvelopeAttackFilter(envelope.attackMs);
            voice.setEnvelopeDecayFilter(envelope.decayMs);
            voice.setEnvelopeSustainLevelFilter(envelope.sustainLevel);
            voice.setEnvelopeReleaseFilter(envelope.releaseMs);
            voice.setContourF(envelope.contour);
        }

        static void configurePitchEnvelope(Voice& voice, const PitchEnvelopePatch& envelope) noexcept
        {
            voice.setPitchAttack(envelope.attackMs);
            voice.setPitchDecay(envelope.decayMs);
            voice.setPitchEnvelopeDepth(envelope.depthSemitones);
        }

        static void configureLfo(Voice& voice, const LfoPatch& lfo) noexcept
        {
            voice.setLfoWaveForm(lfo.waveform);
            voice.setLfoPitchFactor(lfo.speedHz);
            voice.setLfoFilterModulationDepth(lfo.filterDepth);
            voice.setLfoOscModulationDepth(lfo.oscDepth);
            voice.setLfoKeyFollow(lfo.keyFollow);
        }

        static void configureSlot(Voice& voice, const size_t index, const SlotPatch& slot) noexcept
        {
            voice.setCtrlDimension(index, slot.source);
            voice.setCtrlCurve(index, slot.curve);
            voice.setCtrlTarget(index, slot.target);
            voice.setCtrlType(index, slot.type);
        }

        static void configureVoice(Voice& voice, const MorphPatch& patch, const Settings& settings) noexcept
        {
            for (size_t i = 0; i < patch.oscillators.size(); ++i)
            {
                configureOscillator(voice, i, patch.oscillators[i]);
                configureMorph(voice, i, patch.morphs[i]);
            }
            configureAmplitude(voice, patch.amplitude, settings);
            configureFilterEnvelope(voice, patch.filterEnvelope);
            configurePitchEnvelope(voice, patch.pitchEnvelope);
            configureLfo(voice, patch.lfo);
            voice.setFilterPreset(patch.filter.preset);
            voice.setPresetWaveTable(patch.distortion);
            for (size_t i = 0; i < c_mpeSlotCount; ++i)
            {
                configureSlot(voice, i, patch.slots[i]);
            }
            applyRouting(voice, patch, settings);
        }

        void renderBlock() noexcept
        {
            m_outLeft.fill(0.0f);
            m_outRight.fill(0.0f);
            std::array<float, c_blockSize> voiceLeft;
            std::array<float, c_blockSize> voiceRight;
            for (auto& voice : m_voices)
            {
                if (!voice->isPlaying())
                {
                    continue;
                }
                voice->processBlock(voiceLeft.data(), voiceRight.data(), c_blockSize);
                for (size_t i = 0; i < c_blockSize; ++i)
                {
                    m_outLeft[i] += voiceLeft[i];
                    m_outRight[i] += voiceRight[i];
                }
            }
            for (size_t i = 0; i < c_blockSize; ++i)
            {
                m_outLeft[i] = m_dcBlocker[0].process(m_outLeft[i]);
                m_outRight[i] = m_dcBlocker[1].process(m_outRight[i]);
            }
            m_effects.process(m_outLeft.data(), m_outRight.data());
            for (size_t i = 0; i < c_blockSize; ++i)
            {
                const float gain = m_volume.next();
                m_outLeft[i] *= gain;
                m_outRight[i] *= gain;
            }
            m_position = 0;
        }

        float m_sampleRate;
        WaveTableBank m_bank;
        WaveShaperTableStore m_shaperTables;
        std::array<std::unique_ptr<Voice>, c_voices> m_voices;
        VoiceAllocator<c_voices> m_allocator;
        SustainPedalHandler<c_maxHeldNotes> m_sustain;
        MpeZone m_zone;
        std::array<ChannelState, 16> m_channels{};
        std::array<OnePoleHighpass, 2> m_dcBlocker{};
        Effects m_effects;
        OnePoleSmoother m_volume;
        MorphPatch m_patch{};
        float m_reverbSize{-1.0f};
        float m_reverbDecay{-1.0f};
        std::array<float, c_blockSize> m_outLeft{};
        std::array<float, c_blockSize> m_outRight{};
        size_t m_position{c_blockSize};
    };

    /// The factory patch of the Patch port into the voices (a copy of a value; nothing happens if its script failed).
    void loadFactory()
    {
        const FactoryPatch& factory = m_factory[m_settings.patch];
        if (factory.state == script::ScriptState::Ok)
        {
            m_engine->loadPatch(factory.patch, m_settings);
        }
    }

    /// Runs the ten factory scripts and keeps what they make.
    void loadFactoryPatches()
    {
        FolderModules modules(m_factoryDirectory);
        for (size_t i = 0; i < c_factoryPatchCount; ++i)
        {
            FactoryPatch& slot = m_factory[i];
            slot = FactoryPatch{};
            std::string text;
            if (!script::detail::readTextFile(m_factoryDirectory + "/" + c_factoryPatchNames[i] + ".lua",
                                              c_maxScriptBytes, text))
            {
                slot.state = script::ScriptState::FileMissing;
                continue;
            }
            const PatchScriptResult result =
                evaluatePatchScript(text, c_factoryPatchNames[i], script::Limits{}, 1, &modules, slot.patch);
            slot.state = script::detail::stateOf(result.status);
            slot.patch.name = c_factoryPatchNames[i];
        }
    }

    /// A change of the Script port while the plugin runs.
    void selectSound()
    {
        if (m_settings.script == 0)
        {
            m_script.deselect();
            loadFactory();
        }
        else
        {
            m_script.select(script::Origin::User, c_userScriptNames[m_settings.script - 1]);
        }
    }

    /// The selected sound at activation: not in the audio thread, so a user script is loaded here and now.
    void activateSound()
    {
        if (m_settings.script == 0)
        {
            m_script.deselect();
            m_script.work();
            MorphCommand command;
            uint32_t generation = 0;
            while (m_script.peekCommand(command, generation))
            {
                m_script.discardCommand();
            }
            m_seenGeneration = m_script.readyGeneration();
            loadFactory();
        }
        else
        {
            m_script.select(script::Origin::User, c_userScriptNames[m_settings.script - 1]);
            m_script.work();
            drainScript();
        }
    }

    /// Applies what the script has called since the last time. When another script has taken over (a first load, a
    /// reload), what its top-level code and OnStart called is made into one patch, starting from a blank one, and
    /// loaded at once with all notes released: the sound is that patch, whatever played before. What a hook calls later
    /// is applied one command at a time.
    void drainScript() noexcept
    {
        const uint32_t ready = m_script.readyGeneration();
        if (ready != m_seenGeneration)
        {
            MorphPatch patch;
            MorphCommand command;
            uint32_t generation = 0;
            while (m_script.peekCommand(command, generation) && generation == ready)
            {
                m_script.discardCommand();
                if (command.kind == MorphCommandKind::MpeZone)
                {
                    applyZoneCommand(command.zone);
                }
                else
                {
                    jp::applyCommand(patch, command);
                }
            }
            m_seenGeneration = ready;
            m_engine->loadPatch(patch, m_settings);
        }
        MorphCommand command;
        uint32_t generation = 0;
        while (m_script.peekCommand(command, generation) && generation == m_seenGeneration)
        {
            m_script.discardCommand();
            if (command.kind == MorphCommandKind::MpeZone)
            {
                applyZoneCommand(command.zone);
            }
            else
            {
                m_engine->applyScriptCommand(command, m_settings);
            }
        }
    }

    void applyZoneCommand(const MpeZoneCommand& zone) noexcept
    {
        m_settings.master = zone.master;
        m_settings.lower = zone.lower;
        m_settings.upper = zone.upper;
        m_engine->applyZone(m_settings);
    }

    /// The MIDI the zone lets through, as events for the hooks that the script defines.
    void forwardToScript(const uint8_t* msg) noexcept
    {
        const uint8_t status = msg[0];
        if (status < 0x80 || status >= 0xF0 || !m_engine->zoneAccepts(status & 0x0F))
        {
            return;
        }
        MorphEvent event;
        event.channel = static_cast<uint8_t>(status & 0x0F);
        event.first = msg[1];
        event.second = msg[2];
        switch (status & 0xF0)
        {
            case 0x90:
                if (msg[2] == 0)
                {
                    event.kind = MorphEvent::Kind::NoteOff;
                    event.second = 0x40;
                    break;
                }
                event.kind = MorphEvent::Kind::NoteOn;
                break;
            case 0x80:
                event.kind = MorphEvent::Kind::NoteOff;
                break;
            case 0xB0:
                event.kind = MorphEvent::Kind::ControlChange;
                break;
            case 0xC0:
                event.kind = MorphEvent::Kind::ProgramChange;
                event.second = 0;
                break;
            case 0xE0:
                event.kind = MorphEvent::Kind::PitchBend;
                event.first = static_cast<int16_t>(((msg[2] << 7) | msg[1]) - 8192);
                event.second = 0;
                break;
            default:
                return;
        }
        if (m_script.wantsHook(morphex_script::hookOf(event.kind)))
        {
            m_script.pushEvent(event);
        }
    }

    float m_sampleRate{48000.0f};
    Settings m_settings{};
    std::unique_ptr<Engine> m_engine;
    std::string m_factoryDirectory;
    std::string m_userDirectory;
    std::array<FactoryPatch, c_factoryPatchCount> m_factory{};
    MorphBinder m_binder;
    script::ScriptHost<MorphEvent, MorphCommand, 256, 256> m_script{m_binder};
    uint32_t m_seenGeneration{0};
};

}
