// Unit tests for the plain C++ DSP core.

#include "dsp/BandSplitter.h"
#include "dsp/PpqRing.h"
#include "dsp/SyncedCapture.h"
#include "dsp/TriggerDetector.h"

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <vector>

namespace
{
int failures = 0;

void check (bool condition, const char* what)
{
    if (! condition)
    {
        std::printf ("FAIL: %s\n", what);
        ++failures;
    }
}

using amvscope::SyncedCapture;

void captureStartsEmpty()
{
    SyncedCapture c;
    bool anyFilled = false;
    for (std::size_t i = 0; i < SyncedCapture::numBins; ++i)
        anyFilled = anyFilled || c.readBin (i).filled;
    check (! anyFilled, "capture starts empty");
}

void capturePlacesSamplesByPosition()
{
    // One quarter note window, 2048 samples per quarter: one sample per bin.
    SyncedCapture c;
    std::vector<float> ramp (SyncedCapture::numBins);
    for (std::size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = (float) i;

    const double step = 1.0 / (double) SyncedCapture::numBins;
    c.write (ramp.data(), ramp.size(), 0.0, step, 1.0);

    check (c.readBin (0).max == 0.0f, "first sample in first bin");
    check (c.readBin (1000).max == 1000.0f, "sample 1000 in bin 1000");
    check (c.readBin (SyncedCapture::numBins - 1).filled, "last bin filled");
}

void captureWrapsWindow()
{
    // Position 5.5 in a 1-quarter window lands in the middle bin.
    SyncedCapture c;
    const float one = 0.7f;
    c.write (&one, 1, 5.5, 0.001, 1.0);
    check (c.readBin (SyncedCapture::numBins / 2).max == 0.7f, "position 5.5 lands mid-window");
    check (c.getPlayhead() > 0.49f && c.getPlayhead() < 0.51f, "playhead at middle");
}

void captureKeepsMinMax()
{
    // Many samples into one bin: min and max are kept.
    SyncedCapture c;
    const float values[] { 0.1f, -0.4f, 0.9f, 0.2f };
    c.write (values, 4, 0.0, 1e-9, 1.0);
    const auto b = c.readBin (0);
    check (b.min == -0.4f && b.max == 0.9f, "bin keeps min and max");
}

void captureNewPassReplaces()
{
    SyncedCapture c;
    const float loud = 0.9f, quiet = 0.1f;
    c.write (&loud, 1, 0.0, 1e-9, 1.0);   // pass 0
    c.write (&quiet, 1, 1.0, 1e-9, 1.0);  // pass 1, same bin
    check (c.readBin (0).max == 0.1f, "next pass replaces the old value");

    c.write (&loud, 1, 0.0, 1e-9, 1.0, 100);  // different passBase, same position
    check (c.readBin (0).max == 0.9f, "pass base separates recordings");
}

void triggerFiresOnRisingLevel()
{
    amvscope::TriggerDetector t;
    t.setThreshold (0.5f);
    t.setHoldoff (0);

    std::vector<float> s (100, 0.0f);
    s[40] = 0.8f;
    check (t.process (s.data(), s.size()) == 40, "fires at the first loud sample");
    check (t.process (s.data(), s.size()) == 40, "re-arms after the signal drops");
}

void triggerRespectsHoldoff()
{
    amvscope::TriggerDetector t;
    t.setThreshold (0.5f);
    t.setHoldoff (100);
    t.reset();

    std::vector<float> s (64, 0.0f);
    s[10] = 0.9f;
    s[50] = 0.9f;
    // Hits at 10 and 50 of each 64-sample block: 10 fires, 50 (40 later) and the
    // next block's 10 (64 later) are inside the holdoff, the next 50 (104 later) fires.
    check (t.process (s.data(), s.size()) == 10, "first hit");
    check (t.process (s.data(), s.size()) == 50, "hits inside holdoff ignored, next one fires");
}

void triggerNeedsSignalToDropFirst()
{
    amvscope::TriggerDetector t;
    t.setThreshold (0.5f);
    t.setHoldoff (0);

    std::vector<float> loud (32, 0.9f);
    check (t.process (loud.data(), loud.size()) == -1, "constant loud signal does not trigger");
}
void ringReadsByPosition()
{
    amvscope::PpqRing ring;
    check (! ring.hasData(), "ring starts empty");

    // Quarter notes 0..2 at 1/1000 quarter per sample: value = position.
    std::vector<float> v (2000);
    for (std::size_t i = 0; i < v.size(); ++i)
        v[i] = (float) i / 1000.0f;
    ring.write (v.data(), nullptr, v.size(), 0.0, 0.001);

    check (ring.hasData(), "ring has data");
    check (std::abs (ring.getHead() - 1.999) < 1e-9, "head is the last position");
    check (std::abs (ring.read (1.5, 1.5005).max - 1.5f) < 0.002f, "reads the right position (fine)");
    check (std::abs (ring.read (1.0, 1.5).max - 1.5f) < 0.002f, "long range uses coarse bins");
    check (! ring.read (5.0, 5.001).filled, "unwritten position is empty");
}

void ringRejectsOtherLaps()
{
    amvscope::PpqRing ring;
    const float x = 0.5f;
    ring.write (&x, nullptr, 1, 3.0, 0.001);

    // 3.0 and 35.0 share a bin (ring is 32 quarters) but are different laps.
    check (ring.read (3.0, 3.0004).filled, "same lap readable");
    check (! ring.read (35.0, 35.0004).filled, "next lap not confused with this one");
    check (! ring.read (-29.0, -28.9996).filled, "previous lap not confused with this one");

    const float y = -0.25f;
    ring.write (&y, nullptr, 1, -1.0, 0.001);   // pre-roll positions work too
    check (ring.read (-1.0, -0.9996).min == -0.25f, "negative position readable");
}

void storeFiltersByPass()
{
    amvscope::BinStore store (4);
    store.add (1, 7, 0.4f, { 0.1f, 0.2f, 0.3f });
    check (store.read (1).filled, "any pass accepted by default");
    check (store.read (1, 7).filled, "matching pass accepted");
    check (! store.read (1, 8).filled, "other pass rejected");
    check (store.read (1).bands.high == 0.3f, "bands stored");
}

void bandsFollowFrequency()
{
    auto measure = [] (double freq)
    {
        amvscope::BandSplitter split;
        split.prepare (48000.0);
        amvscope::Bands peak;
        for (int n = 0; n < 48000; ++n)
        {
            const auto b = split.process ((float) std::sin (2.0 * 3.141592653589793 * freq * n / 48000.0));
            if (n > 24000)
            {
                peak.low = std::max (peak.low, b.low);
                peak.mid = std::max (peak.mid, b.mid);
                peak.high = std::max (peak.high, b.high);
            }
        }
        return peak;
    };

    const auto bass = measure (50.0);
    const auto voice = measure (800.0);
    const auto hat = measure (10000.0);
    check (bass.low > 0.9f && bass.low > 10.0f * bass.high && bass.low > 5.0f * bass.mid, "50 Hz is low");
    check (voice.mid > voice.low && voice.mid > voice.high, "800 Hz is mostly mid");
    check (hat.high > 0.9f && hat.low < 0.01f, "10 kHz is high");
}
} // namespace

int main()
{
    captureStartsEmpty();
    capturePlacesSamplesByPosition();
    captureWrapsWindow();
    captureKeepsMinMax();
    captureNewPassReplaces();
    triggerFiresOnRisingLevel();
    triggerRespectsHoldoff();
    triggerNeedsSignalToDropFirst();
    ringReadsByPosition();
    ringRejectsOtherLaps();
    storeFiltersByPass();
    bandsFollowFrequency();

    if (failures == 0)
        std::printf ("All DSP tests passed.\n");

    return failures == 0 ? 0 : 1;
}
