// Stand-alone stress test for AudioCaptureEngine, meant to be built with -fsanitize=thread
// (cmake -DGHOSTVERB_SANITIZE_THREAD=ON). Audio thread writes, UI thread snapshots and re-reads,
// message thread restarts / changes sample rate. Exit code 0 only if nothing was violated.
#include "AudioCaptureEngine.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace gv;

int main()
{
    AudioCaptureEngine e;
    e.prepare (48000.0);
    e.setLimitSeconds (1.0);
    e.start();

    std::atomic<bool> run { true };
    std::atomic<long> blocks { 0 }, snaps { 0 }, violations { 0 };

    std::thread audio ([&]
    {
        juce::AudioBuffer<float> in (2, 64);
        float c = 0.0f;
        while (run.load())
        {
            for (int i = 0; i < 64; ++i) { in.setSample (0, i, c); in.setSample (1, i, c); c += 1.0f; if (c > 1.0e6f) c = 0.0f; }
            e.process (in);
            ++blocks;
        }
    });

    std::thread ui ([&]
    {
        while (run.load())
        {
            auto s = e.getSnapshot();
            if (! s) continue;
            auto hash = [&] { double h = 0.0; for (int i = 0; i < s.numSamples; ++i) h += (double) s.channel (0)[i] * (1 + (i & 7)) + s.channel (1)[i]; return h; };
            const double h1 = hash();
            std::this_thread::sleep_for (std::chrono::microseconds (300));
            if (hash() != h1) ++violations;
            for (int i = 0; i < s.numSamples; ++i) if (s.channel (0)[i] != s.channel (1)[i]) { ++violations; break; }
            ++snaps;
        }
    });

    const double rates[] = { 44100.0, 48000.0, 96000.0, 22050.0 };
    for (int k = 0; k < 400; ++k)
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (3));
        switch (k % 4)
        {
            case 0: e.start(); break;
            case 1: e.stop(); break;
            case 2: e.prepare (rates[(k / 4) % 4]); break;
            default: e.start(); (void) e.copyRange (0, 1000); break;
        }
    }
    run.store (false);
    audio.join();
    ui.join();

    std::printf ("blocks=%ld snapshots=%ld violations=%ld\n", blocks.load(), snaps.load(), violations.load());
    return (violations.load() == 0 && blocks.load() > 1000 && snaps.load() > 50) ? 0 : 1;
}
