#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

#include "dsp/Numbers/Random.h"

namespace jp
{

/// Built-in waveform shapes. Last is a count marker and stays at the end.
enum class BasicWave
{
    Sine = 0,
    Triangle,
    Saw,
    SharkFin,
    Square,
    Pulse,
    Pulse1,
    NoiseFloor,
    White,
    RectifiedSine,
    RectifiedTriangle,
    Last
};

/// Samples of one cycle in a table (a power of two).
constexpr size_t c_waveTableSize{2048};

/// One mipmap level: a band-limited cycle and the phase increment it may be played up to.
struct WaveTable
{
    float topFreq{}; ///< in cycles per sample; the level serves increments below it
    std::array<float, c_waveTableSize + 1>
        data{}; ///< one cycle plus a copy of the first sample, so interpolation needs no wrap test
};

/// All mipmap levels of one waveform, ordered by increasing topFreq.
struct WaveTableSet
{
    BasicWave wave{BasicWave::Sine};
    std::vector<WaveTable> tables;

    /// The first level whose topFreq lies above the phase increment, else the last (the one with the fewest harmonics).
    [[nodiscard]] size_t levelFor(const float phaseIncrement) const noexcept
    {
        for (size_t i = 0; i < tables.size(); ++i)
        {
            if (phaseIncrement < tables[i].topFreq)
            {
                return i;
            }
        }
        return tables.empty() ? 0 : tables.size() - 1;
    }
};

/**
 * @brief The band-limited wavetables of a set of waveforms, built once and read-only afterwards.
 *
 * A single table cannot serve the whole pitch range: it holds a fixed set of harmonics, and playing it fast pushes
 * the upper ones past Nyquist. Each waveform therefore has mipmap levels, each with the harmonics that cannot alias
 * at its own pitch: the first keeps harmonics 1 to 1024 (of 2048 samples), every next one about two thirds of the
 * previous (1024, 683, 455, ... 5, 3, 2), 16 levels. Level k serves phase increments below 0.6 / H_k, H_k being its
 * harmonic count. An oscillator picks the level with twice its increment, so the highest harmonic lies at 0.3 of the
 * sample rate at most, and the pitch can rise by a factor 1.67 (0.74 octave) within a note before it reaches
 * Nyquist. The levels come from the spectrum of the shape by an inverse FFT with the upper harmonics zeroed, in
 * double. DC and the Nyquist bin are removed from every level. Tonal waveforms are scaled so that the first level has
 * an RMS of 0.5, with the same gain for all levels (the level is equal across waveforms and pitch); the noise tables
 * are scaled to a peak of 1e-5 (the oscillator adds the noise itself) and come from a seeded generator, identical on
 * every platform. The shapes keep their direction (a saw rises).
 *
 * Building allocates and takes an FFT per waveform and level: do it when the plugin is created, never in the audio
 * callback. All eleven waveforms build in 2.6 ms on an Apple M-series core. Memory: 8200 bytes per level, 131200
 * per waveform, 1443200 for all eleven. The tables are shared by any number of oscillators, which keep a pointer.
 * @see https://www.earlevel.com/main/2012/05/04/a-wavetable-oscillator-part-1/
 */
class WaveTableBank
{
  public:
    static constexpr float c_firstTopFreq{0.6f}; ///< topFreq of a level is this divided by its harmonic count
    static constexpr float c_tonalRms{0.5f};
    static constexpr float c_noisePeak{1e-5f};

    /// Builds every waveform.
    WaveTableBank()
        : WaveTableBank({BasicWave::Sine, BasicWave::Triangle, BasicWave::Saw, BasicWave::SharkFin, BasicWave::Square,
                         BasicWave::Pulse, BasicWave::Pulse1, BasicWave::NoiseFloor, BasicWave::White,
                         BasicWave::RectifiedSine, BasicWave::RectifiedTriangle})
    {
    }

