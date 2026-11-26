// GHOST VERB test-suite: reverse -> reverb -> reverse algorithm, wet-only, duration/sync, channels,
// capture, render thread, WAV export and the processor's real-time behaviour.
#include "PluginProcessor.h"
#include "ReverseReverbEngine.h"
#include "PluginEditor.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <new>
#include <thread>
#include <vector>

//==============================================================================
// Allocation tracker: lets a test prove that processBlock() never allocates.
namespace { thread_local bool gTrackAllocs = false; thread_local int gAllocCount = 0; }
void* operator new (std::size_t n)   { if (gTrackAllocs) ++gAllocCount; if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[] (std::size_t n) { if (gTrackAllocs) ++gAllocCount; if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

using namespace gv;

namespace
{
    juce::AudioBuffer<float> noise (int ch, int n, float amp, int seed = 1)
    {
        juce::Random r (seed);
        juce::AudioBuffer<float> b (ch, n);
        for (int c = 0; c < ch; ++c)
            for (int i = 0; i < n; ++i)
                b.setSample (c, i, (r.nextFloat() * 2.0f - 1.0f) * amp);
        return b;
    }

    float rms (const juce::AudioBuffer<float>& b, int start, int num)
    {
        double acc = 0.0;
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = start; i < start + num; ++i)
                acc += (double) b.getSample (c, i) * b.getSample (c, i);
        return (float) std::sqrt (acc / (double) juce::jmax (1, num * b.getNumChannels()));
    }

    bool allFinite (const juce::AudioBuffer<float>& b)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (c, i))) return false;
        return true;
    }

    float peak (const juce::AudioBuffer<float>& b)
    {
        float p = 0.0f;
        for (int c = 0; c < b.getNumChannels(); ++c) p = juce::jmax (p, b.getMagnitude (c, 0, b.getNumSamples()));
        return p;
    }

    bool waitFor (const std::function<bool()>& cond, int ms)
    {
        const auto t0 = juce::Time::getMillisecondCounter();
        while (! cond())
        {
            if (juce::Time::getMillisecondCounter() - t0 > (juce::uint32) ms) return false;
            juce::Thread::sleep (10);
        }
        return true;
    }

    struct FakePlayHead : juce::AudioPlayHead
    {
        double bpm = 120.0; int num = 4, den = 4; bool provide = true;
        juce::Optional<PositionInfo> getPosition() const override
        {
            if (! provide) return {};
            PositionInfo p;
            p.setBpm (bpm);
            p.setTimeSignature (juce::AudioPlayHead::TimeSignature { num, den });
            return p;
        }
    };
}

//==============================================================================
struct ReverbCoreTests : public juce::UnitTest
{
    ReverbCoreTests() : juce::UnitTest ("Reverb core / reverse chain", "GhostVerb") {}

