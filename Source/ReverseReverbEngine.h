#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <functional>
#include <vector>

namespace gv
{
/** Stereo algorithmic reverb: 8-line feedback delay network (Hadamard mixing),
    input diffusion, per-line damping and slow delay modulation.
    The output is read exclusively from the delay lines, so it is 100 % wet:
    nothing of the input reaches the output before the shortest delay time. */
class FdnReverb
{
public:
    static constexpr int numLines = 8;

    /** size, tone: 0..1 */
    void prepare (double sampleRate, float size, float tone);
    void reset();
    void process (float inL, float inR, float& outL, float& outR) noexcept;

    /** Number of leading output samples that are guaranteed to be exactly zero for any input. */
    int guaranteedSilentSamples() const noexcept { return silentSamples; }
    double rt60Seconds() const noexcept          { return rt60; }

    static double rt60ForSize (float size) noexcept;

private:
    struct DelayLine
    {
        std::vector<float> buffer;
        int mask = 0, writePos = 0;
        float baseDelay = 0.0f, modDepth = 0.0f;
        double phase = 0.0, phaseInc = 0.0;
        float damp = 0.0f, gain = 0.0f;
    };

    struct Allpass
    {
        std::vector<float> buffer;
        int pos = 0;
        float process (float x) noexcept
        {
            const float d = buffer[(size_t) pos];
            const float w = x + 0.55f * d;
            buffer[(size_t) pos] = w;
            if (++pos >= (int) buffer.size()) pos = 0;
            return d - 0.55f * w;
        }
    };

    std::array<DelayLine, numLines> lines;
    std::array<Allpass, 3> apL, apR;
    float dampCoeff = 0.0f, injectGain = 0.0f, outScale = 0.0f;
    int silentSamples = 0;
    double rt60 = 1.0;
};

/** Offline reverse-reverb render:
      selection -> reverse -> 100 % wet reverb (+ complete tail) -> reverse everything -> wet-only audio. */
class ReverseReverbEngine
{
public:
    struct Settings
    {
        float size = 0.5f;      // 0..1  (room size + decay)
        float tone = 0.5f;      // 0..1  (dark..bright damping)
        float width = 1.0f;     // 0..2  (0 = mono, 1 = natural, 2 = wide)
        float outputDb = 0.0f;  // final volume
    };

    struct FullRender
    {
        juce::AudioBuffer<float> audio;   // reversed processed signal: [tail reversed | wet of reversed selection]
        int attackIndex = 0;              // sample where the selection start (the attack) sits
        int onsetSamples = 0;             // pre-delay of the reverb (guaranteed-silent samples): the swell peak sits this far before the attack
        double rt60 = 0.0;
    };

    /** Longest swell we accept (very slow tempos). */
    static constexpr double maxSwellSeconds = 60.0;

    static int tailSamplesFor (const Settings&, double sampleRate, int minTailSamples);

    /** Complete chain. `selection` may have 1 or 2 channels. Returns false if aborted. */
    static bool renderFull (const juce::AudioBuffer<float>& selection, double sampleRate, const Settings&,
                            int minTailSamples, FullRender& result,
                            const std::function<bool()>& shouldAbort = {});

    /** The swell: exactly `lengthSamples` long; its end (the loudest point of the build-up) falls exactly on the
        attack once the reverb's own pre-delay is removed. Applies OUTPUT,
        2 ms de-click fades and a safety scale that guarantees no sample exceeds 0.98. */
    static juce::AudioBuffer<float> extractSwell (const FullRender&, int lengthSamples,
                                                  double sampleRate, float outputDb);
};
} // namespace gv
