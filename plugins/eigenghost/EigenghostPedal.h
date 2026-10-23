#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "dsp/BlockProcessors/BlockProcOnePole.h"
#include "dsp/BlockProcessors/BlockProcPitch.h"
#include "dsp/BlockProcessors/BlockProcVibrato.h"
#include "dsp/Delays/ParallelPlainDelay.h"
#include "dsp/Diffuser/SchroederDiffuser.h"
#include "dsp/Numbers/Random.h"
#include "dsp/Parameters/ParameterSmoothing.h"
#include "dsp/Reverbs/FdnTank.h"

namespace jp
{

/**
 * @brief Reverb: a six-stage Schroeder allpass diffuser feeding a 16-line FDN with two
 * pitch-shifting lines; mono in, stereo out.
 *
 * Signal path: input -> pre-delay (whole samples, 0 to 200 ms) -> diffuser (six allpasses
 * between the diffuser size and 3 times it, spread along a bulge of 0.4, gain up to 0.75) ->
 * x 1 / sqrt(16) into every line of the tank -> left and right taps of all lines -> mid/side
 * width -> wet gain, plus the dry input on both outputs. The 1 / sqrt(16) makes
 * the first pass through the 16 lines carry the input's energy; the reverberant part adds
 * with the decay (wet level for noise +0.7, +4.8, +7.8 dB at 0.5, 3, 10 s, default size).
 * An allpass at gain 0 is a pure delay, so at diffusion 0 the diffuser adds the summed stage
 * lengths to the pre-delay (42 ms at the default 1.3 m).
 *
 * Tank lines (index 0 is the shortest): 0 and 1 carry the damping lowpass, 2 and 3 the low
 * cut highpass, 4 and 5 a vibrato (0.5 Hz, 90 degrees apart, peak deviation = mod depth in
 * cents, allpass-interpolated, its nominal delay subtracted from the line so the loop stays
 * exact), 6 and 7 the pitch shifters (pitch 1 and pitch 2, shifted share = pitch amount). Processing on a few lines is
 * enough: the Hadamard feedback spreads every line into all others within a few passes. Line lengths: size (metres, the
 * shortest line) to size x spread, along the bulge curve, at most 0.5 s; size and pre-delay changes crossfade in 50 ms
 * (a pre-delay change during a fade waits for its end, the newest wins).
 *
 * The diffuser and the tank run in blocks of 16 samples behind a FIFO, so the wet signal is
 * 16 samples late and the dry signal is not; the block grid is the pedal's own sample count,
 * so the output does not depend on how the host splits the audio. Smoothing: dry, wet and
 * width per sample (20 ms); decay (in log time), pitch amount, diffusion, damping and low cut
 * (in log frequency) per block (20 ms); the mod depth per block over c_modDepthSmoothingMs
 * (500 ms: a fast change would sweep the read position and bend the pitch); size, spread,
 * bulge, pre-delay and the diffuser size through crossfades; pitch and reverse at once.
 *
 * Dry and wet at c_muteDb mute exactly. The constructor and setSampleRate() (when the rate
 * changes) allocate; process() and the setters are real-time safe. Setters take the plugin
 * port values (seconds, dB, metres, semitones, percent, ms, Hz, cents).
 */
class EigenghostPedal
{
  public:
    static constexpr size_t c_order{16};
    static constexpr size_t c_blockSize{16};
    static constexpr size_t c_diffuserStages{6};
    /// Longest diffuser stage at 48 kHz (50 ms); the allpass buffers scale with the rate.
    static constexpr size_t c_diffuserMax48kHz{2400};
    static constexpr float c_maxLineSeconds{0.5f};
    static constexpr std::array<size_t, 2> c_dampedLines{0, 1};
    static constexpr std::array<size_t, 2> c_lowCutLines{2, 3};
    static constexpr std::array<size_t, 2> c_modulatedLines{4, 5};
    static constexpr std::array<size_t, 2> c_pitchLines{6, 7};
    static constexpr float c_maxDiffusionGain{0.75f};
    /// Longest diffuser stage as a multiple of the shortest, and the bulge between them.
    static constexpr float c_diffuserSpread{3.0f};
    static constexpr float c_diffuserBulge{0.4f};
    static constexpr float c_maxPredelayMs{200.0f};
    static constexpr float c_modulationHz{0.5f};
    static constexpr float c_maxModDepthCents{20.0f};
    static constexpr float c_modDepthSmoothingMs{500.0f};
    /// Pre-delay crossfade, as the tank's size fade.
    static constexpr float c_predelayFadeSeconds{0.05f};
    static constexpr float c_muteDb{-60.0f};
    static constexpr float c_smoothingMs{20.0f};
    static constexpr float c_inputScale{0.25f};

