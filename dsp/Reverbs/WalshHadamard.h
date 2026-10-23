#pragma once

#include <array>
#include <cstddef>

namespace jp
{

/**
 * @brief Unnormalised Walsh-Hadamard transform in place, natural (Sylvester) order, for N = 2^k.
 *
 * Output i is the sum over j of (-1)^popcount(i & j) * x[j]: the Sylvester Hadamard matrix,
 * whose rows are mutually orthogonal with norm sqrt(N), so H H = N I. Scaling by 1 / sqrt(N)
 * makes it an orthogonal (lossless) feedback matrix; that factor is left to the caller,
 * which usually folds it into a per-line gain.
 *
 * The butterfly takes log2(N) stages of N / 2 add/subtract pairs (N log2 N operations instead
 * of N^2). Plain scalar code on purpose: the inner loops have a constant trip count, so the
 * compiler vectorises them (NEON on aarch64, SSE on x86), and the result is bit-identical on
 * every platform because the order of the additions is fixed.
 * @see https://en.wikipedia.org/wiki/Fast_Walsh%E2%80%93Hadamard_transform
 */
template <size_t N>
inline void walshHadamardInPlace(float* x) noexcept
{
    static_assert(N >= 2 && (N & (N - 1)) == 0, "N must be a power of two");
    for (size_t stride = 1; stride < N; stride *= 2)
    {
        for (size_t start = 0; start < N; start += 2 * stride)
        {
            for (size_t k = start; k < start + stride; ++k)
            {
                const float a = x[k];
                const float b = x[k + stride];
                x[k] = a + b;
                x[k + stride] = a - b;
            }
        }
    }
}

/// walshHadamardInPlace() applied to every column of N rows of Length values at once: each
/// butterfly adds and subtracts two whole rows, so the inner loop runs along a row (contiguous,
/// vectorised) instead of across rows. Every column gets the same additions in the same
/// order as walshHadamardInPlace(), so the results are bit-identical.
template <size_t N, size_t Length>
inline void walshHadamardRowsInPlace(std::array<std::array<float, Length>, N>& rows) noexcept
{
    static_assert(N >= 2 && (N & (N - 1)) == 0, "N must be a power of two");
    for (size_t stride = 1; stride < N; stride *= 2)
    {
        for (size_t start = 0; start < N; start += 2 * stride)
        {
            for (size_t k = start; k < start + stride; ++k)
            {
                auto& a = rows[k];
                auto& b = rows[k + stride];
                for (size_t i = 0; i < Length; ++i)
                {
                    const float x = a[i];
                    const float y = b[i];
                    a[i] = x + y;
                    b[i] = x - y;
                }
            }
        }
    }
}

/// As walshHadamardRowsInPlace(), from source rows into target rows (no copy: the first stage
/// reads the source); bit-identical to copying and transforming in place.
template <size_t N, size_t Length>
inline void walshHadamardRows(const std::array<std::array<float, Length>, N>& source,
                              std::array<std::array<float, Length>, N>& target) noexcept
{
    static_assert(N >= 2 && (N & (N - 1)) == 0, "N must be a power of two");
    for (size_t k = 0; k < N; k += 2)
    {
        for (size_t i = 0; i < Length; ++i)
        {
            const float x = source[k][i];
            const float y = source[k + 1][i];
            target[k][i] = x + y;
            target[k + 1][i] = x - y;
        }
    }
    for (size_t stride = 2; stride < N; stride *= 2)
    {
        for (size_t start = 0; start < N; start += 2 * stride)
        {
            for (size_t k = start; k < start + stride; ++k)
            {
                auto& a = target[k];
                auto& b = target[k + stride];
                for (size_t i = 0; i < Length; ++i)
                {
                    const float x = a[i];
                    const float y = b[i];
                    a[i] = x + y;
                    b[i] = x - y;
                }
            }
        }
    }
}

/// As walshHadamardInPlace(), from input to output (input and output must not overlap).
template <size_t N>
inline void walshHadamard(const float* input, float* output) noexcept
{
    for (size_t i = 0; i < N; ++i)
    {
        output[i] = input[i];
    }
    walshHadamardInPlace<N>(output);
}

}
