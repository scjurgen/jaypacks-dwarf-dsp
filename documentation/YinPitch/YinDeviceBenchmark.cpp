// Device benchmark of jp::YinPitchDetector: cost per 128-frame block of the decimation options and of the detector as
// published by AbacDsp (modulo indexing, no early exit), on tones and noise. Not built by CMake: cross-built with the
// device toolchain and run on the Dwarf, see README.md in this folder.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>

#include "dsp/Analysis/YinPitchDetector.h"
#include "dsp/Numbers/Random.h"

// The AbacDsp detector as published (modulo indexing, window 1200 over a 2400 ring, no early exit), C++17.
class Verbatim
{
  public:
    Verbatim(float fs, float minF, float maxF, float hopF)
        : m_fs(fs)
        , m_buf(size_t(4.0f * (fs / minF)), 0.0f)
        , m_hop(size_t(fs / hopF))
        , m_minP(size_t(fs / maxF))
        , m_maxP(size_t(fs / minF))
        , m_diff(m_buf.size() / 2, 0.0f)
        , m_cm(m_buf.size() / 2, 0.0f)
    {
    }
    float step(float in)
    {
        m_buf[m_w] = in;
        m_w = (m_w + 1) % m_buf.size();
        if (++m_cnt >= m_hop)
        {
            m_cnt = 0;
            m_pitch = compute();
        }
        return m_pitch;
    }

  private:
    float compute()
    {
        const size_t n = m_buf.size(), half = n / 2;
        float ss = 0;
        for (size_t i = 0; i < half; ++i)
        {
            const float s = m_buf[(m_w - half + i + n) % n];
            ss += s * s;
        }
        if (std::sqrt(ss / half) <= 1e-6f)
            return 0;
        const size_t maxTau = std::min(m_diff.size(), m_maxP);
        for (size_t tau = 0; tau < maxTau; ++tau)
        {
            float sum = 0;
            for (size_t j = 0; j < half; ++j)
            {
                const float d = m_buf[(m_w + j) % n] - m_buf[(m_w + j + tau) % n];
                sum += d * d;
            }
            m_diff[tau] = sum;
        }
        m_diff[0] = 1.0f;
        m_cm[0] = 1.0f;
        float cum = m_diff[0];
        for (size_t tau = 1; tau < m_cm.size(); ++tau)
        {
            cum += m_diff[tau];
            m_cm[tau] = cum > 1e-10f ? m_diff[tau] * float(tau) / cum : 1.0f;
        }
        const size_t start = std::max(m_minP, size_t{1}), end = std::min(m_cm.size(), m_maxP);
        for (size_t tau = start; tau < end; ++tau)
            if (m_cm[tau] < 0.1f && tau < m_cm.size() - 1 && m_cm[tau] <= m_cm[tau - 1] && m_cm[tau] <= m_cm[tau + 1])
                return m_fs / float(tau);
        const auto it = std::min_element(m_cm.begin() + start, m_cm.begin() + end);
        return *it < 0.8f ? m_fs / float(it - m_cm.begin()) : 0.0f;
    }
    float m_fs;
    std::vector<float> m_buf;
    size_t m_hop, m_minP, m_maxP;
    std::vector<float> m_diff, m_cm;
    size_t m_w{0}, m_cnt{0};
    float m_pitch{0};
};

static std::vector<float> tone(double f, size_t n)
{
    std::vector<float> s(n);
    const double a[] = {1.0, 0.6, 0.4, 0.25, 0.15};
    for (size_t i = 0; i < n; ++i)
    {
        double x = 0;
        for (int h = 0; h < 5; ++h)
            x += a[h] * std::sin(6.283185307179586 * f * (h + 1) * i / 48000.0);
        s[i] = float(0.2 * x);
    }
    return s;
}
static std::vector<float> noise(size_t n)
{
    jp::Random r(5);
    std::vector<float> s(n);
    for (auto& x : s)
        x = 0.3f * (2 * r.uniform() - 1);
    return s;
}

struct Stats
{
    double mean, p50, p99, max;
};

template <typename Det>
static Stats run(Det& d, const std::vector<float>& in, volatile float& sink)
{
    const size_t block = 128, blocks = in.size() / block;
    std::vector<double> t;
    t.reserve(blocks);
    for (size_t b = 0; b < blocks; ++b)
    {
        const auto t0 = std::chrono::steady_clock::now();
        float acc = 0;
        for (size_t i = 0; i < block; ++i)
            acc += d.step(in[b * block + i]);
        const auto t1 = std::chrono::steady_clock::now();
        sink = sink + acc;
        if (b >= 400)
            t.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }
    std::sort(t.begin(), t.end());
    double sum = 0;
    for (double x : t)
        sum += x;
    return {sum / t.size(), t[t.size() / 2], t[size_t(t.size() * 0.99)], t.back()};
}

int main()
{
    volatile float sink = 0;
    const size_t n = 48000 * 8;
    const std::vector<std::pair<const char*, std::vector<float>>> inputs = {
        {"tone 82 Hz", tone(82.4, n)}, {"tone 1 kHz", tone(1000.0, n)}, {"noise", noise(n)}};
    std::printf("%-10s %-22s %5s %9s %9s %9s %9s\n", "input", "variant", "hop", "mean us", "p50 us", "p99 us",
                "max us");
    for (float hop : {50.0f, 20.0f})
        for (const auto& [name, in] : inputs)
        {
            {
                Verbatim d(48000.0f, 80, 1000, hop);
                const Stats s = run(d, in, sink);
                std::printf("%-10s %-22s %5.0f %9.2f %9.2f %9.2f %9.2f\n", name, "verbatim", hop, s.mean, s.p50, s.p99,
                            s.max);
            }
            for (size_t dec : {1, 2, 4})
            {
                jp::YinPitchDetector d(48000.0f, 80, 1000, hop, {dec});
                const Stats s = run(d, in, sink);
                char label[40];
                std::snprintf(label, sizeof label, "decimated by %zu", dec);
                std::printf("%-10s %-22s %5.0f %9.2f %9.2f %9.2f %9.2f\n", name, label, hop, s.mean, s.p50, s.p99,
                            s.max);
            }
        }
}