    /// Port defaults (eigenghost.json).
    static constexpr float c_defaultDecay{3.0f};
    static constexpr float c_defaultDry{0.0f};
    static constexpr float c_defaultWet{-6.0f};
    static constexpr float c_defaultSize{12.0f};
    static constexpr float c_defaultPitch1{12.0f};
    static constexpr float c_defaultPitch2{-12.0f};
    static constexpr float c_defaultShimmer{50.0f};
    static constexpr float c_defaultReverse{0.0f};
    static constexpr float c_defaultDiffusion{60.0f};
    static constexpr float c_defaultDiffuserSize{1.3f};
    static constexpr float c_defaultSpread{3.0f};
    static constexpr float c_defaultBulge{0.6f};
    static constexpr float c_defaultPredelay{20.0f};
    static constexpr float c_defaultModDepth{4.0f};
    static constexpr float c_defaultDamping{3000.0f};
    static constexpr float c_defaultLowCut{20.0f};
    static constexpr float c_defaultWidth{100.0f};

    using Tank = FdnTank<c_order, c_blockSize>;

    explicit EigenghostPedal(const uint64_t seed = Random::c_defaultSeed)
        : m_seed(seed)
    {
        for (auto* smoother : {&m_dry, &m_wet, &m_width})
        {
            smoother->setTimeMs(c_smoothingMs);
        }
        for (auto* smoother : {&m_decayLog, &m_shimmer, &m_diffusion, &m_dampingLog, &m_lowCutLog})
        {
            smoother->setTimeMs(c_smoothingMs);
        }
        m_modDepth.setTimeMs(c_modDepthSmoothingMs);
        setDecay(c_defaultDecay);
        setDry(c_defaultDry);
        setWet(c_defaultWet);
        setSize(c_defaultSize);
        setPitch1(c_defaultPitch1);
        setPitch2(c_defaultPitch2);
        setShimmer(c_defaultShimmer);
        setReverse(c_defaultReverse);
        setDiffusion(c_defaultDiffusion);
        setDiffuserSize(c_defaultDiffuserSize);
        setPredelay(c_defaultPredelay);
        setModDepth(c_defaultModDepth);
        setSpread(c_defaultSpread);
        setBulge(c_defaultBulge);
        setDamping(c_defaultDamping);
        setLowCut(c_defaultLowCut);
        setWidth(c_defaultWidth);
        setSampleRate(48000.0f);
    }

    EigenghostPedal(const EigenghostPedal&) = delete;
    EigenghostPedal& operator=(const EigenghostPedal&) = delete;

    /// Builds the diffuser and the tank for the rate (allocates when the rate changes), then
    /// applies every setting and clears the state.
    void setSampleRate(const float sampleRate)
    {
        if (!m_engine || sampleRate != m_sampleRate)
        {
            m_sampleRate = sampleRate;
            m_engine = std::make_unique<Engine>(sampleRate, m_seed);
        }
        for (auto* smoother : {&m_dry, &m_wet, &m_width})
        {
            smoother->setSampleRate(sampleRate);
        }
        for (auto* smoother : {&m_decayLog, &m_shimmer, &m_diffusion, &m_dampingLog, &m_lowCutLog, &m_modDepth})
        {
            smoother->setSampleRate(sampleRate / static_cast<float>(c_blockSize));
        }
        m_engine->tank.setMinSize(m_size);
        m_engine->tank.setMaxSize(m_size * m_spread);
        m_engine->tank.setSpreadBulge(m_bulge);
        for (size_t i = 0; i < 2; ++i)
        {
            m_engine->pitch[i].setPitch(m_pitch[i]);
            m_engine->pitch[i].setReverse(m_reverse);
        }
        const auto fade = static_cast<size_t>(c_predelayFadeSeconds * sampleRate);
        m_predelayFadeSamples = std::max((fade + c_blockSize - 1) / c_blockSize, size_t{1}) * c_blockSize;
        m_diffuserSizePending = true;
        m_predelayPending = true;
        reset();
    }