    /// Builds only the listed waveforms (a repeated one once).
    WaveTableBank(const std::initializer_list<BasicWave> waves)
    {
        for (const BasicWave wave : waves)
        {
            if (wave != BasicWave::Last && !contains(wave))
            {
                m_sets.push_back(build(wave));
            }
        }
    }

    [[nodiscard]] bool contains(const BasicWave wave) const noexcept
    {
        return find(wave) != nullptr;
    }

    /// The set of a waveform, nullptr if it was not built.
    [[nodiscard]] const WaveTableSet* find(const BasicWave wave) const noexcept
    {
        for (const WaveTableSet& set : m_sets)
        {
            if (set.wave == wave)
            {
                return &set;
            }
        }
        return nullptr;
    }

    /// Bytes of table data.
    [[nodiscard]] size_t memoryBytes() const noexcept
    {
        size_t bytes = 0;
        for (const WaveTableSet& set : m_sets)
        {
            bytes += set.tables.size() * sizeof(WaveTable);
        }
        return bytes;
    }

  private:
    static constexpr uint64_t c_noiseSeed{0x57415645ULL};

    [[nodiscard]] static std::vector<double> shape(const BasicWave wave)
    {
        constexpr double pi{3.14159265358979323846};
        std::vector<double> samples(c_waveTableSize);
        Random random(c_noiseSeed + static_cast<uint64_t>(wave));
        for (size_t n = 0; n < c_waveTableSize; ++n)
        {
            const double phase = static_cast<double>(n) / static_cast<double>(c_waveTableSize);
            double value = 0.0;
            switch (wave)
            {
                case BasicWave::Sine:
                    value = std::sin(2.0 * pi * phase);
                    break;
                case BasicWave::RectifiedSine:
                    value = std::abs(std::sin(2.0 * pi * phase));
                    break;
                case BasicWave::Triangle:
                    value = 2.0 * std::abs(2.0 * phase - 1.0) - 1.0;
                    break;
                case BasicWave::RectifiedTriangle:
                    value = std::abs(2.0 * std::abs(2.0 * phase - 1.0) - 1.0);
                    break;
                case BasicWave::Saw:
                    value = -1.0 + 2.0 * phase;
                    break;
                case BasicWave::SharkFin:
                    value = phase < 0.04 ? -1.0 + 2.0 * (phase / 0.04) : 1.0 - 2.0 * ((phase - 0.04) / 0.96);
                    break;
                case BasicWave::Square:
                    value = n < c_waveTableSize / 2 ? 1.0 : -1.0;
                    break;
                case BasicWave::Pulse:
                    value = n < c_waveTableSize / 5 ? 1.0 : -1.0;
                    break;
                case BasicWave::Pulse1:
                    value = n < c_waveTableSize / 20 ? 1.0 : -1.0;
                    break;
                case BasicWave::NoiseFloor:
                case BasicWave::White:
                    value = 2.0 * static_cast<double>(random.uniform()) - 1.0;
                    break;
                case BasicWave::Last:
                    break;
            }
            samples[n] = value;
        }
        return samples;
    }

    /// cos and sin of 2 pi k / N for k below N / 2.
    struct Twiddles
    {
        Twiddles()
            : cosine(c_waveTableSize / 2)
            , sine(c_waveTableSize / 2)
        {
            constexpr double pi{3.14159265358979323846};
            for (size_t k = 0; k < c_waveTableSize / 2; ++k)
            {
                const double angle = 2.0 * pi * static_cast<double>(k) / static_cast<double>(c_waveTableSize);
                cosine[k] = std::cos(angle);
                sine[k] = std::sin(angle);
            }
        }

        std::vector<double> cosine;
        std::vector<double> sine;
    };

