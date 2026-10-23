#pragma once

#include <algorithm>
#include <cstddef>

namespace jp
{

/*
 * Prime delay lengths for unmodulated delay sets.
 *
 * Delay lines whose lengths share a common factor reinforce each other at that period,
 * piling their modes onto the same frequencies and turning a diffuse tail into an audible
 * pitch. Mutually prime lengths have no common period shorter than their product, so the
 * reinforcement never arrives. Modulated lines do not need this, since the modulation
 * already breaks the coincidence.
 *
 * All functions are bounded, allocation-free and exception-free, so they may run on the
 * audio thread; the cost grows with the length (trial division up to sqrt(n)).
 */

/// Primality by 6k +/- 1 trial division. Exact.
[[nodiscard]] inline bool isPrimeNumber(const size_t n) noexcept
{
    if (n == 2 || n == 3)
    {
        return true;
    }
    if (n <= 1 || n % 2 == 0 || n % 3 == 0)
    {
        return false;
    }
    for (size_t i = 5; i * i <= n; i += 6)
    {
        if (n % i == 0 || n % (i + 2) == 0)
        {
            return false;
        }
    }
    return true;
}

/// Next prime at or above wIn, floored at MinValue. Gives up after 250 odd candidates and
/// returns a non-prime; that bound is only reachable above 387 million samples (a period of
/// over two hours at 48 kHz). Inputs above 10^9 are returned unchanged.
template <size_t MinValue>
[[nodiscard]] inline size_t getUsefulPrime(const size_t wIn) noexcept
{
    static_assert(MinValue >= 3, "the smallest useful prime is 3");
    if (wIn > 1000000000)
    {
        return wIn;
    }
    auto n = std::max(MinValue, wIn) | 1;
    for (size_t m = 0; m < 250; ++m)
    {
        if (isPrimeNumber(n))
        {
            return n;
        }
        n += 2;
    }
    return n;
}

/// Distinct primes for a set of requested sizes, sorted by value. A duplicate request is
/// nudged up to the next free prime, so no two lines share a length. Precondition:
/// numItems > 0 (with 0 nothing is written).
template <size_t MinValue>
inline void generateUniquePrimeSet(const size_t* source, size_t* target, const size_t numItems) noexcept
{
    std::copy(source, source + numItems, target);
    std::sort(target, target + numItems);
    size_t successivePrime = 0;
    for (size_t idx = 0; idx < numItems; ++idx)
    {
        target[idx] = getUsefulPrime<MinValue>(std::max(target[idx], successivePrime));
        successivePrime = target[idx] + 1;
    }
}

/// Sorts the sizes and replaces each duplicate with the next prime above it until all sizes
/// differ. Sizes that are already distinct stay as they are (also when not prime).
inline void ensureUniqueDiscreteSize(size_t* discreteSize, const size_t count) noexcept
{
    bool changesMade = false;
    do
    {
        changesMade = false;
        std::sort(discreteSize, discreteSize + count);
        for (size_t i = 1; i < count; ++i)
        {
            if (discreteSize[i] == discreteSize[i - 1])
            {
                discreteSize[i] = getUsefulPrime<3>(discreteSize[i] + 2);
                changesMade = true;
            }
        }
    } while (changesMade);
}

}