    /// T60 in seconds.
    void setDecay(const float seconds) noexcept
    {
        m_decayLog.setTarget(std::log2(std::max(seconds, 0.01f)));
    }

    void setDry(const float db) noexcept
    {
        m_dry.setTarget(dbToGainMuted(db));
    }

    void setWet(const float db) noexcept
    {
        m_wet.setTarget(dbToGainMuted(db));
    }

    /// Shortest line in metres.
    void setSize(const float meters) noexcept
    {
        m_size = std::max(meters, 0.01f);
        if (m_engine)
        {
            m_engine->tank.setMinSize(m_size);
            m_engine->tank.setMaxSize(m_size * m_spread);
        }
    }

    void setPitch1(const float semitones) noexcept
    {
        setPitch(0, semitones);
    }

    void setPitch2(const float semitones) noexcept
    {
        setPitch(1, semitones);
    }

    /// Share of the shifted signal in the two pitch lines, percent.
    void setShimmer(const float percent) noexcept
    {
        m_shimmer.setTarget(std::clamp(percent, 0.0f, 100.0f) / 100.0f);
    }

    void setReverse(const float toggle) noexcept
    {
        m_reverse = toggle >= 0.5f;
        if (m_engine)
        {
            for (auto& pitch : m_engine->pitch)
            {
                pitch.setReverse(m_reverse);
            }
        }
    }

    /// Allpass gain in percent of c_maxDiffusionGain.
    void setDiffusion(const float percent) noexcept
    {
        m_diffusion.setTarget(std::clamp(percent, 0.0f, 100.0f) / 100.0f * c_maxDiffusionGain);
    }

    /// Shortest diffuser stage in metres (333.3 m/s, as the tank); the longest is
    /// c_diffuserSpread times longer.
    void setDiffuserSize(const float meters) noexcept
    {
        m_diffuserSizeM = std::max(meters, 0.0f);
        m_diffuserSizePending = true;
    }

    /// Pre-delay before the diffuser in ms (0 to c_maxPredelayMs).
    void setPredelay(const float ms) noexcept
    {
        m_predelayMs = std::clamp(ms, 0.0f, c_maxPredelayMs);
        m_predelayPending = true;
    }

    /// Peak pitch deviation of the modulated lines in cents (0 to c_maxModDepthCents).
    void setModDepth(const float cents) noexcept
    {
        m_modDepth.setTarget(std::clamp(cents, 0.0f, c_maxModDepthCents));
    }

    /// Longest line as a multiple of the size.
    void setSpread(const float factor) noexcept
    {
        m_spread = std::max(factor, 1.0f);
        setSize(m_size);
    }

    void setBulge(const float value) noexcept
    {
        m_bulge = std::clamp(value, -1.0f, 1.0f);
        if (m_engine)
        {
            m_engine->tank.setSpreadBulge(m_bulge);
        }
    }

    /// Cutoff of the lowpass in the damped lines, Hz.
    void setDamping(const float hz) noexcept
    {
        m_dampingLog.setTarget(std::log2(std::max(hz, 1.0f)));
    }

    /// Cutoff of the highpass in the low-cut lines, Hz.
    void setLowCut(const float hz) noexcept
    {
        m_lowCutLog.setTarget(std::log2(std::max(hz, 1.0f)));
    }

    /// Stereo width of the wet signal in percent (100: as the tank's taps, 0: mono).
    void setWidth(const float percent) noexcept
    {
        m_width.setTarget(std::max(percent, 0.0f) / 100.0f);
    }

    /// Clears all signal state, snaps the smoothed settings to their targets and applies
    /// pending size changes at once.
    void reset() noexcept
    {
        for (auto* smoother : {&m_dry, &m_wet, &m_width})
        {
            smoother->snapTo(smoother->getTarget());
        }
        for (auto* smoother : {&m_decayLog, &m_shimmer, &m_diffusion, &m_dampingLog, &m_lowCutLog, &m_modDepth})
        {
            smoother->snapTo(smoother->getTarget());
        }
        applyControlRate(true);
        applyDiffuserSize();
        m_engine->diffuser.snapSizes();
        m_engine->diffuser.clear();
        m_engine->predelay.clear();
        m_engine->predelay.setSize(0, predelaySamples());
        m_predelayPending = false;
        m_engine->tank.reset();
        m_input.fill(0.0f);
        m_wetLeft.fill(0.0f);
        m_wetRight.fill(0.0f);
        m_position = 0;
    }

