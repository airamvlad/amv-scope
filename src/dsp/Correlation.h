#pragma once

// How well two signals line up, from the min/max columns the display already has.
//
// +1: they move together (they add up, more low end)
//  0: unrelated
// -1: they move against each other (they cancel)
//
// Also finds the shift of the second signal that gives the best match, which
// tells you how many milliseconds to move the bass (or kick) to line them up.

#include <cmath>
#include <cstddef>
#include <vector>

namespace amvscope
{

/** Correlation of a and b over the indexes where both have data.
    shift: b is read `shift` samples later (b[i + shift]). */
inline float correlation (const std::vector<float>& a, const std::vector<float>& b,
                          const std::vector<bool>& valid, int shift = 0) noexcept
{
    double sumAB = 0.0, sumAA = 0.0, sumBB = 0.0;
    const auto n = (int) a.size();

    for (int i = 0; i < n; ++i)
    {
        const int j = i + shift;

        if (j < 0 || j >= n || ! valid[(std::size_t) i] || ! valid[(std::size_t) j])
            continue;

        const double x = a[(std::size_t) i], y = b[(std::size_t) j];
        sumAB += x * y;
        sumAA += x * x;
        sumBB += y * y;
    }

    const double denom = std::sqrt (sumAA * sumBB);
    return denom > 1.0e-12 ? (float) (sumAB / denom) : 0.0f;
}

struct BestShift
{
    int shift = 0;            // in samples of the input vectors; positive: move b earlier
    float exactShift = 0.0f;  // the same, refined to a fraction of a sample
    float correlation = 0.0f; // correlation at that shift
    bool inverted = false;    // true: the best match needs b's polarity flipped
};

/** Tries every shift in [-maxShift, maxShift] and returns the one where the
    signals match best, with either polarity. */
inline BestShift findBestShift (const std::vector<float>& a, const std::vector<float>& b,
                                const std::vector<bool>& valid, int maxShift) noexcept
{
    BestShift best;
    float bestScore = -1.0f;

    for (int s = -maxShift; s <= maxShift; ++s)
    {
        const float c = correlation (a, b, valid, s);

        // A steady tone matches again one cycle later; a small penalty for
        // distance picks the nearest of those matches.
        const float score = std::abs (c) - 0.03f * (float) std::abs (s) / (float) (maxShift > 0 ? maxShift : 1);

        if (score > bestScore)
        {
            bestScore = score;
            best = { s, (float) s, c, c < 0.0f };
        }
    }

    // Fit a parabola through the peak and its neighbours to get between samples.
    if (std::abs (best.shift) < maxShift)
    {
        const float sign = best.inverted ? -1.0f : 1.0f;
        const float before = sign * correlation (a, b, valid, best.shift - 1);
        const float at = sign * best.correlation;
        const float after = sign * correlation (a, b, valid, best.shift + 1);
        const float curve = before - 2.0f * at + after;

        if (curve < -1.0e-6f)
        {
            const float frac = 0.5f * (before - after) / curve;

            if (std::abs (frac) <= 0.5f)
                best.exactShift = (float) best.shift + frac;
        }
    }

    return best;
}

} // namespace amvscope