    void runTest() override
    {
        const double sr = 48000.0;

        beginTest ("FDN reverb is wet-only: exact silence before the first reflection, then stable decaying tail");
        for (float size : { 0.0f, 0.5f, 1.0f })
        {
            FdnReverb rv;
            rv.prepare (sr, size, 0.5f);
            const int silent = rv.guaranteedSilentSamples();
            expect (silent > 100, "pre-delay should be > 100 samples, got " + juce::String (silent));

            const int n = (int) (12.0 * sr);
            juce::AudioBuffer<float> out (2, n);
            for (int i = 0; i < n; ++i)
            {
                float l, r;
                rv.process (i == 0 ? 1.0f : 0.0f, i == 0 ? 1.0f : 0.0f, l, r);
                out.setSample (0, i, l); out.setSample (1, i, r);
            }
            bool zeroPrefix = true;
            for (int i = 0; i < silent; ++i)
                if (out.getSample (0, i) != 0.0f || out.getSample (1, i) != 0.0f) { zeroPrefix = false; break; }
            expect (zeroPrefix, "dry signal leaked into the output (size " + juce::String (size) + ")");
            expect (allFinite (out), "non finite samples");

            const float early = rms (out, (int) (0.05 * sr), (int) (0.2 * sr));
            const int t60 = (int) (rv.rt60Seconds() * sr);
            const float late = (t60 + (int) (0.2 * sr) < n) ? rms (out, t60, (int) (0.2 * sr)) : 0.0f;
            const float db = 20.0f * std::log10 (juce::jmax (1.0e-9f, late) / juce::jmax (1.0e-9f, early));
            logMessage ("  size " + juce::String (size) + ": rt60 target " + juce::String (rv.rt60Seconds(), 2)
                        + " s, pre-delay " + juce::String (1000.0 * silent / sr, 1) + " ms, level at rt60 = " + juce::String (db, 1) + " dB re early");
            expect (db < -35.0f, "tail should have decayed by rt60 (measured " + juce::String (db) + " dB)");
            expect (early > 1.0e-4f, "reverb output too quiet");
        }

        beginTest ("Engine = reverse -> reverb 100% wet -> reverse, bit exact");
        {
            auto sel = noise (2, (int) (0.4 * sr), 0.5f, 7);
            ReverseReverbEngine::Settings s; s.size = 0.4f; s.tone = 0.6f; s.width = 1.0f;
            ReverseReverbEngine::FullRender full;
            expect (ReverseReverbEngine::renderFull (sel, sr, s, 24000, full), "renderFull failed");

            // independent re-implementation of the chain with the public reverb
            const int F = sel.getNumSamples();
            const int T = full.audio.getNumSamples() - F;
            expect (T >= 24000 + full.onsetSamples, "tail must cover LENGTH + pre-delay");
            expectEquals (full.attackIndex, T);

            juce::AudioBuffer<float> rev (2, F);
            rev.makeCopyOf (sel);
            rev.reverse (0, F);
            FdnReverb rv; rv.prepare (sr, s.size, s.tone);
            juce::AudioBuffer<float> manual (2, F + T);
            manual.clear();
            for (int i = 0; i < F + T; ++i)
            {
                float l, r;
                rv.process (i < F ? rev.getSample (0, i) : 0.0f, i < F ? rev.getSample (1, i) : 0.0f, l, r);
                manual.setSample (0, i, l); manual.setSample (1, i, r);
            }
            manual.reverse (0, F + T);

            bool same = true;
            for (int c = 0; c < 2 && same; ++c)
                for (int i = 0; i < F + T; ++i)
                    if (manual.getSample (c, i) != full.audio.getSample (c, i)) { same = false; break; }
            expect (same, "engine output differs from the reference reverse->reverb->reverse chain");
            expect (rms (full.audio, 0, T) > 1.0e-4f, "tail (the swell) must be audible");
        }

        beginTest ("Swell grows towards the attack and ends aligned with it");
        {
            // selection = decaying noise burst starting at the attack (sample 0)
            const int F = (int) (0.5 * sr);
            auto sel = noise (2, F, 0.6f, 3);
            for (int i = 0; i < F; ++i) { const float g = std::exp (-6.0f * (float) i / (float) F); for (int c = 0; c < 2; ++c) sel.setSample (c, i, sel.getSample (c, i) * g); }

            ReverseReverbEngine::Settings s; s.size = 0.5f;
            const int L = (int) (1.0 * sr);
            ReverseReverbEngine::FullRender full;
            expect (ReverseReverbEngine::renderFull (sel, sr, s, L, full), "renderFull failed");
            auto swell = ReverseReverbEngine::extractSwell (full, L, sr, 0.0f);

            const int q = L / 4;
            const float first = rms (swell, 0, q), last = rms (swell, L - q, q);
            logMessage ("  rms first quarter " + juce::String (first, 5) + ", last quarter " + juce::String (last, 5)
                        + " (" + juce::String (20.0f * std::log10 (last / juce::jmax (1.0e-9f, first)), 1) + " dB crescendo)");
            expect (last > first * 3.0f, "no crescendo towards the attack");

            // loudest 10 ms window must sit in the last 15 % of the file (i.e. at the attack)
            const int win = (int) (0.01 * sr);
            int bestPos = 0; float best = 0.0f;
            for (int i = 0; i + win < L; i += win / 2)
            { const float v = rms (swell, i, win); if (v > best) { best = v; bestPos = i; } }
            logMessage ("  loudest window at " + juce::String (100.0 * bestPos / L, 1) + " % of the file");
            expect (bestPos > (int) (0.85 * L), "swell peak is not aligned with the attack");

            // the audio of the selection itself must not appear (no dry): correlate with the reversed selection
            double dot = 0.0, e1 = 0.0, e2 = 0.0;
            for (int i = 0; i < F; ++i)
            {
                const float a = swell.getSample (0, L - F + i);                         // last F samples of the swell
                const float b = sel.getSample (0, F - 1 - i);                           // dry, reversed
                dot += a * b; e1 += a * a; e2 += b * b;
            }
            const double corr = std::abs (dot) / std::sqrt (juce::jmax (1.0e-30, e1 * e2));
            logMessage ("  |correlation| swell vs dry material: " + juce::String (corr, 4));
            expect (corr < 0.1, "dry signal present in the wet output");
        }

        beginTest ("Exact musical length for every LENGTH / tempo / time signature");
        {
            struct Case { int idx; double bpm; int n, d; double sec; };
            const Case cases[] = { { 2, 120, 4, 4, 2.0 },  { 1, 120, 4, 4, 1.0 },  { 0, 120, 4, 4, 0.5 },
                                   { 2, 90, 3, 4, 2.0 },   { 2, 90, 6, 8, 2.0 },   { 1, 140, 7, 8, 7 * 0.5 * 60.0 / 140.0 * 0.5 },
                                   { 0, 174, 4, 4, 4 * 60.0 / 174.0 * 0.25 } };
            for (double rate : { 44100.0, 48000.0, 96000.0 })
                for (auto& c : cases)
                {
                    const int expected = (int) std::llround (c.sec * rate);
                    expectEquals (lengthInSamples (c.idx, c.bpm, c.n, c.d, rate), expected);

                    auto sel = noise (2, (int) (0.2 * rate), 0.3f);
                    ReverseReverbEngine::FullRender full;
                    ReverseReverbEngine::Settings s;
                    ReverseReverbEngine::renderFull (sel, rate, s, expected, full);
                    auto swell = ReverseReverbEngine::extractSwell (full, expected, rate, 0.0f);
                    expectEquals (swell.getNumSamples(), expected);
                    expectEquals (swell.getNumChannels(), 2);
                }
            expectEquals (lengthInSamples (2, 0.0, 0, 0, 48000.0), 96000);       // fallback 120 BPM 4/4
            expectEquals (lengthInSamples (2, std::nan (""), 4, 4, 48000.0), 96000);
        }

        beginTest ("Channels: mono input -> stereo wet, WIDTH 0 -> mono, stereo stays stereo");
        {
            auto mono = noise (1, (int) (0.3 * sr), 0.4f);
            ReverseReverbEngine::Settings s;
            ReverseReverbEngine::FullRender full;
            expect (ReverseReverbEngine::renderFull (mono, sr, s, 20000, full), "mono render failed");
            expectEquals (full.audio.getNumChannels(), 2);
            auto swell = ReverseReverbEngine::extractSwell (full, 20000, sr, 0.0f);
            double diff = 0.0; for (int i = 0; i < 20000; ++i) diff += std::abs (swell.getSample (0, i) - swell.getSample (1, i));
            expect (diff > 1.0e-3, "stereo reverb should decorrelate L/R");

            s.width = 0.0f;
            expect (ReverseReverbEngine::renderFull (mono, sr, s, 20000, full), "width0 render failed");
            swell = ReverseReverbEngine::extractSwell (full, 20000, sr, 0.0f);
            double d0 = 0.0; for (int i = 0; i < 20000; ++i) d0 += std::abs (swell.getSample (0, i) - swell.getSample (1, i));
            expect (d0 < 1.0e-4, "WIDTH 0 must be mono");
        }

        beginTest ("No clipping even with full-scale input and +12 dB output; no NaN; abort works");
        {
            auto loud = noise (2, (int) (1.0 * sr), 1.0f);
            ReverseReverbEngine::Settings s; s.size = 1.0f; s.outputDb = 12.0f; s.width = 2.0f;
            ReverseReverbEngine::FullRender full;
            expect (ReverseReverbEngine::renderFull (loud, sr, s, 48000, full), "render failed");
            auto swell = ReverseReverbEngine::extractSwell (full, 48000, sr, s.outputDb);
            expect (peak (swell) <= 0.98001f, "clipping: peak " + juce::String (peak (swell)));
            expect (allFinite (swell), "NaN/inf");

            ReverseReverbEngine::FullRender f2;
            expect (! ReverseReverbEngine::renderFull (loud, sr, s, 48000, f2, [] { return true; }), "abort ignored");
        }

        beginTest ("Sample rates and tiny / odd selections are handled");
        for (double rate : { 22050.0, 44100.0, 88200.0, 192000.0 })
        {
            auto sel = noise (2, (int) (0.05 * rate) + 1, 0.5f);
            ReverseReverbEngine::FullRender full;
            ReverseReverbEngine::Settings s;
            const int L = lengthInSamples (0, 120, 4, 4, rate);
            expect (ReverseReverbEngine::renderFull (sel, rate, s, L, full), "render failed at " + juce::String (rate));
            auto sw = ReverseReverbEngine::extractSwell (full, L, rate, 0.0f);
            expect (allFinite (sw) && sw.getNumSamples() == L, "bad swell at " + juce::String (rate));
        }
    }
};