    void process(const float* const* in, float* const* out, const size_t numSamples) noexcept
    {
        for (size_t i = 0; i < numSamples; ++i)
        {
            const float x = in[0][i];
            const float wetLeft = m_wetLeft[m_position];
            const float wetRight = m_wetRight[m_position];
            m_input[m_position] = x;
            if (++m_position == c_blockSize)
            {
                processBlock();
                m_position = 0;
            }
            const float mid = 0.5f * (wetLeft + wetRight);
            const float side = 0.5f * (wetLeft - wetRight) * m_width.next();
            const float dry = m_dry.next() * x;
            const float wet = m_wet.next();
            out[0][i] = dry + wet * (mid + side);
            out[1][i] = dry + wet * (mid - side);
        }
    }

    /// Wet delay against the dry signal, samples.
    [[nodiscard]] static constexpr size_t getLatency() noexcept
    {
        return c_blockSize;
    }

    [[nodiscard]] const Tank& getTank() const noexcept
    {
        return m_engine->tank;
    }

    [[nodiscard]] size_t getDiffuserStageSize(const size_t index) const noexcept
    {
        return m_engine->diffuser.getStageSize(index);
    }

    /// Pre-delay in samples the delay line is set to (after a running fade).
    [[nodiscard]] size_t getPredelaySamples() const noexcept
    {
        return m_engine->predelay.getSize(0);
    }

    /// Swing of a modulated line's vibrato in samples.
    [[nodiscard]] float getModulationSwing(const size_t index) const noexcept
    {
        return m_engine->vibrato[index].getSwing();
    }

  private:
    struct Engine
    {
        Engine(const float sampleRate, const uint64_t seed)
            : tank(sampleRate, c_maxLineSeconds, seed)
            , predelay(static_cast<size_t>(std::ceil(c_maxPredelayMs * 0.001f * sampleRate)) + c_blockSize)
            , diffuser(sampleRate)
            , pitch{{BlockPitch<c_blockSize>(sampleRate, seed + 1), BlockPitch<c_blockSize>(sampleRate, seed + 2)}}
            , damping{{BlockLowpass<c_blockSize>(sampleRate), BlockLowpass<c_blockSize>(sampleRate)}}
            , lowCut{{BlockHighpass<c_blockSize>(sampleRate), BlockHighpass<c_blockSize>(sampleRate)}}
            , vibrato{{BlockVibrato<c_blockSize>(sampleRate, maxSwingSeconds()),
                       BlockVibrato<c_blockSize>(sampleRate, maxSwingSeconds())}}
        {
            diffuser.setBulge(c_diffuserBulge);
            for (size_t i = 0; i < 2; ++i)
            {
                tank.setDelayCallback(c_dampedLines[i], &damping[i]);
                tank.setDelayCallback(c_lowCutLines[i], &lowCut[i]);
                vibrato[i].setRate(c_modulationHz);
                vibrato[i].setPhase(0.25f * static_cast<float>(i));
                tank.setDelayCallback(c_modulatedLines[i], &vibrato[i], vibrato[i].getLatency());
                tank.setDelayCallback(c_pitchLines[i], &pitch[i]);
            }
        }

        /// Swing that c_maxModDepthCents needs at c_modulationHz, seconds.
        [[nodiscard]] static float maxSwingSeconds() noexcept
        {
            return BlockVibrato<c_blockSize>::swingSamplesFor(c_maxModDepthCents, c_modulationHz, 1.0f);
        }

        Engine(const Engine&) = delete;
        Engine& operator=(const Engine&) = delete;

        Tank tank;
        ParallelPlainDelay<c_blockSize, 1> predelay;
        SchroederDiffuser<c_diffuserMax48kHz, c_diffuserStages, c_blockSize> diffuser;
        std::array<BlockPitch<c_blockSize>, 2> pitch;
        std::array<BlockLowpass<c_blockSize>, 2> damping;
        std::array<BlockHighpass<c_blockSize>, 2> lowCut;
        std::array<BlockVibrato<c_blockSize>, 2> vibrato;
    };

    [[nodiscard]] static float dbToGainMuted(const float db) noexcept
    {
        return db <= c_muteDb ? 0.0f : std::pow(10.0f, db / 20.0f);
    }

    void setPitch(const size_t index, const float semitones) noexcept
    {
        m_pitch[index] = semitones;
        if (m_engine)
        {
            m_engine->pitch[index].setPitch(semitones);
        }
    }

