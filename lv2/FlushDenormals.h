#pragma once

#include <cstdint>

#if defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#endif

namespace jp
{

/**
 * @brief Scoped flush-to-zero: while an instance lives, the FPU of the current thread treats
 * denormal floats as zero; the previous mode is restored on destruction.
 *
 * Filter and smoother states that decay towards zero pass through denormal values, which
 * are up to 100 times slower on many CPUs. Setting the mode around run() keeps the plugin
 * fast whatever the host configured, and restoring it leaves the host's mode untouched.
 *
 * aarch64 (Dwarf, Apple Silicon): FPCR.FZ (bit 24), which flushes denormal inputs and
 * results. x86: MXCSR FTZ (bit 15) and DAZ (bit 6). Other targets: no effect.
 * Not copyable; construct and destroy on the same thread.
 */
class ScopedFlushDenormals
{
  public:
    ScopedFlushDenormals() noexcept
    {
#if defined(__aarch64__)
        asm volatile("mrs %0, fpcr" : "=r"(m_saved));
        const uint64_t flushToZero = m_saved | (uint64_t{1} << 24);
        asm volatile("msr fpcr, %0" : : "r"(flushToZero));
#elif defined(__x86_64__) || defined(__i386__)
        m_saved = _mm_getcsr();
        _mm_setcsr(static_cast<unsigned>(m_saved) | 0x8040u);
#endif
    }

    ~ScopedFlushDenormals()
    {
#if defined(__aarch64__)
        asm volatile("msr fpcr, %0" : : "r"(m_saved));
#elif defined(__x86_64__) || defined(__i386__)
        _mm_setcsr(static_cast<unsigned>(m_saved));
#endif
    }

    ScopedFlushDenormals(const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator=(const ScopedFlushDenormals&) = delete;

  private:
    uint64_t m_saved{0};
};

}