//==============================================================================
struct CaptureTests : public juce::UnitTest
{
    CaptureTests() : juce::UnitTest ("Capture engine", "GhostVerb") {}

    void runTest() override
    {
        beginTest ("Records stereo/mono, never alters the input, auto-stops at the limit");
        AudioCaptureEngine cap;
        expect (! cap.start(), "start before prepare must fail");
        cap.prepare (48000.0);
        cap.setLimitSeconds (1.0);
        expectEquals (cap.getLimitSamples(), 48000);

        expect (cap.start(), "start failed");
        auto in = noise (2, 512, 0.5f, 9);
        auto copy = in;
        cap.process (in);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i) expectEquals (in.getSample (c, i), copy.getSample (c, i));
        expectEquals (cap.getNumCaptured(), 512);
        {
            auto snap = cap.getSnapshot();
            expectEquals (snap.numSamples, 512);
            for (int i = 0; i < 512; ++i) expectEquals (snap.channel (1)[i], copy.getSample (1, i));
        }

        auto mono = noise (1, 512, 0.5f, 4);
        cap.process (mono);
        {
            auto snap = cap.getSnapshot();
            for (int i = 0; i < 512; ++i)
            {
                expectEquals (snap.channel (0)[512 + i], mono.getSample (0, i));
                expectEquals (snap.channel (1)[512 + i], mono.getSample (0, i));    // mono duplicated
            }
        }