    // per-block parameters: step the smoothers once per block and apply what moved
    void applyControlRate(const bool force) noexcept
    {
        Engine& engine = *m_engine;
        if (force || !m_decayLog.isSettled())
        {
            engine.tank.setDecay(1000.0f * std::exp2(m_decayLog.next()));
        }
        if (force || !m_shimmer.isSettled())
        {
            const float share = m_shimmer.next();
            for (auto& pitch : engine.pitch)
            {
                pitch.setPitchMix(share);
            }
        }
        if (force || !m_diffusion.isSettled())
        {
            engine.diffuser.setFeedback(m_diffusion.next());
        }
        if (force || !m_dampingLog.isSettled())
        {
            const float hz = std::exp2(m_dampingLog.next());
            for (auto& filter : engine.damping)
            {
                filter.setCutoff(hz);
            }
        }
        if (force || !m_lowCutLog.isSettled())
        {
            const float hz = std::exp2(m_lowCutLog.next());
            for (auto& filter : engine.lowCut)
            {
                filter.setCutoff(hz);
            }
        }
        if (force || !m_modDepth.isSettled())
        {
            const float cents = m_modDepth.next();
            for (auto& vibrato : engine.vibrato)
            {
                vibrato.setDepthCents(cents);
            }
        }
    }

    [[nodiscard]] size_t predelaySamples() const noexcept
    {
        return static_cast<size_t>(std::lround(m_predelayMs * 0.001f * m_sampleRate));
    }

    // a new pre-delay crossfades once the running fade (if any) has ended; the newest wins
    void applyPredelay() noexcept
    {
        if (!m_predelayPending || m_engine->predelay.isFading())
        {
            return;
        }
        m_predelayPending = false;
        static_cast<void>(m_engine->predelay.startFade({predelaySamples()}, m_predelayFadeSamples));
    }

    void applyDiffuserSize() noexcept
    {
        if (!m_diffuserSizePending)
        {
            return;
        }
        m_diffuserSizePending = false;
        const float bottom = m_diffuserSizeM / Tank::c_speedOfSound * m_sampleRate;
        const auto bottomSamples = static_cast<size_t>(std::max(bottom, 1.0f));
        const auto topSamples = static_cast<size_t>(std::max(bottom * c_diffuserSpread, 1.0f));
        m_engine->diffuser.setSizes(bottomSamples, topSamples);
    }

    void processBlock() noexcept
    {
        applyControlRate(false);
        applyDiffuserSize();
        applyPredelay();
        Engine& engine = *m_engine;
        m_predelayIn[0] = m_input;
        engine.predelay.processBlock(m_predelayIn, m_predelayOut);
        engine.diffuser.processBlock(m_predelayOut[0].data(), m_diffused.data());
        for (auto& x : m_diffused)
        {
            x *= c_inputScale;
        }
        engine.tank.processBlockSplit(m_diffused.data(), m_wetLeft.data(), m_wetRight.data());
    }

    uint64_t m_seed;
    float m_sampleRate{0.0f};
    std::unique_ptr<Engine> m_engine;

    OnePoleSmoother m_dry;
    OnePoleSmoother m_wet;
    OnePoleSmoother m_width;
    OnePoleSmoother m_decayLog;
    OnePoleSmoother m_shimmer;
    OnePoleSmoother m_diffusion;
    OnePoleSmoother m_dampingLog;
    OnePoleSmoother m_lowCutLog;
    OnePoleSmoother m_modDepth;

    float m_size{c_defaultSize};
    float m_spread{c_defaultSpread};
    float m_bulge{c_defaultBulge};
    float m_diffuserSizeM{c_defaultDiffuserSize};
    bool m_diffuserSizePending{true};
    float m_predelayMs{c_defaultPredelay};
    bool m_predelayPending{true};
    size_t m_predelayFadeSamples{2400};
    bool m_reverse{false};
    std::array<float, 2> m_pitch{c_defaultPitch1, c_defaultPitch2};

    std::array<float, c_blockSize> m_input{};
    std::array<std::array<float, c_blockSize>, 1> m_predelayIn{};
    std::array<std::array<float, c_blockSize>, 1> m_predelayOut{};
    std::array<float, c_blockSize> m_diffused{};
    std::array<float, c_blockSize> m_wetLeft{};
    std::array<float, c_blockSize> m_wetRight{};
    size_t m_position{0};
};

}