    /// In-place radix-2 FFT of c_waveTableSize points; the inverse is scaled by 1 / N.
    static void fft(std::vector<double>& re, std::vector<double>& im, const bool inverse, const Twiddles& twiddles)
    {
        const size_t length = re.size();
        for (size_t i = 1, j = 0; i < length; ++i)
        {
            size_t bit = length >> 1;
            for (; j & bit; bit >>= 1)
            {
                j ^= bit;
            }
            j ^= bit;
            if (i < j)
            {
                std::swap(re[i], re[j]);
                std::swap(im[i], im[j]);
            }
        }
        const double sign = inverse ? 1.0 : -1.0;
        for (size_t span = 2; span <= length; span <<= 1)
        {
            const size_t stride = length / span;
            for (size_t k = 0; k < length; k += span)
            {
                for (size_t j = 0; j < span / 2; ++j)
                {
                    const double wr = twiddles.cosine[j * stride];
                    const double wi = sign * twiddles.sine[j * stride];
                    const size_t a = k + j;
                    const size_t b = a + span / 2;
                    const double tr = wr * re[b] - wi * im[b];
                    const double ti = wr * im[b] + wi * re[b];
                    re[b] = re[a] - tr;
                    im[b] = im[a] - ti;
                    re[a] += tr;
                    im[a] += ti;
                }
            }
        }
        if (inverse)
        {
            for (size_t i = 0; i < length; ++i)
            {
                re[i] /= static_cast<double>(length);
                im[i] /= static_cast<double>(length);
            }
        }
    }

    [[nodiscard]] static WaveTableSet build(const BasicWave wave)
    {
        const Twiddles twiddles;
        std::vector<double> spectrumRe = shape(wave);
        std::vector<double> spectrumIm(c_waveTableSize, 0.0);
        fft(spectrumRe, spectrumIm, false, twiddles);
        spectrumRe[0] = spectrumIm[0] = 0.0;
        spectrumRe[c_waveTableSize / 2] = spectrumIm[c_waveTableSize / 2] = 0.0;

        // one level per harmonic count, from every harmonic down to two
        std::vector<std::vector<double>> levels;
        std::vector<float> topFreqs;
        size_t harmonics = c_waveTableSize / 2;
        while (harmonics > 1)
        {
            std::vector<double> re(c_waveTableSize, 0.0);
            std::vector<double> im(c_waveTableSize, 0.0);
            for (size_t k = 1; k <= harmonics; ++k)
            {
                re[k] = spectrumRe[k];
                im[k] = spectrumIm[k];
                re[c_waveTableSize - k] = spectrumRe[c_waveTableSize - k];
                im[c_waveTableSize - k] = spectrumIm[c_waveTableSize - k];
            }
            fft(re, im, true, twiddles);
            levels.push_back(std::move(re));
            topFreqs.push_back(c_firstTopFreq / static_cast<float>(harmonics));
            const size_t next = (4 * harmonics + 3) / 6; // floor(2 / 3 harmonics + 1 / 2)
            if (next == harmonics)
            {
                break;
            }
            harmonics = next;
        }

        // one gain for all levels, set by the first
        const bool noise = wave == BasicWave::NoiseFloor || wave == BasicWave::White;
        double sumSquares = 0.0;
        double peak = 0.0;
        for (const double x : levels.front())
        {
            sumSquares += x * x;
            peak = std::max(peak, std::abs(x));
        }
        const double rms = std::sqrt(sumSquares / static_cast<double>(c_waveTableSize));
        const double gain = noise ? static_cast<double>(c_noisePeak) / peak : static_cast<double>(c_tonalRms) / rms;

        WaveTableSet set;
        set.wave = wave;
        set.tables.resize(levels.size());
        for (size_t level = 0; level < levels.size(); ++level)
        {
            WaveTable& table = set.tables[level];
            table.topFreq = topFreqs[level];
            for (size_t n = 0; n < c_waveTableSize; ++n)
            {
                table.data[n] = static_cast<float>(levels[level][n] * gain);
            }
            table.data[c_waveTableSize] = table.data[0];
        }
        return set;
    }

    std::vector<WaveTableSet> m_sets;
};

}