        cap.stop();
        expect (cap.getState() == AudioCaptureEngine::State::Ready, "state after stop");
        expectEquals (cap.getNumCaptured(), 1024);
        cap.process (in);                                    // ignored when not capturing
        expectEquals (cap.getNumCaptured(), 1024);

        expect (cap.start(), "restart failed");
        for (int i = 0; i < 200; ++i) cap.process (in);      // 200 * 512 > 48000
        expect (cap.getState() == AudioCaptureEngine::State::Ready, "no auto stop");
        expectEquals (cap.getNumCaptured(), 48000);

        auto part = cap.copyRange (100, 300);
        expectEquals (part.getNumSamples(), 300);
        expectEquals (part.getNumChannels(), 2);

        beginTest ("Snapshots are immutable: a snapshot taken earlier never changes after new captures / sample-rate changes");
        {
            AudioCaptureEngine e;
            e.prepare (44100.0);
            e.setLimitSeconds (1.0);
            e.start();
            auto ramp = juce::AudioBuffer<float> (2, 256);
            for (int i = 0; i < 256; ++i) { ramp.setSample (0, i, (float) i); ramp.setSample (1, i, (float) -i); }
            e.process (ramp);
            auto old = e.getSnapshot();
            expectEquals (old.numSamples, 256);

            e.stop(); e.start();                                   // new capture (previously overwrote the same memory)
            auto other = noise (2, 256, 0.9f, 77);
            for (int k = 0; k < 20; ++k) e.process (other);
            e.prepare (96000.0);                                   // sample-rate change reallocates
            e.start();
            for (int k = 0; k < 20; ++k) e.process (other);

            bool intact = old.numSamples == 256 && old.sampleRate == 44100.0;
            for (int i = 0; i < 256; ++i) intact &= old.channel (0)[i] == (float) i && old.channel (1)[i] == (float) -i;
            expect (intact, "an old snapshot was modified or invalidated");
            expectEquals (e.getSampleRate(), 96000.0);
        }

        beginTest ("Stress: audio thread writes while the UI snapshots and the message thread restarts / changes sample rate");
        {
            AudioCaptureEngine e;
            e.prepare (48000.0);
            e.setLimitSeconds (2.0);
            e.start();
            std::atomic<bool> run { true };
            std::atomic<int> audioBlocks { 0 }, snapshots { 0 }, violations { 0 };

            std::thread audio ([&]
            {
                juce::AudioBuffer<float> in (2, 128);
                float counter = 0.0f;
                while (run.load())
                {
                    for (int i = 0; i < 128; ++i) { in.setSample (0, i, counter); in.setSample (1, i, counter); counter += 1.0f; if (counter > 1.0e6f) counter = 0.0f; }
                    e.process (in);
                    ++audioBlocks;
                    std::this_thread::sleep_for (std::chrono::microseconds (50));
                }
            });

            std::thread ui ([&]
            {
                while (run.load())
                {
                    auto snap = e.getSnapshot();
                    if (snap)
                    {
                        // hash the immutable region, wait while the audio thread keeps working, hash again
                        auto hash = [&]
                        {
                            double h = 0.0;
                            for (int i = 0; i < snap.numSamples; ++i) h += (double) snap.channel (0)[i] * (1 + (i & 7)) + (double) snap.channel (1)[i];
                            return h;
                        };
                        const double h1 = hash();
                        std::this_thread::sleep_for (std::chrono::milliseconds (1));
                        if (hash() != h1) ++violations;
                        for (int i = 0; i < snap.numSamples; ++i) if (snap.channel (0)[i] != snap.channel (1)[i]) { ++violations; break; }
                        ++snapshots;
                    }
                }
            });

            const double rates[] = { 44100.0, 48000.0, 96000.0, 22050.0 };
            for (int k = 0; k < 120; ++k)
            {
                juce::Thread::sleep (8);
                switch (k % 4)
                {
                    case 0: e.start(); break;
                    case 1: e.stop(); break;
                    case 2: e.prepare (rates[(k / 4) % 4]); break;
                    default: e.start(); break;
                }
            }
            run.store (false);
            audio.join(); ui.join();
            logMessage ("  stress: " + juce::String (audioBlocks.load()) + " audio blocks, " + juce::String (snapshots.load()) + " snapshots, "
                        + juce::String (violations.load()) + " violations");
            expect (audioBlocks.load() > 100 && snapshots.load() > 20, "stress test did not exercise both threads");
            expectEquals (violations.load(), 0);
        }

