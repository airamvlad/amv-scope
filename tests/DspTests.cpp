// Unit tests for the plain C++ DSP core.

#include "dsp/SyncedCapture.h"
#include "dsp/TriggerDetector.h"

#include <cstdio>
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

    if (failures == 0)
        std::printf ("All DSP tests passed.\n");

    return failures == 0 ? 0 : 1;
}
