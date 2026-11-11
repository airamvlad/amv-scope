#pragma once

// Splits a signal into low (below ~200 Hz), mid and high (above ~2.5 kHz) bands
// and reports each band's level (a short peak envelope, so the colour does not
// flicker at zero crossings), for colouring the waveform by frequency.

#include <cmath>

#include "BinStore.h"

namespace amvscope
{

class BandSplitter
{
public:
    void prepare (double sampleRate) noexcept
    {
        lp200 = make (sampleRate, 200.0, false);
        hp200 = make (sampleRate, 200.0, true);
        lp2500 = make (sampleRate, 2500.0, false);
        hp2500 = make (sampleRate, 2500.0, true);
        decay = (float) std::exp (-1.0 / (0.010 * sampleRate));   // 10 ms release
        reset();
    }

    void reset() noexcept
    {
        for (auto& st : lowState) st = {};
        for (auto& st : midState) st = {};
        for (auto& st : highState) st = {};
        env = {};
    }

    Bands process (float x) noexcept
    {
        // Each band has its own 4th-order (Linkwitz-Riley) filters, so a band
        // does not pick up the phase error of the others.
        const float low = run (lp200, lowState[1], run (lp200, lowState[0], x));
        const float mid = run (hp200, midState[3], run (hp200, midState[2],
                               run (lp2500, midState[1], run (lp2500, midState[0], x))));
        const float high = run (hp2500, highState[1], run (hp2500, highState[0], x));
        follow (env.low, low);
        follow (env.mid, mid);
        follow (env.high, high);
        return env;
    }

private:
    struct Coeffs { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct State { float z1 = 0, z2 = 0; };

    void follow (float& e, float v) const noexcept
    {
        const float a = std::abs (v);
        e = a > e ? a : e * decay;
    }

    static float run (const Coeffs& c, State& s, float x) noexcept
    {
        // Transposed direct form II.
        const float y = c.b0 * x + s.z1;
        s.z1 = c.b1 * x - c.a1 * y + s.z2;
        s.z2 = c.b2 * x - c.a2 * y;
        return y;
    }

    static constexpr double q = 0.7071067811865476;   // Butterworth

    /** Butterworth 2nd-order low or high pass (RBJ cookbook). */
    static Coeffs make (double sr, double f, bool highPass)
    {
        const double w = 2.0 * 3.141592653589793 * f / sr;
        const double alpha = std::sin (w) / (2.0 * q);
        const double cw = std::cos (w);
        const double a0 = 1.0 + alpha;
        const double g = highPass ? (1.0 + cw) * 0.5 : (1.0 - cw) * 0.5;
        const double b1 = highPass ? -(1.0 + cw) : (1.0 - cw);
        return { (float) (g / a0), (float) (b1 / a0), (float) (g / a0),
                 (float) (-2.0 * cw / a0), (float) ((1.0 - alpha) / a0) };
    }

    Coeffs lp200, hp200, lp2500, hp2500;
    State lowState[2], midState[4], highState[2];
    Bands env;
    float decay = 0.99f;
};

} // namespace amvscope