        beginTest ("Interrupted capture / empty states");
        expect (cap.start(), "start");
        cap.stop();
        expect (cap.getState() == AudioCaptureEngine::State::Idle, "stop with nothing recorded goes back to idle");
        expectEquals (cap.copyRange (0, 100).getNumSamples(), 0);
    }
};

//==============================================================================
struct RendererTests : public juce::UnitTest
{
    RendererTests() : juce::UnitTest ("Render thread + WAV export", "GhostVerb") {}

    void runTest() override
    {
        const double sr = 48000.0;
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ghostverb_tests");
        tmp.deleteRecursively();

        beginTest ("24-bit stereo WAV round trip");
        {
            auto audio = noise (2, 10000, 0.7f, 5);
            auto f = tmp.getChildFile ("roundtrip.wav");
            juce::String err;
            expect (AudioRenderer::writeWav24 (audio, sr, f, err), "write failed: " + err);
            juce::AudioFormatManager fm; fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
            expect (rd != nullptr, "cannot read back");
            if (rd != nullptr)
            {
                expectEquals ((int) rd->numChannels, 2);
                expectEquals ((int) rd->bitsPerSample, 24);
                expectEquals ((int) rd->lengthInSamples, 10000);
                expectEquals (rd->sampleRate, sr);
                juce::AudioBuffer<float> back (2, 10000);
                rd->read (&back, 0, 10000, 0, true, true);
                float maxErr = 0.0f;
                for (int c = 0; c < 2; ++c) for (int i = 0; i < 10000; ++i) maxErr = juce::jmax (maxErr, std::abs (back.getSample (c, i) - audio.getSample (c, i)));
                logMessage ("  max 24-bit quantisation error " + juce::String (maxErr, 9));
                expect (maxErr < 1.0e-6f, "round-trip error too large");
            }

            juce::String err2;
            expect (! AudioRenderer::writeWav24 (juce::AudioBuffer<float>(), sr, f, err2) && err2.isNotEmpty(), "empty export must fail with a message");
            juce::String err3;
            auto blocker = tmp.getChildFile ("blocker.txt");           // a FILE where a folder is needed: fails on every OS
            blocker.replaceWithText ("x");
            expect (! AudioRenderer::writeWav24 (audio, sr, blocker.getChildFile ("x.wav"), err3) && err3.isNotEmpty(), "bad path must fail with a message");
        }

        beginTest ("Render runs on its own thread: submit() returns immediately, result is wet, exact length, file written");
        {
            AudioRenderer r;
            AudioRenderer::Job job;
            job.selection = noise (2, (int) (2.0 * sr), 0.5f);
            job.sampleRate = sr;
            job.settings.size = 0.8f;
            job.lengthSamples = (int) sr;
            job.outputFile = tmp.getChildFile ("job.wav");

            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            r.submit (std::move (job));
            const auto dt = juce::Time::getMillisecondCounterHiRes() - t0;
            logMessage ("  submit() took " + juce::String (dt, 2) + " ms");
            expect (dt < 50.0, "submit blocked the caller");
            expect (r.getStatus() == AudioRenderer::Status::Rendering || r.getStatus() == AudioRenderer::Status::Ready, "status");

            expect (waitFor ([&] { return r.getStatus() == AudioRenderer::Status::Ready; }, 60000), "render did not finish");
            auto out = r.getOutput();
            expect (out != nullptr, "no output");
            if (out != nullptr)
            {
                expectEquals (out->audio.getNumSamples(), (int) sr);
                expectEquals (out->audio.getNumChannels(), 2);
                expect (out->file.existsAsFile(), "temp WAV missing");
                expect (out->fileError.isEmpty(), "file error: " + out->fileError);
                expect (rms (out->audio, 0, out->audio.getNumSamples()) > 1.0e-4f, "silent result");
            }

            // supersede: a second job replaces a running one
            AudioRenderer::Job a, b;
            a.selection = noise (2, (int) (30.0 * sr), 0.5f); a.sampleRate = sr; a.lengthSamples = 48000; a.settings.size = 1.0f;
            a.outputFile = tmp.getChildFile ("cancelled.wav");
            b.selection = noise (2, (int) (0.3 * sr), 0.5f); b.sampleRate = sr; b.lengthSamples = 24000;
            b.outputFile = tmp.getChildFile ("winner.wav");
            r.submit (std::move (a));
            juce::Thread::sleep (150);                                                // job A is now running
            const auto c0 = juce::Time::getMillisecondCounterHiRes();
            r.submit (std::move (b));
            expect (waitFor ([&] { return r.getStatus() == AudioRenderer::Status::Ready; }, 60000), "superseding render did not finish");
            const auto took = juce::Time::getMillisecondCounterHiRes() - c0;
            logMessage ("  superseding job completed " + juce::String (took, 0) + " ms after being submitted");
            expectEquals (r.getOutput()->audio.getNumSamples(), 24000);
            expect (! tmp.getChildFile ("cancelled.wav").existsAsFile(), "the cancelled render still wrote its file");
            expect (tmp.getChildFile ("winner.wav").existsAsFile(), "winning render missing its file");

            AudioRenderer::Job bad; bad.selection = noise (2, 1000, 0.1f); bad.sampleRate = sr; bad.lengthSamples = 0;
            r.submit (std::move (bad));
            expect (waitFor ([&] { return r.getStatus() == AudioRenderer::Status::Failed; }, 5000), "invalid job must fail cleanly");
            expect (r.getError().isNotEmpty(), "failure needs a message");
        }
        tmp.deleteRecursively();
    }
};

