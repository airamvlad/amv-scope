// Unit tests for the plain C++ DSP core.

#include "dsp/History.h"
#include "dsp/SampleFifo.h"

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

void fifoKeepsOrder()
{
    amvscope::SampleFifo<int> fifo (8);
    const int in[] = { 1, 2, 3, 4, 5 };
    check (fifo.push (in, 5) == 5, "fifo accepts 5 items");
    check (fifo.size() == 5, "fifo size is 5");

    int out[8] {};
    check (fifo.pop (out, 3) == 3, "fifo pops 3");
    check (out[0] == 1 && out[1] == 2 && out[2] == 3, "fifo pops in order");
    check (fifo.size() == 2, "fifo has 2 left");
}

void fifoDropsWhenFull()
{
    amvscope::SampleFifo<int> fifo (4);
    const int in[] = { 1, 2, 3, 4, 5, 6 };
    check (fifo.push (in, 6) == 4, "full fifo drops the extra items");

    int out[8] {};
    check (fifo.pop (out, 8) == 4, "fifo returns what it kept");
    check (out[0] == 1 && out[3] == 4, "fifo kept the oldest items");
}

void fifoWrapsAround()
{
    amvscope::SampleFifo<int> fifo (5);
    int out[8] {};
    int next = 0, expected = 0;
    bool ok = true;

    for (int round = 0; round < 100; ++round)
    {
        const int in[] = { next, next + 1, next + 2 };
        next += (int) fifo.push (in, 3);
        const auto n = fifo.pop (out, 2);

        for (std::size_t i = 0; i < n; ++i)
            ok = ok && out[i] == expected++;
    }

    check (ok, "fifo keeps order across wrap-around");
}

void historyReturnsNewestFirst()
{
    amvscope::History history (4);
    std::vector<amvscope::Frame> frames;

    for (int i = 0; i < 6; ++i)
        frames.push_back ({ (float) i, -(float) i });

    history.add (frames.data(), frames.size());

    check (history.fromNewest (0).left == 5.0f, "newest frame is the last added");
    check (history.fromNewest (3).left == 2.0f, "oldest kept frame");
    check (history.fromNewest (0).right == -5.0f, "right channel kept");
}
} // namespace

int main()
{
    fifoKeepsOrder();
    fifoDropsWhenFull();
    fifoWrapsAround();
    historyReturnsNewestFirst();

    if (failures == 0)
        std::printf ("All DSP tests passed.\n");

    return failures == 0 ? 0 : 1;
}