//==============================================================================
struct ProcessorTests : public juce::UnitTest
{
    ProcessorTests() : juce::UnitTest ("Processor: passthrough, tempo, capture -> generate -> preview", "GhostVerb") {}

    void runTest() override
    {
        const double sr = 48000.0;
        const int blk = 512;
        beginTest ("Allocation tracker really detects allocations (so the zero-alloc checks below are meaningful)");
        {
            gTrackAllocs = true; gAllocCount = 0;
            auto* leak = new int[64]; leak[0] = 1;
            auto* leak2 = new double[16]; leak2[0] = 2.0;
            static void* volatile sink; sink = leak; sink = leak2;           // the optimiser may not drop the allocations
            const int seen = gAllocCount; gTrackAllocs = false;
            delete[] leak; delete[] leak2;
            expect (seen >= 2, "tracker missed allocations: " + juce::String (seen));
        }

        GhostVerbAudioProcessor p;
        p.setPlayConfigDetails (2, 2, sr, blk);
        p.prepareToPlay (sr, blk);
        juce::MidiBuffer midi;

        beginTest ("Layouts: mono and stereo supported, mismatches rejected");
        {
            auto make = [] (const juce::AudioChannelSet& in, const juce::AudioChannelSet& out)
            { juce::AudioProcessor::BusesLayout l; l.inputBuses.add (in); l.outputBuses.add (out); return l; };
            expect (p.checkBusesLayoutSupported (make (juce::AudioChannelSet::stereo(), juce::AudioChannelSet::stereo())));
            expect (p.checkBusesLayoutSupported (make (juce::AudioChannelSet::mono(), juce::AudioChannelSet::mono())));
            expect (! p.checkBusesLayoutSupported (make (juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo())));
            expect (! p.checkBusesLayoutSupported (make (juce::AudioChannelSet::create5point1(), juce::AudioChannelSet::create5point1())));
        }

        beginTest ("Tempo / time signature are read from the playhead inside processBlock (fallback 120 BPM 4/4)");
        FakePlayHead ph;
        p.setPlayHead (&ph);
        {
            auto h0 = p.getHostInfo();
            expect (h0.bpm == 120.0 && h0.numerator == 4 && h0.denominator == 4 && ! h0.tempoFromHost, "fallback");
            ph.bpm = 90.0; ph.num = 6; ph.den = 8;
            auto b = noise (2, blk, 0.2f);
            p.processBlock (b, midi);
            auto h = p.getHostInfo();
            expect (h.bpm == 90.0 && h.numerator == 6 && h.denominator == 8 && h.tempoFromHost, "host info not updated");
        }

        beginTest ("Passthrough is bit-exact while idle and while capturing; no allocations in processBlock");
        {
            expect (p.startCapture(), "startCapture");
            bool exact = true;
            gTrackAllocs = true; gAllocCount = 0;
            double worst = 0.0;
            for (int blkIdx = 0; blkIdx < 400; ++blkIdx)         // ~4.3 s
            {
                auto b = noise (2, blk, 0.5f, 100 + blkIdx % 7);
                auto ref = b;
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                p.processBlock (b, midi);
                worst = juce::jmax (worst, juce::Time::getMillisecondCounterHiRes() - t0);
                for (int c = 0; c < 2 && exact; ++c)
                    for (int i = 0; i < blk; ++i) if (b.getSample (c, i) != ref.getSample (c, i)) { exact = false; break; }
            }
            const int allocs = gAllocCount;
            gTrackAllocs = false;
            expect (exact, "input was altered by the plugin");
            expectEquals (allocs, 0);
            logMessage ("  processBlock worst case while capturing: " + juce::String (worst, 3) + " ms (blocks of 512 @48k = 10.7 ms budget)");
            expect (p.capture.getNumCaptured() >= 400 * blk - blk, "capture lost audio");
            p.stopCapture();
            expect (p.capture.getState() == AudioCaptureEngine::State::Ready, "capture state");
        }

        beginTest ("Generate (non-blocking) -> exact musical length -> preview mixes wet audio without allocations");
        {
            int a, b; p.getSelection (a, b);
            expect (b > a, "default selection missing");
            p.setSelection (24000, 24000 + (int) (3.0 * sr));
            p.apvts.getParameter (ids::size)->setValueNotifyingHost (1.0f);                 // longest tail: heavier render
            p.apvts.getParameter (ids::length)->setValueNotifyingHost (1.0f);        // 1 BAR  (6/8 @ 90 BPM = 2.0 s)
            const auto g0 = juce::Time::getMillisecondCounterHiRes();
            const auto err = p.generate();
            const auto gdt = juce::Time::getMillisecondCounterHiRes() - g0;
            expect (err.isEmpty(), "generate: " + err);
            logMessage ("  generate() returned after " + juce::String (gdt, 2) + " ms");

            // audio keeps flowing while the render thread works
            double worst = 0.0; bool exact = true; int iterations = 0;
            gTrackAllocs = true; gAllocCount = 0;
            while (p.renderer.getStatus() == AudioRenderer::Status::Rendering)
            {
                ++iterations;
                auto blkBuf = noise (2, blk, 0.3f);
                auto ref = blkBuf;
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                p.processBlock (blkBuf, midi);
                worst = juce::jmax (worst, juce::Time::getMillisecondCounterHiRes() - t0);
                for (int i = 0; i < blk; ++i) if (blkBuf.getSample (0, i) != ref.getSample (0, i)) exact = false;
            }
            const int allocs = gAllocCount; gTrackAllocs = false;
            logMessage ("  " + juce::String (iterations) + " audio blocks processed while the render was running");
            expect (iterations >= 10, "render finished too quickly to prove anything (" + juce::String (iterations) + " blocks)");
            expect (exact, "audio altered while rendering");
            expectEquals (allocs, 0);
            logMessage ("  processBlock worst case during render: " + juce::String (worst, 3) + " ms");
            expect (worst < 25.0, "processBlock stalled during render");

            expect (p.renderer.getStatus() == AudioRenderer::Status::Ready, "render status: " + p.renderer.getError());
            auto out = p.renderer.getOutput();
            expect (out != nullptr, "no output");
            if (out != nullptr)
            {
                expectEquals (out->audio.getNumSamples(), 96000);            // 6 * (4/8) * 60/90 = 2.0 s
                expectEquals (out->audio.getNumChannels(), 2);
                expect (out->file.existsAsFile(), "temp file for drag & drop missing");
                expectEquals (out->numerator, 6); expectEquals (out->denominator, 8);
                expect (out->bpm == 90.0, "bpm stored");
            }

            expect (p.previewResult(), "previewResult");
            auto silent = juce::AudioBuffer<float> (2, blk); silent.clear();
            bool heard = false, finite = true;
            gTrackAllocs = true; gAllocCount = 0;
            for (int i = 0; i < 20; ++i)
            {
                auto x = silent;
                p.processBlock (x, midi);
                heard |= peak (x) > 1.0e-4f; finite &= allFinite (x);
            }
            const int pa = gAllocCount; gTrackAllocs = false;
            expect (heard, "preview produced no audio");
            expect (finite, "preview not finite");
            expectEquals (pa, 0);
            p.stopPreview();
            for (int i = 0; i < 4; ++i) { auto x = silent; p.processBlock (x, midi); }
            expect (p.getPreviewKind() == GhostVerbAudioProcessor::PreviewKind::None, "preview did not stop");

            expect (p.previewSelection(), "previewSelection");
            auto x = silent; p.processBlock (x, midi);
            expect (peak (x) > 1.0e-4f, "selection preview silent");
            p.stopPreview();
        }

        beginTest ("Guard rails: empty capture, short selection, capturing state");
        {
            GhostVerbAudioProcessor q;
            q.prepareToPlay (sr, blk);
            expect (q.generate().isNotEmpty(), "generate with no capture must report an error");
            expect (! q.previewResult() && ! q.previewSelection(), "previews with nothing must be refused");
            q.startCapture();
            expect (q.generate().isNotEmpty(), "generate while capturing must be refused");
            auto b = noise (2, blk, 0.2f);
            for (int i = 0; i < 20; ++i) q.processBlock (b, midi);
            q.stopCapture();
            int s, e; q.getSelection (s, e);
            q.setSelection (s, s + 100);
            expect (q.generate().isNotEmpty(), "too-short selection must be refused");
        }

        beginTest ("State round trip (APVTS + capture limit)");
        {
            p.apvts.getParameter (ids::size)->setValueNotifyingHost (0.9f);
            p.setCaptureLimitSeconds (10.0);
            juce::MemoryBlock mb; p.getStateInformation (mb);
            GhostVerbAudioProcessor q; q.prepareToPlay (sr, blk);
            q.setStateInformation (mb.getData(), (int) mb.getSize());
            expectWithinAbsoluteError (q.apvts.getRawParameterValue (ids::size)->load(), 0.9f, 0.01f);
            expectEquals (q.capture.getLimitSeconds(), 10.0);
        }
        p.setPlayHead (nullptr);
    }
};


//==============================================================================
struct EditorRenderTests : public juce::UnitTest
{
    EditorRenderTests() : juce::UnitTest ("Editor renders offscreen (construction, layout, palette)", "GhostVerb") {}

    static int countNear (const juce::Image& img, juce::Colour c, int tol)
    {
        int n = 0;
        for (int y = 0; y < img.getHeight(); ++y)
            for (int x = 0; x < img.getWidth(); ++x)
            {
                auto p = img.getPixelAt (x, y);
                if (std::abs ((int) p.getRed() - (int) c.getRed()) <= tol && std::abs ((int) p.getGreen() - (int) c.getGreen()) <= tol
                    && std::abs ((int) p.getBlue() - (int) c.getBlue()) <= tol) ++n;
            }
        return n;
    }

    void save (const juce::Image& img, const juce::String& name)
    {
        const auto dir = juce::SystemStats::getEnvironmentVariable ("GHOSTVERB_SNAPSHOT_DIR", {});
        if (dir.isEmpty()) return;
        auto f = juce::File (dir).getChildFile (name);
        f.getParentDirectory().createDirectory();
        f.deleteFile();
        juce::FileOutputStream out (f);
        juce::PNGImageFormat().writeImageToStream (img, out);
    }

    void runTest() override
    {
        const double sr = 48000.0;
        juce::MidiBuffer midi;

        beginTest ("Empty state and populated state both render with the brand palette");
        {
            GhostVerbAudioProcessor p;
            p.prepareToPlay (sr, 512);
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            expect (ed != nullptr, "no editor");
            expectEquals (ed->getWidth(), 560);
            expectEquals (ed->getHeight(), 440);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            expectEquals (img.getWidth(), 560);
            expect (img.getPixelAt (3, 3) == juce::Colour (0xff111111), "background must be #111111");
            expect (countNear (img, juce::Colour (0xffc8a96e), 6) > 400, "gold #C8A96E missing");
            expect (countNear (img, juce::Colour (0xfff4f1ea), 40) > 150, "warm white text missing");
            save (img, "ui_empty.png");
            ed.reset();

            // synthetic "voice": three decaying sine bursts
            p.startCapture();
            juce::AudioBuffer<float> b (2, 512);
            long t = 0;
            for (int blk = 0; blk < 300; ++blk)
            {
                for (int i = 0; i < 512; ++i, ++t)
                {
                    const double sec = (double) t / sr;
                    const double inBurst = std::fmod (sec, 1.4);
                    const float v = sec < 0.5 ? 0.0f : (float) (0.7 * std::exp (-3.0 * inBurst) * std::sin (2.0 * 3.14159265 * (180.0 + 90.0 * std::floor (sec / 1.4)) * sec));
                    b.setSample (0, i, v); b.setSample (1, i, v * 0.9f);
                }
                p.processBlock (b, midi);
            }
            p.stopCapture();
            p.setSelection ((int) (0.5 * sr), (int) (1.9 * sr));
            expect (p.generate().isEmpty(), "generate");
            expect (waitFor ([&] { return p.renderer.getStatus() == AudioRenderer::Status::Ready; }, 30000), "render");

            ed.reset (p.createEditor());
            auto img2 = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            expect (countNear (img2, juce::Colour (0xffc8a96e), 6) > countNear (img, juce::Colour (0xffc8a96e), 6), "waveform / active controls should add gold");
            save (img2, "ui_ready.png");

            ed->setSize (840, 660);                       // resize keeps the 560x440 layout, scaled
            auto img3 = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            expectEquals (img3.getWidth(), 840);
            expect (img3.getPixelAt (5, 5) == juce::Colour (0xff111111), "scaled background");
            save (img3, "ui_scaled.png");
            ed.reset();
        }
    }
};

static ReverbCoreTests reverbCoreTests;
static CaptureTests captureTests;
static RendererTests rendererTests;
static ProcessorTests processorTests;
static EditorRenderTests editorRenderTests;

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::UnitTestRunner runner;
    runner.runAllTests();

    int failures = 0, total = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        auto* r = runner.getResult (i);
        failures += r->failures;
        total += r->passes + r->failures;
    }
    juce::Logger::writeToLog ("==== " + juce::String (total) + " checks, " + juce::String (failures) + " failures ====");
    return failures == 0 ? 0 : 1;
}
